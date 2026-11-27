#include <errno.h>

#include "shell.h"
#include "builtins.h"
#include "expander.h"
#include "input.h"
#include "jobs.h"
#include "linc/xsys.h"
#include "parser.h"
#include "sig_funcs.h"
#include "utils.h"
#include "config.h"
#include "variable.h"
#include "state.h"

#define PROMPT_DEFAULT "> "
#define PROMPT_LUA "\x1b[36mlua\x1b[m \x1b[35m>\x1b[m "

#define WAIT_EXIT 193 /* +1 highest signal exit status */

static ps_ast ast_nd = {
    .next = &ast_nd,
    .prev = &ast_nd
};

[[maybe_unused]] static void ast_ref(ps_ast *ast) {
    ASSERT(ast && ast->refs > 0);
    ASSERT(ast != &ast_nd);

    ++ast->refs;
}

static ps_ast *ast_acquire(const char *line, size_t n) {
    ASSERT(line);

    ps_ast *out = xmalloc(sizeof(ps_ast));
    *out = (ps_ast){0};

    if (line_to_ast(line, n, out) == -1) {
        free(out);
        return nullptr;
    }

    out->refs = 1;
    out->prev = ast_nd.prev;
    out->next = &ast_nd;

    ast_nd.prev->next = out;
    ast_nd.prev = out;

    return out;
}

static void ast_release(ps_ast *ast) {
    ASSERT(ast && ast->refs > 0);

    if (--ast->refs == 0) {
        ast->prev->next = ast->next;
        ast->next->prev = ast->prev;

        ps_free(ast);
        free(ast);
    }
}

static int run_ast(ps_ast *ast);

static void subshell_setup(int type) {
    env.subsh |= type;

    job_clear();
    sig_restore(&env.og_mask);

    sig_block(SIGCHLD);
    sig_set_handler(SIGCHLD, sigchld_handler);
    drop_shell_fds(true);
}

static void cursor_left_anchor(void) {
    struct winsize ws;
    if (ioctl(env.tty_fd, TIOCGWINSZ, &ws) == -1)
        fatal("ioctl");

    printf("%*s\x1b[1G", ws.ws_col, "");
}

static bool same_ev_type(const proc_view *p1, const proc_view *p2) {
    return (p1->stat == p2->stat) &&
        (p2->stat == PROC_RUN ||
        (p2->stat == PROC_EXIT && p1->exit_stat == p2->exit_stat) ||
        (p1->stat == PROC_SIG && p1->term_sig == p2->term_sig) ||
        (p1->stat == PROC_STOP && p1->stop_sig == p2->stop_sig));
}

static void str_append_free(char *s, linc_string *base) {
    linc_string_append(base, s);
    free(s);
}

char *make_word_str(ps_word *word) {
    linc_string out = LINC_STRING_INIT;

    for (size_t seg_i = 0; seg_i < word->segments.size; ++seg_i) {
        ps_segment *seg = &word->segments.data[seg_i];

        if (seg->quote == PS_Q_NONE) {
            linc_string_append(&out, seg->raw);
        } else if (seg->quote == PS_Q_DOUBLE) {
            linc_string_appendf(&out, "\"%s\"", seg->raw);
        } else if (seg->quote == PS_Q_SINGLE) {
            linc_string_appendf(&out, "'%s'", seg->raw);
        }
    }


    char *word_str = xstrdup(out.cstr);
    linc_string_free(&out);
    return word_str;
}

char *make_redir_str(ps_redir *redir) {
    linc_string out = LINC_STRING_INIT;
    char *rdr_op = (redir->rdr_in) ? "<" : ">";

    if (redir->append)
        rdr_op = ">>";

    if (redir->fd_dup) {
        linc_string_appendf(
            &out, "%d%s&%d",
            redir->io_num, rdr_op, redir->fd_target
        );
    } else {
        if ((redir->io_num != 0 && redir->rdr_in) ||
            (redir->io_num != 1 && !redir->rdr_in))
            linc_string_appendf(&out, "%d", redir->io_num);

        linc_string_appendf(&out, "%s ", rdr_op);
        str_append_free(make_word_str(&redir->target), &out);
    }

    char *redir_str = xstrdup(out.cstr);
    linc_string_free(&out);
    return redir_str;
}

