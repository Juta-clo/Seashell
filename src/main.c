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
        err_msg("lex error");
        return NULL;
    }

    ps_ast *ast = xmalloc(sizeof(ps_ast));

    if (ps_parse(&toks, ast) == -1) {
        err_msg("syntax error");
        free(ast);
        lx_free(&toks);
        return NULL;
    }

    if (ex_expand(ast) == -1) {
        ps_free(ast);
        free(ast);
        lx_free(&toks);
        return NULL;
    }

    lx_free(&toks);
    return ast;
}

static void launch_job(ps_pline *pline, bool bg, pid_t jid) {
    if (!bg)
        xtcgetattr(sh_env.tty_fd, &sh_env.tty_state);

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
                queue_builtin_exit_event(jid, status);
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
}

static bool try_run_next_job(const job_event *jev, bool bg) {
    bool execed_pline = false;

    job_plan *plan = lookup_plan(jev->jid, NULL);
    if (!plan)
        return false;

    ps_andor *andor;
    while ((andor = plan_next(plan))) {
        if (jev->success && andor->op == PS_OR_IF)
            continue;

        if (!jev->success && andor->op == PS_AND_IF)
            continue;

        /* We can reuse the same job_id. When control
         * flow reaches arrives here we are directly
         * responding to that jobs exit and no other
         * job can be started during this window. */

        launch_job(&andor->pline, bg, plan->jid);
        execed_pline = true;
        break;
    }

    return execed_pline;
}

static char *get_jev_str(const job_event *jev) {
    dstr msg;
    dstr_init(&msg);

    if (jev->builtin) {
        dstr_printf(&msg, "[%d] Done (builtin)", jev->jid);

    } else if (jev->type == JEXITED) {
        dstr_printf(&msg, "[%d] ", jev->jid);
        int offset = msg.len;

        for (size_t i = 0; i < jev->pviews.size; ++i) {
            proc_view *pview = &jev->pviews.data[i];

            if (i != 0)
                dstr_printf(&msg, "%*s", offset, "");

            if (pview->stat == PROC_EXIT) {
                dstr_printf(&msg, "'%s' Exited (%d)",
                        pview->argv[0], pview->exit_stat);

            } else if (pview->stat == PROC_SIG) {
                dstr_printf(&msg, "'%s' %s (%d)", pview->argv[0],
                        strsignal(pview->term_sig), pview->term_sig);
            } else
                xfatal("unexpected process status");

            if (i != jev->pviews.size - 1)
                dstr_printf(&msg, "\n");
        }

    } else if (jev->type == JSTOPPED) {
        dstr_printf(&msg, "[%d] Stopped", jev->jid);

    } else if (jev->type == JCONTINUED) {
        dstr_printf(&msg, "[%d] Continued", jev->jid);
    }

    return msg.c_str;
}

void reclaim_terminal(void) {
    if (xtcsetpgrp(sh_env.tty_fd, getpgrp()) == -1)
        err_exit("tcsetpgrp");

    printty_start(sh_env.pt);
    sh_env.fg_jid = NOFG;
    sh_env.need_prompt = true;
}

bool shell_in_fg(void) {
    return sh_env.fg_jid == NOFG;
}

bool fg_event(job_event *jev) {
    return sh_env.fg_jid  == jev->jid;
}

void print_job_event(const job_event *jev) {
    sh_env.need_prompt = true;

    char *msg = get_jev_str(jev);
    LOG_INFO("%s", msg);

    if (shell_in_fg()) {
        display_job_msg(msg);
    } else
        printf("%s\n", msg);

    free(msg);
}

/* If a job is signaled, the andor chain is cancelled so
 * its plan is freed.
 *
 * on intr_int or intr_stop we print a newline so that the
 * next print will be after the echoed control character.
 *
 * If  foreground job exits normally we try to run the next
 * job in its plan (andor list). If it has one, then we do
 * nothing. The existing job keeps the foreground and we
 * simply go back to waiting. If there is no more jobs in
 * the plan we remove the plan, take back the termianl and
 * draw the prompt. */

static void handle_fg_event(const job_event *jev) {
    if (jev->type == JEXITED) {
        update_status_variable(&sh_env.vars, jev);

        if (jev->exit_type == JEXIT_INTR) {
            plan_done(jev->jid);
            reclaim_terminal();
            printf("\n");

            if (jev->signaled)
                print_job_event(jev);

        } else {
            if (jev->signaled)
                print_job_event(jev);

            if (try_run_next_job(jev, IN_FG))
                return;

            plan_done(jev->jid);
            reclaim_terminal();
        }

    } else if (jev->type == JSTOPPED) {
        /* restore foreground process group status so that
         * tcsetattr doesn't not return TTOU */
        if (xtcsetpgrp(sh_env.tty_fd, getpgrp()) == -1)
            err_exit("tcsetpgrp");
        sh_env.fg_jid = NOFG;

        /* save stopped programs termios */
        job_plan *plan = lookup_plan(jev->jid, NULL);
        xtcgetattr(sh_env.tty_fd, &plan->tty_state);
        plan->tty_state_saved = true;

        /* restore old original shell termios, then initialize
         * printty, so that it uses the old original shell termios */
        xtcsetattr(sh_env.tty_fd, TCSANOW, &sh_env.tty_state);
        printty_start(sh_env.pt);

        if (jev->intr_stop)
            printf("\n");

        print_job_event(jev);

    } else if (jev->type == JCONTINUED) {
      /* Don't print here? If a interactive terminal program was resumed
       * then printing will not be desired. We shouldn't print when we
       * arenot in the foreground (This case only triggers by fg).
       *
       * zsh handles this by lying. It prints the job continued msg
       * before even sending SIGCONT. Thats probably a decent
       * compromise. In any case I would never print here. Going to
       * do that for now. See builtins.c
       *
       * fg builtin applies the saved termios for the stopped fg
       * job because it needs to do it before resuming it. */
    }
}

static void handle_bg_event(job_event *jev) {
    if (jev->type != JEXITED) {
        print_job_event(jev);
        return;
    }

    if (jev->exit_type == JEXIT_NORMAL && try_run_next_job(jev, IN_BG)) {
        if (jev->signaled)
            print_job_event(jev);

        return;
    }

    plan_done(jev->jid);
    print_job_event(jev);
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

            free_job_event(jev);
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

        if (shell_in_fg()) {
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

        if (ast->andors.size == 0)
            xfatal("parsed empty ast");

        ps_pline *first_pline = &ast->andors.data[0].pline;

        int jid = create_job_id();

        if (!ast->bg) {
            sh_env.fg_jid = jid;
            printty_stop(sh_env.pt);
        } else {
            printf("[%d] Started\n", jid);
            sh_env.need_prompt = true;
        }

        launch_job(first_pline, ast->bg, jid);
        add_plan(jid, ast);
    }

    return EXIT_SUCCESS;
}
