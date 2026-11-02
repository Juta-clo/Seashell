#include <errno.h>

#include "shell.h"
#include "builtins.h"
#include "expander.h"
#include "input.h"
#include "job_state.h"
#include "linc/xsys.h"
#include "parser.h"
#include "sig_funcs.h"
#include "utils.h"
#include "config.h"
#include "variable.h"
#include "state.h"

#define PROMPT_DEFAULT "> "
#define PROMPT_LUA "\x1b[36mlua\x1b[m \x1b[35m>\x1b[m "
#define IN_BG 1
#define IN_FG 2

#define WAIT_EXIT 193 /* +1 highest signal exit status */
#define PLINE_INTR -1

static int line_to_ast(const char *line, ps_ast *ast) {
    if (strcmp("", line) == 0)
        return -1;

    da_tok toks;
    int rv = lx_tokenize(line, &toks);
    if (rv != LX_OK) {
        err_msg(lx_errstr(rv));
        return -1;
    }

    if (ps_parse(&toks, ast) == -1) {
        lx_free(&toks);
        return -1;
    }

    lx_free(&toks);

    return 0;
}

linc_string shell_get_prompt(void) {
    linc_string prompt = LINC_STRING_INIT;

    if (sh_env.lua_mode) {
        linc_string_append(&prompt, PROMPT_LUA);
    } else {
        if (config_field_prompt(&prompt) == -1)
            linc_string_append(&prompt, PROMPT_DEFAULT);
    }

    return prompt;
}

void move_fd(int fd1, int fd2) {
    if (fd1 == fd2)
        return;

    xdup2(fd1, fd2);
    xclose(fd1);
}

void child_fd_setup(bool first, bool last, int next_pipe[2], int prev_rfd) {
    /* Set up file descriptors */
    if (!first)
        move_fd(prev_rfd, STDIN_FILENO);

    if (!last) {
        move_fd(next_pipe[1], STDOUT_FILENO);

        if (close(next_pipe[0]) == -1)
            err_exit("close");
    }
}