char *make_cmd_str(ps_cmd *cmd) {
    linc_string out = LINC_STRING_INIT;

    if (cmd->kind == PS_CMD_SUBSH) {
        char *ast_str = make_ast_str(&cmd->subsh);
        linc_string_appendf(&out, "(%s)", ast_str);
        free(ast_str);
    } else if (cmd->kind == PS_CMD_IF) {
        char *cond_str = make_ast_str(&cmd->if_st.cond);
        char *body_str = make_ast_str(&cmd->if_st.body);
        linc_string_appendf(&out, "if %s { %s }", cond_str, body_str);
        free(cond_str);
        free(body_str);
    } else if (cmd->kind == PS_CMD_WHILE) {
        char *cond_str = make_ast_str(&cmd->while_st.cond);
        char *body_str = make_ast_str(&cmd->while_st.body);
        linc_string_appendf(&out, "while %s { %s }", cond_str, body_str);
        free(cond_str);
        free(body_str);
    } else if (cmd->kind == PS_CMD_FOR) {
        char *ind_str = make_word_str(&cmd->for_st.ind);
        linc_string_appendf(&out, "for %s; ", ind_str);
        free(ind_str);

        for (size_t i = 0; i < cmd->for_st.vals.size; ++i) {
            char *val_str = make_word_str(&cmd->for_st.vals.data[i]);
            linc_string_appendf(&out, "%s ", val_str);
            free(val_str);
        }

        char *body_str = make_ast_str(&cmd->for_st.body);
        linc_string_appendf(&out, "{ %s }", body_str);
        free(body_str);
    } else if (cmd->kind == PS_CMD_SIMPLE) {
        for (size_t word_i = 0; word_i < cmd->simple.words.size; ++word_i) {
            ps_word *word = &cmd->simple.words.data[word_i];

            str_append_free(make_word_str(word), &out);

            if (word_i != cmd->simple.words.size - 1)
                linc_string_append(&out, " ");
        }
    } else {
        fatal("unexpected command type");
    }

    for (size_t redir_i = 0; redir_i < cmd->redirs.size; ++redir_i) {
        ps_redir *redir = &cmd->redirs.data[redir_i];

        linc_string_append(&out, " ");
        str_append_free(make_redir_str(redir), &out);
    }

    char *cmd_str = xstrdup(out.cstr);
    linc_string_free(&out);
    return cmd_str;
}

char *make_pline_str(ps_pline *pline) {
    linc_string out = LINC_STRING_INIT;

    for (size_t cmd_i = 0; cmd_i < pline->cmds.size; ++cmd_i) {
        ps_cmd *cmd = &pline->cmds.data[cmd_i];

        str_append_free(make_cmd_str(cmd), &out);

        if (cmd_i != pline->cmds.size - 1)
            linc_string_append(&out, " | ");
    }

    char *pline_str = xstrdup(out.cstr);
    linc_string_free(&out);
    return pline_str;
}

char *make_chain_str(ps_chain *chain) {
    ASSERT(chain && chain->andors.size > 0);

    linc_string out = LINC_STRING_INIT;

    for (size_t andor_i = 0; andor_i < chain->andors.size; ++andor_i) {
        ps_andor *andor = &chain->andors.data[andor_i];

        if (andor->op == PS_AND_IF) {
            linc_string_append(&out, " && ");
        } else if (andor->op == PS_OR_IF) {
            linc_string_append(&out, " || ");
        }

        str_append_free(make_pline_str(&andor->pline), &out);
    }

    if (chain->bg)
        linc_string_append(&out, " &");

    char *ast_str = xstrdup(out.cstr);
    linc_string_free(&out);
    return ast_str;
}

