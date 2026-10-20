#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <poll.h>
#include <unistd.h>
#include <wait.h>
#include <getopt.h>

#include "linc_tools.h"
#include "builtins.h"
#include "exec_funcs.h"
#include "expander.h"
#include "parser.h"
#include "lexer.h"
#include "input.h"
#include "shell_state.h"
#include "utils.h"
#include "job_state.h"
#include "sig_funcs.h"
#include "plan.h"
#include "variable.h"

#define SIG_READY 1
#define STDIN_READY 2
#define IN_BG 1
#define IN_FG 2

static void seashell_shutdown(void) {
    input_deinit(&sh_env);

    for (size_t i = 0; i < get_jctl()->jobs.size; ++i) {
        jc_job *job = &get_jctl()->jobs.data[i];

        if (getpgrp() == job->pgrp.pgid || job->pgrp.pgid <= 1)
            xfatal("unexpected pgid %d", job->pgrp.pgid);

        if (kill(-job->pgrp.pgid, SIGHUP) == -1 && errno != ESRCH) {
            LOG_ERR("kill: %m");
            err_exit("kill");
        }

        if (kill(-job->pgrp.pgid, SIGCONT) == -1 && errno != ESRCH) {
            LOG_ERR("kill: %m");
            err_exit("kill");
        }
    }

    while (get_wstat(&(wait_event){0}) != -1)
        continue;

    clear_job_events();
    clear_job_table();

    LOG_INFO("seashell shutting down");
}

static ps_ast *line_to_ast(char *line) {
    if (strcmp("", line) == 0) {
        drop_line();
        return NULL;
    }

    da_tok toks;
    int rv = lx_tokenize(line, &toks);
    drop_line();

    if (rv != LX_OK) {
        err_msg(lx_errstr(rv));
        return NULL;
    }

    ps_ast *ast = xmalloc(sizeof(ps_ast));

    if (ps_parse(&toks, ast) == -1) {
        err_msg("syntax error");
        free(ast);
        lx_free(&toks);
        return NULL;
    }

    lx_free(&toks);
    return ast;
}

static int launch_job(ps_pline *pline, bool bg, pid_t jid) {
    if (ex_expand_pline(pline) == -1)
        return -1;

    if (!bg) {
        if (sh_env.fg_jid == NOFG)
            printty_stop(sh_env.pt);

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

        *da_push(&plan->exit_event_indexes) = plan->index - 1;
        execed_pline = true;
        break;
    }

    return execed_pline;
}

void reclaim_terminal(void) {
    if (xtcsetpgrp(sh_env.tty_fd, getpgrp()) == -1)
        err_exit("tcsetpgrp");

    printty_start(sh_env.pt);
    sh_env.fg_jid = NOFG;
    sh_env.need_prompt = true;
}

bool fg_event(job_event *jev) {
    return sh_env.fg_jid  == jev->jid;
}

static void handle_fg_event(job_event *jev) {
    int jid = jev->jid;
    bool success = jev->success;

    if (jev->type == JEXITED) {
        job_plan *plan = lookup_plan(jev->jid, NULL);
        *da_push(&plan->exit_events) = *jev;

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
        printty_start(sh_env.pt);

        if (jev->intr_stop)
            printf("\n");

        print_job_event(jid, jev, false);
        free_job_event(jev);
    }

    if (sh_env.fg_pending) {
        int pending_jid = sh_env.fg_pending;
        sh_env.fg_pending = false;

        if (try_run_next_job(pending_jid, success, IN_FG)) {
            LOG_INFO("continued 'fg' plan (still pending=%d)", sh_env.fg_pending);
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
        return;
    }

    job_plan *plan = lookup_plan(jev->jid, NULL);
    *da_push(&plan->exit_events)  = *jev;

    if (jev->exit_type == JEXIT_NORMAL &&
        try_run_next_job(jev->jid, jev->success, IN_BG))
        return;

    print_job_event(jev->jid, NULL, false);
    plan_done(jev->jid);
    return;
}

static void process_signals(void) {
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
        sh_env.need_prompt = true;
        printf("^C\n");
        drop_line();
        LOG_INFO("SIGINT caught");
    }

    if (sighup_caught) {
        LOG_INFO("SIGHUP caught");
        exit(EXIT_FAILURE);
    }

    reset_sig_flags();
}

int main(int argc, char *const *argv) {
    if (args_parse(argc, argv, opts, ARGS_NO_NON_OPTS) == -1)
        exit(EXIT_FAILURE);

    xatexit(seashell_shutdown);
    env_setup(&sh_env);

    while (true) {
        char *line;

        if (sh_env.fg_jid == NOFG) {
            line = get_line();
            if (!line) {
                process_signals();
                continue;
            }

        } else {
            sigsuspend(&sh_env.og_mask);
            if (errno != EINTR)
                err_exit("sigsuspend");

            process_signals();
            continue;
        }

        ps_ast *ast = line_to_ast(line);
        if (!ast) {
            sh_env.need_prompt = true;
            continue;
        }

        int jid = create_job_id();
        job_plan *plan = add_plan(jid, ast);

        if (launch_job(&plan_next(plan)->pline, ast->bg, jid) == -1) {
            plan_done(jid);
            sh_env.need_prompt = true;
            continue;
        }

        if (ast->bg) {
            pid_t pgid = lookup_job_pgrp(jid);
            printf("[%d] %d\n", jid , pgid);
            sh_env.need_prompt = true;
        }
    }

    return EXIT_SUCCESS;
}