void child_redir_setup(da_redir *redirs) {
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

pline_data exec_pline(const ps_pline *pline, bool bg) {
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
            pld.pgid = (sh_env.subsh != SUBSH_NONE) ? getpgrp() : cpid;

        if (cpid == 0) {
            xsetpgid(0, pld.pgid);

            if (!bg && first && sh_env.subsh == SUBSH_NONE)
                xtcsetpgrp(sh_env.tty_fd, getpgrp());

            sh_env.subsh |= SUBSH_PIPE;

            child_fd_setup(first, last, next_pipe, prev_rfd);
            child_redir_setup(&cur_cmd->redirs);
            export_vars(&sh_env.vars);

            sig_restore(&sh_env.og_mask);

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

        if (!bg && first && sh_env.subsh == SUBSH_NONE)
            xtcsetpgrp(sh_env.tty_fd, pld.pgid);

        if (!first)
            xclose(prev_rfd);

        if (!last) {
            prev_rfd = next_pipe[0];
            xclose(next_pipe[1]);
        }

        pline_proc *pproc = linc_array_push(&pld.procs);
        pproc->pid = cpid;
        pproc->argv = copy_argv(cur_cmd->argv);
    }

    return pld;
}

int shell_run_pline(ps_pline *pline, bool bg, pid_t jid) {
    if (expand_pline(pline) == -1)
        return -1;

    if (!bg && sh_env.subsh == SUBSH_NONE) {
        sh_env.fg_jid = jid;
        xtcgetattr(sh_env.tty_fd, &sh_env.tio);
    }

    bool handled = false;
    bool try_builtin = pline->cmds.size == 1 &&
                       pline->cmds.data[0].redirs.size == 0 &&
                       (!bg || sh_env.subsh != SUBSH_NONE);

    if (try_builtin) {
        sh_builtin *builtin = get_builtin(pline->cmds.data[0].argv[0]);

        if (builtin) {
            LOG_INFO("[%d] Started %s (no fork)", jid, builtin->name);

            int status = builtin->func(pline->cmds.data[0].argv, &sh_env);

            /* fg "replaces" itself with the existing job. Because we
             * don't queue fg's exit event we only reclaim the termianl
             * when the job that was fg'd exits (or stops) */
            if (strcmp("fg", builtin->name) != 0 || status == EXIT_FAILURE) {
                LOG_INFO("queued \"%s\" exit event", builtin->name);
                queue_builtin_exit(jid, pline->cmds.data[0].argv, status);
            } else {
                sh_env.fg_pending = jid;
            }

            handled = true;
        }
    }

    if (!handled) {
        pline_data pld = exec_pline(pline, bg);
        add_job(jid, &pld);

        LOG_INFO("[%d] Started %s %d", jid, pline->cmds.data[0].argv[0],
                pld.pgid);
    }

    return 0;
}

static void reclaim_terminal(void) {
    if (xtcsetpgrp(sh_env.tty_fd, getpgrp()) == -1)
        err_exit("tcsetpgrp");

    sh_env.fg_jid = NOFG;
}

static void handle_job_event(job_event *jev) {
    if (sh_env.fg_jid != jev->jid) {
        print_job_event(jev->jid, jev, false);
        input_on_new_line();
    } else if (jev->type == JEXITED) {
        reclaim_terminal();

        if (jev->exit_type == JEXIT_INTR)
            printf("\n");

        if (jev->signaled)
            print_job_event(jev->jid, jev, false);
    } else if (jev->type == JSTOPPED) {
        reclaim_terminal();

        jc_job *job = xjob_lookup(jev->jid);
        xtcgetattr(sh_env.tty_fd, &job->tio);
        job->tio_saved = true;

        xtcsetattr(sh_env.tty_fd, TCSANOW, &sh_env.tio);

        if (jev->intr_stop)
            printf("\n");

        print_job_event(jev->jid, jev, false);
    }
}

void flush_wait_events(void) {
    wait_event wev;
    while (get_wstat(&wev) != -1)
        update_job_proc(wev);
}

void flush_job_events(void) {
    job_event *jev;

    while ((jev = pop_job_event())) {
        if (sh_env.subsh == SUBSH_NONE)
            handle_job_event(jev);

        free_job_event(jev);
    }
}

void process_signals(void) {
    if (sigchld_caught) {
        LOG_INFO("SIGCHLD caught");
        sigchld_caught = false;

        flush_wait_events();

        if (sh_env.fg_jid == NOFG && sh_env.subsh == SUBSH_NONE)
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

void shell_wait(void) {
    sigsuspend(&sh_env.og_mask);
    if (errno != EINTR)
        err_exit("sigsuspend");

    process_signals();
}

static bool andor_should_run(ps_andor *andor, int status) {
    if (andor->op == PS_AND_IF && status != EXIT_SUCCESS)
        return false;

    if (andor->op == PS_OR_IF && status == EXIT_SUCCESS)
        return false;

    return true;
}

int shell_wait_pline(int jid, int *status) {
    job_event *jev;
    int rv;

    while (true) {
        sigsuspend(&sh_env.og_mask);
        process_signals();

        while ((jev = pop_job_event())) {
            if (sh_env.subsh == SUBSH_NONE)
                handle_job_event(jev);

            if (jev->jid == jid) {
                if (jev->type == JEXITED) {
                    if (jev->last->stat == PROC_EXIT) {
                        *status = jev->last->exit_stat;
                    } else if (jev->last->stat == PROC_SIG) {
                        *status = jev->last->term_sig + 128;
                    }

                    if (jev->exit_type == JEXIT_INTR) {
                        rv = PLINE_INTR;
                    } else {
                        rv = 0;
                    }

                    goto cleanup;
                }

                if (sh_env.subsh == SUBSH_NONE && jev->type == JSTOPPED) {
                    if (jev->last->stat == PROC_STOP) {
                        *status = jev->last->stop_sig + 128;
                    } else {
                        *status = WAIT_EXIT;
                    }

                    rv = 0;

                    goto cleanup;
                }
            }

            free_job_event(jev);
            jev = nullptr;
        }
    }

cleanup:
    free_job_event(jev);
    flush_job_events();
    return rv;
}

int shell_run_andor(ps_ast *ast) {
    int status = EXIT_SUCCESS;

    for (size_t i = 0; i < ast->andors.size; ++i) {
        ps_andor *andor = &ast->andors.data[i];

        if (andor_should_run(andor, status)) {
            sh_env.fg_jid = create_job_id();

            if (shell_run_pline(&andor->pline, ast->bg, sh_env.fg_jid) == -1) {
                status = EXIT_FAILURE;
                sh_env.fg_jid = NOFG;
                continue;
            }

            int rv = shell_wait_pline(sh_env.fg_jid, &status);
            update_status_variable(status);

            sh_env.fg_jid = NOFG;

            if (rv == PLINE_INTR)
                break;
        }
    }

    return status;
}

int shell_fork(void) {
    int cpid = xfork();
    int rv = setpgid(cpid, cpid);

    if (cpid != 0) {
        if (rv == -1 && errno != EACCES)
            err_exit("setpgid");

        return cpid;
    }

    sh_env.subsh |= SUBSH_ASYNC;

    clear_job_events();
    clear_job_table();

    sig_restore(&sh_env.og_mask);

    sig_block(SIGCHLD);
    sig_set_handler(SIGCHLD, sigchld_handler);

    return 0;
}

int shell_run(const char *cmd) {
    ps_ast ast = {0};
    char *line = xstrdup(cmd);
    int status = -1;

    if (line_to_ast(line, &ast) == -1)
        goto cleanup;

    if (!ast.bg) {
        status = shell_run_andor(&ast);
        goto cleanup;
    }

    int jid = create_job_id();

    if (ast.andors.size == 1) {
        if (shell_run_pline(&ast.andors.data[0].pline, true, jid) == -1)
            goto cleanup;
    } else {
        int subsh_pgid = shell_fork();
        if (subsh_pgid == 0)
            _exit(shell_run_andor(&ast));

        pline_data pld = { .pgid = subsh_pgid };

        *linc_array_push(&pld.procs) = (pline_proc) {
            .argv = copy_argv((char *[]){ line, NULL }),
            .pid = subsh_pgid
        };

        add_job(jid, &pld);
    }

    status = EXIT_SUCCESS;

    pid_t pgrp = lookup_job_pgrp(jid);
    printf("[%d] %d\n", jid, pgrp);

cleanup:
    ps_free(&ast);
    free(line);
    return status;
}