char *make_ast_str(ps_ast *ast) {
    linc_string out = LINC_STRING_INIT;

    for (size_t i = 0; i < ast->chains.size; ++i) {
        ps_chain *chain = &ast->chains.data[i];
        str_append_free(make_chain_str(chain), &out);

        if (i != ast->chains.size - 1)
            linc_string_append(&out, "; ");
    }

    char *ast_str = xstrdup(out.cstr);
    linc_string_free(&out);
    return ast_str;
}

void print_job_event(const job_event *jev, bool verbose) {
    if (env.subsh || !env.job_ctrl)
        return;

    cursor_left_anchor();

    int init_off = printf("[%d] ", jev->jid);
    int last_len;
    int argv_off = 21;

    for (size_t i = 0; i < jev->pviews.size; ++i) {
        proc_view *pview = &jev->pviews.data[i];

        if (verbose || i == 0 || !same_ev_type(pview, &pview[-1])) {
            if (pview->stat == PROC_EXIT) {
                if (pview->exit_stat == 0) {
                    last_len = printf("done");
                } else {
                    last_len = printf("exit %d", pview->exit_stat);
                }
            } else if (pview->stat == PROC_RUN) {
                last_len = printf("running");
            } else if (pview->stat == PROC_SIG) {
                last_len = printf("%s", strsignal(pview->term_sig));
            } else if (pview->stat == PROC_STOP) {
                last_len = printf("%s", strsignal(pview->stop_sig));
            } else {
                argv_off -= 1;
            }

            int off = MAX(argv_off - (last_len + init_off), 1);
            printf("%*s", off, "");
        }

        if (verbose)
            printf("%d ", pview->pid);

        input_buf ibuf = input_get_cells(pview->cmd, 0);
        input_highlight(&ibuf, pview->cmd, strlen(pview->cmd), 0);

        for (size_t i = 0; i < ibuf.cells.size; ++i)
            input_draw_cell(&ibuf.cells.data[i], nullptr);

        input_free_cells(&ibuf);

        if (i != jev->pviews.size - 1) {
            printf (" ");

            input_buf ibuf = input_get_cells("|", 0);
            input_highlight_tok(0, 0, LX_TOK_PIPE, &ibuf, 0);

            for (size_t i = 0; i < ibuf.cells.size; ++i)
                input_draw_cell(&ibuf.cells.data[i], nullptr);

            input_free_cells(&ibuf);

            if (!verbose && same_ev_type(pview, &pview[1])) {
                printf(" ");
            } else {
                printf("\n%*s", init_off, "");
            }
        }
    }

    printf("\n");
}

static void reclaim_terminal(bool restore_termios) {
    if (!env.job_ctrl)
        return;

    if (xtcsetpgrp(env.tty_fd, getpgrp()) == -1)
        err_exit("tcsetpgrp");

    env.fg_jid = NOFG;

    if (restore_termios) {
        LOG_INFO("Job exited abnormally. restoring termios");
        xtcsetattr(env.tty_fd, TCSANOW, &env.tio);
    }
}

static void handle_job_event(job_event *jev) {
    if (env.fg_jid != jev->jid) {
        print_job_event(jev, false);
        input_on_new_line();
    } else if (jev->type == JEXITED) {
        reclaim_terminal(jev->signaled);

        if (jev->signaled)
            print_job_event(jev, false);
    } else if (jev->type == JSTOPPED) {
        jc_job *job = job_lookup(jev->jid);
        if (!job) {
            fatal("bad jctl state");
            /* Return instead of fatal? */
        }

        if (env.job_ctrl)
            xtcgetattr(env.tty_fd, &job->tio);
        reclaim_terminal(true);
        print_job_event(jev, false);
    }
}

static void flush_wait_events(void) {
    wait_event wev;
    while (get_wstat(&wev, -1, WOPT_JOB) != -1)
        job_update(wev);
}

