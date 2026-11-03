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

static int line_to_ast(const char *line, ps_ast *ast) {
    if (strcmp("", line) == 0)
        return -1;

    da_tok toks;
    int rv = lx_tokenize(line, &toks);
    if (rv != LX_OK) {
        err_msg("%s", lx_errstr(rv));
        return -1;
    }

    if (ps_parse(&toks, ast) == -1) {
        lx_free(&toks);
        return -1;
    }

    lx_free(&toks);

    return 0;
}

static void move_fd(int fd1, int fd2) {
    if (fd1 == fd2)
        return;

    xdup2(fd1, fd2);
    xclose(fd1);
}

static void child_fd_setup(bool first, bool last, int next_pipe[2], int prev_rfd) {
    if (!first)
        move_fd(prev_rfd, STDIN_FILENO);

    if (!last) {
        move_fd(next_pipe[1], STDOUT_FILENO);

        if (close(next_pipe[0]) == -1)
            err_exit("close");
    }
}

static void child_redir_setup(da_redir *redirs) {
    for (size_t i = 0; i < redirs->size; ++i) {
        ps_redir *redir = &redirs->data[i];
        const char *arg = redir->target.arg;

        int rfd;

        if (redir->io_num == STDIN_FILENO) {
            rfd = open(arg, O_RDONLY);
            if (rfd == -1)
                err_exit("open");
        } else {
            if (redir->append) {
                rfd = open(arg, O_WRONLY | O_CREAT | O_EXCL, 0600);
                if (rfd == -1) {
                    if (errno == EEXIST) {
                        rfd = open(arg, O_WRONLY | O_APPEND);
                        if (rfd == -1)
                            err_exit("open");
                    } else
                        err_exit("open");
                }
            } else {
                rfd = open(arg, O_WRONLY | O_CREAT | O_TRUNC, 0600);
                if (rfd == -1)
                    err_exit("open");
            }
        }

        move_fd(rfd, redir->io_num);
    }
}

static pline_data exec_pline(const ps_pline *pline, bool bg) {
    if (!pline)
        ASSERT(pline);

    int next_pipe[2];
    int prev_rfd;

    pline_data pld = {0};

    for (size_t i = 0; i < pline->cmds.size; ++i) {
        ps_cmd *cur_cmd = &pline->cmds.data[i];
        bool first = (i == 0);
        bool last = (i == pline->cmds.size - 1);

        if (!last)
            xpipe(next_pipe);

        int cpid = xfork();

        if (first)
            pld.pgid = (env.subsh != SUBSH_NONE) ? getpgrp() : cpid;

        if (cpid == 0) {
            xsetpgid(0, pld.pgid);

            if (!bg && first && env.subsh == SUBSH_NONE)
                xtcsetpgrp(env.tty_fd, getpgrp());

            env.subsh |= SUBSH_PIPE;

            child_fd_setup(first, last, next_pipe, prev_rfd);
            child_redir_setup(&cur_cmd->redirs);
            export_vars(&env.vars);

            sig_restore(&env.og_mask);

            /* is it a builtin? */

            int status;
            if (try_run_builtin(cur_cmd->argv, &status))
                _exit(status);

            /* exec program */

            execvp(cur_cmd->argv[0], cur_cmd->argv);

            if (errno == ENOENT) {
                err_msg("command not found: %s", cur_cmd->argv[0]);
                _exit(127);
            } else {
                LOG_ERR("execvp: %m");
                err_exit("execvp");
            }
        }

        if (setpgid(cpid, pld.pgid) == -1 && errno != EACCES) {
            LOG_ERR("setpgid: %m");
            err_exit("setpgid");
        }

        if (!bg && first && env.subsh == SUBSH_NONE)
            xtcsetpgrp(env.tty_fd, pld.pgid);

        if (!first)
            xclose(prev_rfd);

        if (!last) {
            prev_rfd = next_pipe[0];
            xclose(next_pipe[1]);
        }

        pline_proc *pproc = linc_array_push(&pld.procs);
        pproc->pid = cpid;
        pproc->argv = argv_copy(cur_cmd->argv);
    }

    return pld;
}

