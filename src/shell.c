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

#define PROMPT_DEFAULT "> "
#define PROMPT_LUA "\x1b[36mlua\x1b[m \x1b[35m>\x1b[m "
#define IN_BG 1
#define IN_FG 2

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
            pld.pgid = cpid;

        if (cpid == 0) {
            sh_env.subshell |= SUBSH_PIPE;

            /* subshell set up */

            xsetpgid(0, pld.pgid);

            if (!bg && first)
                xtcsetpgrp(sh_env.tty_fd, getpgrp());

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

        if (!bg && first)
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

da_plan plans = {0};

static pid_t create_job_id(void) {
    pid_t jid = 1;

    for (size_t i = 0; i < plans.size;) {
        if (jid == plans.data[i].jid) {
            ++jid;
            i = 0;
            continue;
        }

        ++i;
    }

    return jid;
}

ps_andor *plan_next(job_plan *plan) {
    if (plan->index >= plan->ast->andors.size)
        return NULL;

    return &plan->ast->andors.data[plan->index++];
}

job_plan *add_plan(ps_ast *ast) {
    job_plan *plan = linc_array_push(&plans);
    *plan = (job_plan){0};

    plan->jid = create_job_id();
    plan->ast = ast;

    LOG_INFO("added plan jid=%d", plan->jid);

    return plan;
}

void remove_plan(size_t index) {
    LOG_INFO("removed plan jid=%d", plans.data[index].jid);

    for (size_t i = 0; i < plans.data[index].exit_events.size; ++i)
        free_job_event(&plans.data[index].exit_events.data[i]);
    linc_array_free(&plans.data[index].exit_events);
    linc_array_free(&plans.data[index].exit_event_indexes);

    ps_free(plans.data[index].ast);
    free(plans.data[index].ast);

    linc_array_delete(&plans, index, 1);
}

void plan_done(pid_t jid) {
    size_t plan_i;
    job_plan *plan = lookup_plan(jid, &plan_i);
    if (!plan)
        return;

    remove_plan(plan_i);
}

job_plan *lookup_plan(pid_t jid, size_t *index) {
    for (size_t i = 0; i < plans.size; ++i) {
        if (jid == plans.data[i].jid) {
            if (index)
                *index = i;
            return &plans.data[i];
        }
    }

    return NULL;
}

int launch_job(ps_pline *pline, bool bg, pid_t jid) {
    if (expand_pline(pline) == -1)
        return -1;

    if (!bg) {
        sh_env.fg_jid = jid;

        xtcgetattr(sh_env.tty_fd, &sh_env.tty_state);

        job_plan *plan = lookup_plan(jid, NULL);
        ASSERT(plan);

        if (plan->tty_state_saved) {
            xtcsetattr(sh_env.tty_fd, TCSANOW, &plan->tty_state);
            plan->tty_state_saved = false;
        }
    }

    bool handled = false;
    if (pline->cmds.size == 1 && pline->cmds.data[0].redirs.size == 0 && !bg) {
        sh_builtin *builtin = get_builtin(pline->cmds.data[0].argv[0]);

        if (builtin) {
            LOG_INFO("[%d] Started %s (no fork)", jid, builtin->name);

            int status = builtin->func(pline->cmds.data[0].argv, &sh_env);

            /* fg "replaces" itself with the existing job. Because we
             * don't queue fg's exit event we only reclaim the termianl
             * when the job that was fg'd exits (or stops) */
            if (strcmp("fg", builtin->name) != 0 || status == EXIT_FAILURE) {
                LOG_INFO("queued \"%s\" exit event", builtin->name);
                queue_builtin_exit_event(jid, pline->cmds.data[0].argv, status);
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

static bool try_run_next_job(pid_t jid, bool success, int how) {
    bool bg = (how == IN_BG);
    bool execed_pline = false;

    /* THIS POINTER COULD BE INVALIDATED ON LAUNCH_JOB FROM LUA */
    job_plan *plan = lookup_plan(jid, NULL);
    if (!plan)
        return false;

    ps_andor *andor;
    while ((andor = plan_next(plan))) {
        if (success && andor->op == PS_OR_IF)
            continue;

        if (!success && andor->op == PS_AND_IF)
            continue;

        if (launch_job(&andor->pline, bg, plan->jid) == -1)
            return false;

        *linc_array_push(&plan->exit_event_indexes) = plan->index - 1;
        execed_pline = true;
        break;
    }

    return execed_pline;
}

static void reclaim_terminal(void) {
    if (xtcsetpgrp(sh_env.tty_fd, getpgrp()) == -1)
        err_exit("tcsetpgrp");

    sh_env.fg_jid = NOFG;
}

static bool fg_event(job_event *jev) {
    return sh_env.fg_jid  == jev->jid;
}

static void handle_fg_event(job_event *jev) {
    int jid = jev->jid;
    bool success = jev->success;

    if (jev->type == JEXITED) {
        job_plan *plan = lookup_plan(jev->jid, NULL);
        *linc_array_push(&plan->exit_events) = *jev;

        update_status_variable(&sh_env.vars, jev);

        if (jev->exit_type == JEXIT_INTR) {
            reclaim_terminal();
            printf("\n");

            if (jev->signaled)
                print_job_event(jid, NULL, false);
        } else {
            if (jev->signaled)
                print_job_event(jid, NULL, false);

            if (try_run_next_job(jid, success, IN_FG))
                return;

            reclaim_terminal();
        }

        plan_done(jid);

    } else if (jev->type == JSTOPPED) {
        if (xtcsetpgrp(sh_env.tty_fd, getpgrp()) == -1)
            err_exit("tcsetpgrp");
        sh_env.fg_jid = NOFG;

        job_plan *plan = lookup_plan(jid, NULL);
        xtcgetattr(sh_env.tty_fd, &plan->tty_state);
        plan->tty_state_saved = true;

        xtcsetattr(sh_env.tty_fd, TCSANOW, &sh_env.tty_state);

        if (jev->intr_stop)
            printf("\n");

        print_job_event(jid, jev, false);
        free_job_event(jev);
    }

    if (sh_env.fg_pending) {
        int pending_jid = sh_env.fg_pending;
        sh_env.fg_pending = false;

        if (try_run_next_job(pending_jid, success, IN_FG)) {
            LOG_INFO("continued 'fg' plan (still pending=%d)",
                    sh_env.fg_pending);
            return;
        }

        LOG_INFO("'fg' plan done");
        plan_done(pending_jid);
    }
}

static void handle_bg_event(job_event *jev) {
    if (jev->type != JEXITED) {
        print_job_event(jev->jid, jev, false);
        free_job_event(jev);
        input_on_new_line();

        return;
    }

    job_plan *plan = lookup_plan(jev->jid, NULL);
    *linc_array_push(&plan->exit_events)  = *jev;

    if (jev->exit_type == JEXIT_NORMAL &&
        try_run_next_job(jev->jid, jev->success, IN_BG))
        return;

    print_job_event(jev->jid, NULL, false);
    plan_done(jev->jid);
    input_on_new_line();

    return;
}

void process_signals(void) {
    if (sigchld_caught) {
        LOG_INFO("SIGCHLD caught");

        wait_event wev;
        while (get_wstat(&wev) != -1)
            if (update_job_proc(wev) == -1)
                xfatal("update_job_table");

        job_event *jev;
        while ((jev = pop_job_event())) {
            if (fg_event(jev)) {
                handle_fg_event(jev);
            } else
                handle_bg_event(jev);
        }
    }

    if (sigint_caught) {
        LOG_INFO("SIGINT caught");
        printf("^C\n");
        input_reset_line_state();
    }

    if (sighup_caught) {
        LOG_INFO("SIGHUP caught");
        exit(EXIT_FAILURE);
    }

    reset_sig_flags();
}

static ps_ast *line_to_ast(const char *line) {
    if (strcmp("", line) == 0)
        return NULL;

    da_tok toks;
    int rv = lx_tokenize(line, &toks);
    if (rv != LX_OK) {
        err_msg(lx_errstr(rv));
        return NULL;
    }

    ps_ast *ast = xmalloc(sizeof(ps_ast));
    if (ps_parse(&toks, ast) == -1) {
        free(ast);
        lx_free(&toks);
        return NULL;
    }

    lx_free(&toks);
    return ast;
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

void shell_sleep(void) {
    sigsuspend(&sh_env.og_mask);
    if (errno != EINTR)
        err_exit("sigsuspend");

    process_signals();
}

int shell_run(const char *line) {
    ps_ast *ast = line_to_ast(line);
    if (!ast)
        return -1;

    job_plan *plan = add_plan(ast);

    if (launch_job(&plan_next(plan)->pline, ast->bg, plan->jid) == -1) {
        plan_done(plan->jid);
        return -1;
    }

    if (ast->bg) {
        printf("[%d] %d\n", plan->jid , lookup_job_pgrp(plan->jid));
        return 0;
    }

    while (sh_env.fg_jid != NOFG)
        shell_sleep();

    return get_last_status();
}