static void flush_job_events(void) {
    job_event *jev;

    while ((jev = job_event_pop())) {
        if (!env.subsh)
            handle_job_event(jev);

        job_event_free(jev);
    }
}

static bool andor_should_run(ps_andor *andor, int status) {
    if (andor->op == PS_AND_IF && status != EXIT_SUCCESS)
        return false;

    if (andor->op == PS_OR_IF && status == EXIT_SUCCESS)
        return false;

    return true;
}

static int apply_redirs(da_ex_redir *redirs, bool main_shell) {
    for (size_t i = 0; i < redirs->size; ++i) {
        ex_redir *redir = &redirs->data[i];

        if (main_shell && redir->io_num > 2) {
            LOG_INFO("builtin redirect rejected: %d", redir->io_num);
            continue;
        }

        if (redir->fd_dup) {
            if (dup2(redir->fd_target, redir->io_num) == -1) {
                err_msg("%m");
                return -1;
            }
        } else {
            const char *arg = redir->target;

            int fd;

            if (!redir->rdr_in) {
                if (redir->append) {
                    fd = open(arg, O_WRONLY | O_CREAT | O_APPEND, 0600);
                } else {
                    fd = open(arg, O_WRONLY | O_CREAT | O_TRUNC, 0600);
                }
            } else {
                fd = open(arg, O_RDONLY);
            }

            if (fd == -1) {
                err_msg("open: %m");
                return -1;
            }

            fd_move(fd, redir->io_num, false, true);
        }
    }

    return 0;
}

static int run_if(ps_if *if_st) {
    int status = run_ast(&if_st->cond);
    if (status == -1)
        return status;

    if (status == EXIT_SUCCESS)
        status = run_ast(&if_st->body);

    return status;
}

static int run_while(ps_while *while_st) {
    int status;

    while (true) {
        status = run_ast(&while_st->cond);
        if (status == -1)
            return status;

        if (status == EXIT_SUCCESS) {
            status = run_ast(&while_st->body);
            if (status == -1)
                break;

            if (env.breaking) {
                env.breaking = false;
                break;
            }

            if (env.continuing)
                env.continuing = false;
        } else {
            break;
        }
    }

    return status;
}

static int run_for(ex_res *res, ps_for *for_st) {
    int status = EXIT_SUCCESS;

    for (size_t i = 0; i < res->vals.size; ++i) {
        char *part = res->vals.data[i];

        status = add_var(&env.vars, res->ind, part, false);
        if (status == -1) {
            err_msg("bad index variable: %s", res->ind);
            return EXIT_FAILURE;
        }

        status = run_ast(&for_st->body);

        if (env.breaking) {
            env.breaking = false;
            break;
        }

        if (env.continuing)
            env.continuing = false;

        if (status == -1)
            break;
    }

    return status;
}

static int run_simple(ex_res *res) {
    if (res->argc == 0)
        _exit(EXIT_SUCCESS);

    sh_builtin *builtin = get_builtin(res->argv.data[0]);
    if (builtin)
        _exit(builtin->func(res->argv.data, &env));

    execvp(res->argv.data[0], res->argv.data);

    if (errno == ENOENT) {
        err_msg("command not found: %s", res->argv.data[0]);
        _exit(127);
    } else {
        err_exit("execvp");
    }
}

