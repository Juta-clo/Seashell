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
#define PLINE_INTR -1

static void subshell_setup(int type) {
    env.subsh |= type;

    job_clear();
    sig_restore(&env.og_mask);

    sig_block(SIGCHLD);
    sig_set_handler(SIGCHLD, sigchld_handler);
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
    } else {
        for (size_t word_i = 0; word_i < cmd->words.size; ++word_i) {
            ps_word *word = &cmd->words.data[word_i];

            str_append_free(make_word_str(word), &out);

            if (word_i != cmd->words.size - 1)
                linc_string_append(&out, " ");
        }
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

char *make_ast_str(ps_ast *ast) {
    ASSERT(ast && ast->andors.size > 0);

    linc_string out = LINC_STRING_INIT;

    for (size_t andor_i = 0; andor_i < ast->andors.size; ++andor_i) {
        ps_andor *andor = &ast->andors.data[andor_i];

        if (andor->op == PS_AND_IF) {
            linc_string_append(&out, " && ");
        } else if (andor->op == PS_OR_IF) {
            linc_string_append(&out, " || ");
        }

        str_append_free(make_pline_str(&andor->pline), &out);
    }

    if (ast->bg)
        linc_string_append(&out, " &");

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

static void apply_redirs(da_redir *redirs, bool builtin) {
    for (size_t i = 0; i < redirs->size; ++i) {
        ps_redir *redir = &redirs->data[i];

        if (builtin && redir->io_num > 2) {
            LOG_INFO("builtin redirect rejected: %d", redir->io_num);
            continue;
        }

        if (redir->fd_dup) {
            xdup2(redir->fd_target, redir->io_num);
        } else {
            const char *arg = redir->target.arg;

            int fd;

            if (!redir->rdr_in) {
                if (redir->append) {
                    fd = xopen(arg, O_WRONLY | O_CREAT | O_APPEND, 0600);
                } else {
                    fd = xopen(arg, O_WRONLY | O_CREAT | O_TRUNC, 0600);
                }
            } else {
                fd = xopen(arg, O_RDONLY);
            }

            xdup2_move(fd, redir->io_num);
        }
    }
}

static sh_pgrp exec_pline(const ps_pline *pline, bool bg) {
    if (!pline)
        ASSERT(pline);

    int next_pipe[2];
    int prev_rfd;

    sh_pgrp pld = {0};

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
                xdup2_move(prev_rfd, STDIN_FILENO);

            if (!last) {
                xdup2_move(next_pipe[1], STDOUT_FILENO);
                xclose(next_pipe[0]);
            }

            sig_restore(&env.og_mask);

            if (!cur_cmd->expanded)
                if (expand_cmd(cur_cmd, EXPAND_CMD) == -1)
                    _exit(EXIT_FAILURE);

            apply_redirs(&cur_cmd->redirs, false);
            export_vars(&env.vars);

            if (cur_cmd->kind == PS_CMD_SUBSH) {
                subshell_setup(SUBSH_ASYNC);
                _exit(shell_run_andor(&cur_cmd->subsh));
            }

            if (cur_cmd->argc == 0)
                _exit(EXIT_SUCCESS);

            sh_builtin *builtin = get_builtin(cur_cmd->argv[0]);

            if (builtin)
                _exit(builtin->func(cur_cmd->argv, &env));

            execvp(cur_cmd->argv[0], cur_cmd->argv);

            if (errno == ENOENT) {
                err_msg("command not found: %s", cur_cmd->argv[0]);
                _exit(127);
            } else {
                err_exit("execvp");
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

        if (cur_cmd->kind != PS_CMD_SUBSH)
            close_proc_subs(cur_cmd);

        sh_proc *proc = linc_array_push(&pld.procs);
        proc->pid = cpid;
        proc->cmd = make_cmd_str(cur_cmd);
    }

    return pld;
}

static bool can_run_in_shell(ps_pline *pline) {
    ASSERT(pline && pline->cmds.size > 0);

    if (pline->cmds.size > 1)
        return false;

    if (pline->cmds.data[0].kind != PS_CMD_SIMPLE)
        return false;

    return true;
}

static bool try_run_builtin(ps_pline *pline, int jid) {
    ps_cmd *cmd = &pline->cmds.data[0];

    if (cmd->argc == 0)
        return false;

    sh_builtin *builtin = get_builtin(cmd->argv[0]);
    if (!builtin)
        return false;

    int saved_fds[3];
    for (size_t i = 0; i < 3; ++i)
        if ((saved_fds[i] = dup(i)) == -1 && errno != EBADF)
            err_exit("dup");

    apply_redirs(&cmd->redirs, true);
    int status = builtin->func(cmd->argv, &env);

    for (size_t i = 0; i < 3; ++i)
        xdup2_move(saved_fds[i], i);

    /* fg "replaces" itself with the existing job. Because we
    * don't queue fg's exit event we only reclaim the terminal
    * when the job that was fg'd exits (or stops) */
    if (!STREQ("fg", builtin->name) || status == EXIT_FAILURE) {
        LOG_INFO("queued \"%s\" exit event", builtin->name);
        char *cmd_str = make_cmd_str(cmd);
        job_queue_builtin_exit(jid, cmd_str, status);
        free(cmd_str);
    }

    close_proc_subs(cmd);
    return true;
}

static int shell_start_pline(ps_pline *pline, bool bg, int jid) {
    int status = -1;

    bool try_builtin = (
        can_run_in_shell(pline) &&
        (!bg || env.subsh & SUBSH_ASYNC)
    );

    if (try_builtin) {
        if (expand_cmd(&pline->cmds.data[0], EXPAND_CMD) == -1)
            goto cleanup;

        if (try_run_builtin(pline, jid)) {
            status = 0;
            goto cleanup;
        }
    }

    sh_pgrp pld = exec_pline(pline, bg);
    job_add(jid, &pld);

    status = 0;

cleanup:
    if (pline->cmds.data[0].kind == PS_CMD_SIMPLE)
        close_proc_subs(&pline->cmds.data[0]);

    return status;
}

static int shell_wait_pline(int jid, int *status) {
    job_event *jev;
    int rv;

    while (true) {
        sigsuspend(&env.og_mask);
        process_signals();

        while ((jev = job_event_pop())) {
            if (!env.subsh)
                handle_job_event(jev);

            if (jev->jid == jid) {
                if (jev->type == JEXITED) {
                    if (jev->last->stat == PROC_EXIT) {
                        *status = jev->last->exit_stat;
                    } else if (jev->last->stat == PROC_SIG) {
                        *status = jev->last->term_sig + 128;
                    }

                    rv = (jev->intr_sig) ? PLINE_INTR : 0;
                    goto cleanup;
                }

                if (!env.subsh && jev->type == JSTOPPED) {
                    if (jev->last->stat == PROC_STOP) {
                        *status = jev->last->stop_sig + 128;
                    } else {
                        *status = WAIT_EXIT;
                    }

                    rv = 0;
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
    return rv;
}

int shell_run_andor(ps_ast *ast) {
    int status = EXIT_SUCCESS;
    bool caller = true;

    if (ast->bg && ast->andors.size > 1) {
        ast->bg = false;

        int subsh_pgid = xfork();

        if (env.job_ctrl)
            setpgid(subsh_pgid, subsh_pgid);

        if (subsh_pgid != 0) {
            int jid = job_new_id();
            sh_pgrp pld = { .pgid = subsh_pgid };

            char *ast_str = make_ast_str(ast);
            linc_string subsh_str = LINC_STRING_INIT;
            linc_string_appendf(&subsh_str, "(%s)", ast_str);

            *linc_array_push(&pld.procs) = (sh_proc){
                .pid = subsh_pgid,
                .cmd = subsh_str.cstr,
            };

            free(ast_str);
            job_add(jid, &pld);

            if (!env.subsh && env.job_ctrl)
                printf("[%d] %d\n", jid, subsh_pgid);

            return status;
        } else {
            subshell_setup(SUBSH_ASYNC);
            caller = false;
        }
    }

    for (size_t i = 0; i < ast->andors.size; ++i) {
        ps_andor *andor = &ast->andors.data[i];
        int rv = 0;

        if (andor_should_run(andor, status)) {
            env.fg_jid = job_new_id();

            if (!ast->bg && !env.subsh && env.job_ctrl)
                xtcgetattr(env.tty_fd, &env.tio);

            if (shell_start_pline(&andor->pline, ast->bg, env.fg_jid) == -1) {
                if (last_exit_event.type == PSIGNALED &&
                    last_exit_event.term_sig == SIGINT) {
                    rv = PLINE_INTR;
                    status = SIGINT + 128;
                } else {
                    status = EXIT_FAILURE;
                }
            } else {
                if (ast->bg) {
                    if (!env.subsh && env.job_ctrl) {
                        pid_t pgrp = job_lookup_pgrp(env.fg_jid);
                        printf("[%d] %d\n", env.fg_jid, pgrp);
                    }

                    env.fg_jid = NOFG;
                    break;
                }

                rv = shell_wait_pline(env.fg_jid, &status);
            }

            update_status_variable(status);
            env.fg_jid = NOFG;

            if (rv == PLINE_INTR && env.job_ctrl) {
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

    int rv = lx_tokenize(line, n, &toks);
    if (rv != LX_OK) {
        err_msg("%s", lx_errstr(rv));
        lx_free(&toks);
        ps_free(ast);
        return -1;
    }

    if (toks.size == 0)
        return -1;

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

int shell_run_capture(const char *line, size_t n, int *pid) {
    ps_ast ast = {0};

    if (line_to_ast(line, n, &ast) == -1)
        return -1;

    int pipe_fd[2];
    xpipe(pipe_fd);

    pid_t cpid = xfork();

    if (cpid == 0) {
        subshell_setup(SUBSH_ASYNC);
        ignore_job_ctrl_signals();

        xdup2_move(pipe_fd[1], STDOUT_FILENO);
        xclose(pipe_fd[0]);

        _exit(shell_run_andor(&ast));
    }

    if (pid)
        *pid = cpid;

    xclose(pipe_fd[1]);
    ps_free(&ast);
    return pipe_fd[0];
}

int shell_run(const char *cmd) {
    ps_ast ast = {0};

    if (line_to_ast(cmd, strlen(cmd), &ast) == -1)
        return -1;

    int status = shell_run_andor(&ast);

    ps_free(&ast);
    return status;
}