static void reclaim_terminal(void) {
    if (xtcsetpgrp(env.tty_fd, getpgrp()) == -1)
        err_exit("tcsetpgrp");

    env.fg_jid = NOFG;
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

static linc_string get_pline_str(const job_event *jev, int off, int argv_off,
        bool verbose)
{
    linc_string out = LINC_STRING_INIT;
    int min_offset = 1;

    for (size_t y = 0; y < jev->pviews.size; ++y) {
        proc_view *pview = &jev->pviews.data[y];
        size_t prev_len = out.len;

        if (verbose || y == 0 || !same_ev_type(pview, &pview[-1])) {
            if (pview->stat == PROC_EXIT) {
                if (pview->exit_stat == 0)
                    linc_string_appendf(&out, "done");
                else
                    linc_string_appendf(&out, "exit %d", pview->exit_stat);
            } else if (pview->stat == PROC_RUN) {
                linc_string_append(&out, "running");
            } else if (pview->stat == PROC_SIG) {
                linc_string_appendf(&out, "%s", strsignal(pview->term_sig));
            } else if (pview->stat == PROC_STOP) {
                linc_string_appendf(&out, "%s", strsignal(pview->stop_sig));
            } else
                argv_off -= 1;

            int len = off + (out.len - prev_len);
            int off = MAX(argv_off - len, min_offset);
            linc_string_appendf(&out, "%*s", off, "");
        }

        if (verbose)
            linc_string_appendf(&out, "%d ", pview->pid);

        size_t len = argv_len(pview->argv);

        for (size_t i = 0; i < len; ++i) {
            linc_string_append(&out, pview->argv[i]);
            if (i != len - 1)
                linc_string_append(&out, " ");
        }

        if (y != jev->pviews.size - 1) {
            if (!verbose && same_ev_type(pview, &pview[1])) {
                linc_string_appendf(&out, " | ");
            } else {
                linc_string_appendf(&out, " |\n%*s", off, "");
            }
        }
    }

    return out;
}

static bool andor_should_run(ps_andor *andor, int status) {
    if (andor->op == PS_AND_IF && status != EXIT_SUCCESS)
        return false;

    if (andor->op == PS_OR_IF && status == EXIT_SUCCESS)
        return false;

    return true;
}

static void handle_job_event(job_event *jev) {
    if (env.fg_jid != jev->jid) {
        print_job_event(jev->jid, jev, false);
        input_on_new_line();
    } else if (jev->type == JEXITED) {
        reclaim_terminal();

        if (jev->signaled)
            print_job_event(jev->jid, jev, false);
    } else if (jev->type == JSTOPPED) {
        reclaim_terminal();

        jc_job *job = job_lookup(jev->jid);
        if (!job)
            fatal("bad jctl state");

        /* Save current tio of stopped job, will reapply
         * in fg builtin on resumption */
        xtcgetattr(env.tty_fd, &job->tio);
        job->tio_saved = true;
        xtcsetattr(env.tty_fd, TCSANOW, &env.tio);

        print_job_event(jev->jid, jev, false);
    }
}

static void flush_wait_events(void) {
    wait_event wev;
    while (get_wstat(&wev) != -1)
        job_update(wev);
}

static void flush_job_events(void) {
    job_event *jev;

    while ((jev = job_event_pop())) {
        if (env.subsh == SUBSH_NONE)
            handle_job_event(jev);

        job_event_free(jev);
    }
}

static int shell_run_pline(ps_pline *pline, bool bg, int jid) {
    if (expand_pline(pline) == -1)
        return -1;

    if (!bg && env.subsh == SUBSH_NONE) {
        env.fg_jid = jid;
        xtcgetattr(env.tty_fd, &env.tio);
    }

    bool handled = false;
    bool try_builtin = pline->cmds.size == 1 &&
                       pline->cmds.data[0].redirs.size == 0 &&
                       (!bg || env.subsh != SUBSH_NONE);

    if (try_builtin) {
        sh_builtin *builtin = get_builtin(pline->cmds.data[0].argv[0]);

        if (builtin) {
            LOG_INFO("[%d] Started %s (no fork)", jid, builtin->name);

            int status = builtin->func(pline->cmds.data[0].argv, &env);

            /* fg "replaces" itself with the existing job. Because we
             * don't queue fg's exit event we only reclaim the termianl
             * when the job that was fg'd exits (or stops) */
            if (strcmp("fg", builtin->name) != 0 || status == EXIT_FAILURE) {
                LOG_INFO("queued \"%s\" exit event", builtin->name);
                job_queue_builtin_exit(jid, pline->cmds.data[0].argv, status);
            }

            handled = true;
        }
    }

    if (!handled) {
        pline_data pld = exec_pline(pline, bg);
        job_add(jid, &pld);

        LOG_INFO("[%d] Started %s %d", jid, pline->cmds.data[0].argv[0],
                pld.pgid);
    }

    return 0;
}

static int shell_wait_pline(int jid, int *status) {
    job_event *jev;
    int rv;

    while (true) {
        sigsuspend(&env.og_mask);
        process_signals();

        while ((jev = job_event_pop())) {
            if (env.subsh == SUBSH_NONE)
                handle_job_event(jev);

            if (jev->jid == jid) {
                if (jev->type == JEXITED) {
                    if (jev->last->stat == PROC_EXIT) {
                        *status = jev->last->exit_stat;
                    } else if (jev->last->stat == PROC_SIG) {
                        *status = jev->last->term_sig + 128;
                    }

                    if (jev->intr_sig) {
                        rv = PLINE_INTR;
                    } else {
                        rv = 0;
                    }

                    goto cleanup;
                }

                if (env.subsh == SUBSH_NONE && jev->type == JSTOPPED) {
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

static int shell_run_andor(ps_ast *ast) {
    int status = EXIT_SUCCESS;

    for (size_t i = 0; i < ast->andors.size; ++i) {
        ps_andor *andor = &ast->andors.data[i];

        if (andor_should_run(andor, status)) {
            env.fg_jid = job_new_id();

            if (shell_run_pline(&andor->pline, ast->bg, env.fg_jid) == -1) {
                status = EXIT_FAILURE;
                env.fg_jid = NOFG;
                continue;
            }

            if (env.subsh == SUBSH_NONE && ast->bg) {
                pid_t pgrp = job_lookup_pgrp(env.fg_jid);
                printf("[%d] %d\n", env.fg_jid, pgrp);
                env.fg_jid = NOFG;

                break;
            }

            int rv = shell_wait_pline(env.fg_jid, &status);
            update_status_variable(status);

            env.fg_jid = NOFG;

            if (rv == PLINE_INTR) {
                cursor_left_anchor();
                break;
            }
        }
    }

    return status;
}

static int shell_fork(void) {
    int cpid = xfork();
    int rv = setpgid(cpid, cpid);

    if (cpid != 0) {
        if (rv == -1 && errno != EACCES)
            err_exit("setpgid");

        return cpid;
    }

    env.subsh |= SUBSH_ASYNC;

    job_clear();
    sig_restore(&env.og_mask);

    sig_block(SIGCHLD);
    sig_set_handler(SIGCHLD, sigchld_handler);

    return 0;
}

void seashell_shutdown(void) {
    input_deinit();
    job_hangup();

    LOG_INFO("seashell shutting down");
}

void process_signals(void) {
    if (sigchld_caught) {
        LOG_INFO("SIGCHLD caught");
        sigchld_caught = false;

        flush_wait_events();

        if (env.fg_jid == NOFG && env.subsh == SUBSH_NONE)
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

void print_job_event(int jid, const job_event *jev, bool verbose) {
    if (env.subsh != SUBSH_NONE)
        return;

    linc_string msg = LINC_STRING_INIT;
    linc_string_appendf(&msg, "[%d] ", jid);

    int offset = msg.len;

    linc_string s = get_pline_str(jev, offset, 21, verbose);
    linc_string_append(&msg, s.cstr);
    linc_string_free(&s);

    cursor_left_anchor();
    printf("%s\n", msg.cstr);

    LOG_INFO("%s", msg.cstr);
    linc_string_free(&msg);
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

int shell_run(const char *cmd) {
    ps_ast ast = {0};
    char *line = xstrdup(cmd);
    int status = -1;

    if (line_to_ast(line, &ast) == -1)
        goto cleanup;

    if (!ast.bg || ast.andors.size == 1) {
        status = shell_run_andor(&ast);
        goto cleanup;
    }

    int subsh_pgid = shell_fork();

    if (subsh_pgid == 0) {
        status = shell_run_andor(&ast);
        _exit(status);
    }

    int jid = job_new_id();
    pline_data pld = { .pgid = subsh_pgid };

    *linc_array_push(&pld.procs) = (pline_proc) {
        .argv = argv_copy((char *[]){ line, nullptr }),
        .pid = subsh_pgid
    };

    job_add(jid, &pld);
    status = EXIT_SUCCESS;

    pid_t pgrp = job_lookup_pgrp(jid);
    printf("[%d] %d\n", jid, pgrp);

cleanup:
    ps_free(&ast);
    free(line);
    return status;
}