static int run_pline(ps_pline *pline, bool bg, int jid) {
    int rv = -1;
    sh_builtin *builtin;
    ps_cmd *first_cmd = &pline->cmds.data[0];
    char *first_cmd_str = make_cmd_str(first_cmd);
    ex_res res = {0};

    if (expand_cmd(first_cmd, &res) == -1)
        goto cleanup;

    bool run_in_main = (
        (!bg || (env.subsh & SUBSH_ASYNC)) &&
        (pline->cmds.size == 1) &&
        (first_cmd->kind != PS_CMD_SUBSH) &&
        (first_cmd->kind != PS_CMD_SIMPLE ||
        ((res.argc > 0) && (builtin = get_builtin(res.argv.data[0]))))
    );

    if (run_in_main) {
        int saved_jid = env.fg_jid;
        int status;

        save_shell_fds();

        if (apply_redirs(&res.redirs, true) != -1) {
            if (first_cmd->kind == PS_CMD_SIMPLE) {
                status = builtin->func(res.argv.data, &env);
            } else {
                switch (first_cmd->kind) {
                case PS_CMD_IF:
                    status = run_if(&first_cmd->if_st);
                    break;
                case PS_CMD_WHILE:
                    status = run_while(&first_cmd->while_st);
                    break;
                case PS_CMD_FOR:
                    status = run_for(&res, &first_cmd->for_st);
                    break;
                default:           fatal("unexpected command type");
                }
            }
        } else {
            status = EXIT_FAILURE;
        }

        if (first_cmd->kind == PS_CMD_SIMPLE) {
            /* fg "replaces" itself with the existing job. Because we
            * don't queue fg's exit event we only reclaim the terminal
            * when the job that was fg'd exits (or stops) */
            if (!STREQ("fg", builtin->name) || status == EXIT_FAILURE)
                job_queue_exit(jid, first_cmd_str, status);
        } else {
            env.fg_jid = saved_jid;

            if (status == -1) {
                job_queue_interrupt(jid, first_cmd_str);
            } else {
                job_queue_exit(jid, first_cmd_str, status);
            }
        }

        drop_shell_fds(false);

        rv = 0;
        goto cleanup;
    }

    sh_pgrp pld = {0};
    int next_pipe[2];
    int prev_rfd;

    for (size_t i = 0; i < pline->cmds.size; ++i) {
        ps_cmd *cur_cmd = &pline->cmds.data[i];
        bool first = (i == 0);
        bool last = (i == pline->cmds.size - 1);

        if (!last)
            xpipe(next_pipe);

        int cpid = xfork();

        if (first)
            pld.pgid = (env.subsh & SUBSH_ASYNC) ? getpgrp() : cpid;

        if (cpid == 0) {
            if (env.job_ctrl)
                xsetpgid(0, pld.pgid);

            if (!bg && first && !env.subsh && env.job_ctrl)
                xtcsetpgrp(env.tty_fd, getpgrp());

            env.subsh |= SUBSH_PIPE;

            if (!first)
                fd_move(prev_rfd, STDIN_FILENO, false, true);

            if (!last) {
                fd_move(next_pipe[1], STDOUT_FILENO, false, true);
                xclose(next_pipe[0]);
            }

            sig_restore(&env.og_mask);

            if (!first)
                if (expand_cmd(cur_cmd, &res) == -1)
                    _exit(EXIT_FAILURE);

            if (apply_redirs(&res.redirs, false) == -1)
                _exit(EXIT_FAILURE);

            export_vars(&env.vars);

            if (cur_cmd->kind == PS_CMD_SIMPLE)
                run_simple(&res);

            subshell_setup(SUBSH_ASYNC);

            switch (cur_cmd->kind) {
            case PS_CMD_IF:    _exit(run_if(&cur_cmd->if_st));
            case PS_CMD_WHILE: _exit(run_while(&cur_cmd->while_st));
            case PS_CMD_FOR:   _exit(run_for(&res, &cur_cmd->for_st));
            case PS_CMD_SUBSH:
                rv = run_ast(&cur_cmd->subsh);
                _exit((rv == -1) ? SIGINT + 128 : rv);
            default:           fatal("unexpected command type");
            }
        }

        if (env.job_ctrl)
            if (setpgid(cpid, pld.pgid) == -1 && errno != EACCES)
                err_exit("setpgid");

        if (!bg && first && !env.subsh && env.job_ctrl)
            xtcsetpgrp(env.tty_fd, pld.pgid);

        if (!first)
            xclose(prev_rfd);

        if (!last) {
            prev_rfd = next_pipe[0];
            xclose(next_pipe[1]);
        }

        if (first)
            expand_free(&res);

        sh_proc *proc = linc_array_push(&pld.procs);
        proc->pid = cpid;
        proc->cmd = make_cmd_str(cur_cmd);
    }

    job_add(jid, &pld);

    char *pline_str = make_pline_str(pline);
    LOG_INFO("started '%s' %d", pline_str, pld.pgid);
    free(pline_str);

    rv = 0;

cleanup:
    expand_free(&res);
    free(first_cmd_str);

    return rv;
}

