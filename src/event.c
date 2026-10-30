#include "input.h"
#include "plan.h"
#include "state.h"
#include "sig_funcs.h"
#include "utils.h"
#include "variable.h"
#include "job_state.h"

#include <linc/xsys.h>
#include <linc/array.h>

#define IN_BG 1
#define IN_FG 2

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