static int wait_pline(int jid) {
    job_event *jev;
    int status;

    while (true) {
        sigsuspend(&env.og_mask);
        process_signals();

        while ((jev = job_event_pop())) {
            if (!env.subsh)
                handle_job_event(jev);

            if (jev->jid == jid) {
                if (jev->type == JEXITED) {
                    if (jev->last->stat == PROC_EXIT) {
                        status = jev->last->exit_stat;
                    } else if (jev->last->stat == PROC_SIG) {
                        status = jev->last->term_sig + 128;
                    } else {
                        fatal("unexpected pline status");
                    }

                    if (jev->intr_sig)
                        status = -1;

                    goto cleanup;
                }

                if (!env.subsh && jev->type == JSTOPPED) {
                    if (jev->last->stat == PROC_STOP) {
                        status = jev->last->stop_sig + 128;
                    } else {
                        status = WAIT_EXIT;
                    }

                    goto cleanup;
                }
            }

            job_event_free(jev);
            jev = nullptr;
        }
    }

cleanup:
    job_event_free(jev);
    flush_job_events();
    return status;
}

int shell_run_chain(ps_chain *chain) {
    int status = EXIT_SUCCESS;
    bool caller = true;

    if (chain->bg && chain->andors.size > 1) {
        chain->bg = false;

        int subsh_pgid = xfork();

        if (env.job_ctrl)
            setpgid(subsh_pgid, subsh_pgid);

        if (subsh_pgid != 0) {
            int jid = job_new_id();
            sh_pgrp pld = { .pgid = subsh_pgid };

            char *chain_str = make_chain_str(chain);
            linc_string subsh_str = LINC_STRING_INIT;
            linc_string_appendf(&subsh_str, "(%s)", chain_str);

            *linc_array_push(&pld.procs) = (sh_proc){
                .pid = subsh_pgid,
                .cmd = subsh_str.cstr,
            };

            free(chain_str);
            job_add(jid, &pld);

            if (!env.subsh && env.job_ctrl)
                printf("[%d] %d\n", jid, subsh_pgid);

            return status;
        } else {
            subshell_setup(SUBSH_ASYNC);
            caller = false;
        }
    }

    for (size_t i = 0; i < chain->andors.size; ++i) {
        ps_andor *andor = &chain->andors.data[i];

        if (andor_should_run(andor, status)) {
            env.fg_jid = job_new_id();

            if (!chain->bg && !env.subsh && env.job_ctrl)
                xtcgetattr(env.tty_fd, &env.tio);

            if (run_pline(&andor->pline, chain->bg, env.fg_jid) == -1) {
                status = (last_interrupted()) ? -1 : EXIT_FAILURE;

                if (last_interrupted()) {
                    LOG_INFO("pline interrupted");
                } else {
                    LOG_INFO("pline failed");
                }
            } else {
                if (chain->bg) {
                    if (!env.subsh && env.job_ctrl) {
                        pid_t pgrp = job_lookup_pgrp(env.fg_jid);
                        printf("[%d] %d\n", env.fg_jid, pgrp);
                    }

                    env.fg_jid = NOFG;
                    break;
                }

                status = wait_pline(env.fg_jid);
            }

            update_status_variable((status == -1) ? SIGINT + 128 : status);
            env.fg_jid = NOFG;

            if (status == -1 && env.job_ctrl) {
                cursor_left_anchor();
                break;
            }
        }
    }

    if (!caller)
        _exit(status);

    return status;
}

int line_to_ast(const char *line, size_t n, ps_ast *ast) {
    da_tok toks = {0};
    *ast = (ps_ast){0};

    int rv = lx_tokenize(line, n, &toks);
    if (rv != LX_OK) {
        err_msg("%s", lx_errstr(rv));
        lx_free(&toks);
        return -1;
    }

    if (toks.size == 0)
        return 0;

    ps_scan scan = {
        .toks = &toks,
        .print_err = true,
        .alias = true,
    };

    if (ps_parse(&scan, ast) == -1) {
        lx_free(&toks);
        ps_free(ast);
        return -1;
    }

    lx_free(&toks);
    return 0;
}

void seashell_shutdown(void) {
    if (env.job_ctrl) {
        input_deinit();
        job_hangup();
    }

    LOG_INFO("seashell shutting down");
}

void process_signals(void) {
    if (sigchld_caught || sig_is_pending(SIGCHLD)) {
        LOG_INFO("SIGCHLD caught");
        sigchld_caught = false;

        flush_wait_events();

        if (env.fg_jid == NOFG && !env.subsh)
            flush_job_events();
    }

    if (sigint_caught) {
        LOG_INFO("SIGINT caught");
        sigint_caught = false;

        printf("^C\n");
        input_reset_line_state();
    }

    if (sighup_caught) {
        LOG_INFO("SIGHUP caught");
        sighup_caught = false;

        exit(EXIT_FAILURE);
    }
}

linc_string shell_get_prompt(void) {
    linc_string prompt = LINC_STRING_INIT;

    if (env.lua) {
        linc_string_append(&prompt, PROMPT_LUA);
    } else {
        if (config_field_prompt(&prompt) == -1)
            linc_string_append(&prompt, PROMPT_DEFAULT);
    }

    return prompt;
}

int shell_run_file(const char *path) {
    ASSERT(path);

    linc_string file_content = LINC_STRING_INIT;

    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        err_msg("%m: %s", path);
        return EXIT_FAILURE;
    }

    read_to_eof(fd, &file_content);
    xclose(fd);

    return shell_run(file_content.cstr);
}

int shell_run_capture(const char *line, size_t n, int *pid) {
    ps_ast *ast = ast_acquire(line, n);
    if (!ast)
        return -1;

    if (ast->chains.size == 0) {
        ast_release(ast);
        return -1;
    }

    int pipe_fd[2];
    xpipe(pipe_fd);

    pipe_fd[0] = fd_move(
        pipe_fd[0],
        STATE_FD_PRIVATE_BASE,
        false, false
    );

    pid_t cpid = xfork();

    if (cpid == 0) {
        subshell_setup(SUBSH_ASYNC | SUBSH_CAPTURE);
        ignore_job_ctrl_signals();

        fd_move(pipe_fd[1], STDOUT_FILENO, false, true);
        xclose(pipe_fd[0]);

        int status = run_ast(ast);
        _exit((status == -1) ? SIGINT + 128 : status);
    }

    if (pid)
        *pid = cpid;

    xclose(pipe_fd[1]);

    ast_release(ast);
    return pipe_fd[0];
}

static int run_ast(ps_ast *ast) {
    ASSERT(ast);

    int status = EXIT_SUCCESS;

    for (size_t i = 0; i < ast->chains.size; ++i) {
        if (env.breaking || env.continuing)
            break;

        status = shell_run_chain(&ast->chains.data[i]);

        if (status == -1)
            break;
    }

    return status;
}

int shell_run(const char *cmd) {
    ps_ast *ast = ast_acquire(cmd, strlen(cmd));
    if (!ast)
        return -1;

    if (ast->chains.size == 0) {
        ast_release(ast);
        return EXIT_SUCCESS;
    }

    int status = run_ast(ast);
    if (status == -1)
        status = SIGINT + 128;

    ast_release(ast);
    return status;
}
