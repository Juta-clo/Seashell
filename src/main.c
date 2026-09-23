#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <poll.h>
#include <unistd.h>
#include <wait.h>
#include <getopt.h>

#include "dstr.h"
#include "xfuncs.h"
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
#include "args.h"
#include "plan.h"

#define SIG_READY 1
#define STDIN_READY 2

static void hup_to_children(void) {
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

static ps_ast *line_to_ast(void) {

    char *line = get_line();
    if (strlen(line) == 0)
        return NULL;

    da_tok toks;
    if (lx_tokenize(line, &toks) != LX_OK) {
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

    if (pline->cmds.size == 1 && !bg) {
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

static bool run_next_job_in_plan(const job_event *jev, bool bg) {
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

static bool run_next_job_fg(const job_event *jev) {
    return run_next_job_in_plan(jev, false);
}

static bool run_next_job_bg(job_event *jev) {
    return run_next_job_in_plan(jev, true);
}

static char *get_jev_str(const job_event *jev) {
    dstr msg;
    dstr_init(&msg);

    if (jev->builtin) {
        dstr_printf(&msg, "[%d] Done (builtin)", jev->jid);

    } else if (jev->type == JEXITED) {
        if (jev->signaled) {
            dstr_printf(&msg, "[%d] Done - %s by signal %d", jev->jid,
                    strsignal(jev->last->term_sig), jev->last->term_sig);
        } else {
            dstr_printf(&msg, "[%d] Done - Exited with status %d", jev->jid,
                    jev->last->exit_stat);
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

    sh_env.fg_jid = NOFG;
}

bool shell_in_fg(void) {
    return sh_env.fg_jid == NOFG;
}

bool fg_event(job_event *jev) {
    return sh_env.fg_jid  == jev->jid;
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

static void handle_fg_event(shell_state *sst, const job_event *jev) {
    if (jev->type == JEXITED) {
        if (jev->signaled || jev->intr_int) {
            plan_done(jev->jid);
            reclaim_terminal();
            sst->need_prompt = true;

            if (jev->signaled)
                sst->need_print = true;

            if (jev->intr_int)
                printf("\n");

        } else {
            if (!run_next_job_fg(jev)) {
                plan_done(jev->jid);
                reclaim_terminal();
                sst->need_prompt = true;
            }
        }

    } else if (jev->type == JSTOPPED) {
        reclaim_terminal();

        job_plan *plan = lookup_plan(jev->jid, NULL);
        xtcgetattr(sh_env.tty_fd, &plan->tty_state);
        xtcsetattr(sh_env.tty_fd, TCSANOW, &sh_env.tty_state);
        plan->tty_state_saved = true;

        sst->need_print = true;
        sst->need_prompt = true;

        if (jev->intr_stop)
            printf("\n");

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

static void handle_bg_event(shell_state *sst, job_event *jev) {
    if (jev->type == JEXITED) {
        if (jev->signaled) {
            plan_done(jev->jid);
            sst->need_print = true;

        } else {
            if (!run_next_job_bg(jev)) {
                plan_done(jev->jid);
                sst->need_print = true;
            }
        }

    } else
        sst->need_print = true;
}

static void process_signals(void) {
    shell_state sst = {
        .need_prompt = false,
        .need_print = false,
        .prompt_upset = !shell_in_fg()
    };

    if (sigchld_caught) {
        LOG_INFO("SIGCHLD caught");

        wait_event wev;
        while (get_wstat(&wev) != -1)
            if (update_job_proc(wev) == -1)
                xfatal("update_job_table");

        job_event *jev;
        while ((jev = pop_job_event())) {
            sst.need_print = false;

            if (fg_event(jev)) {
                handle_fg_event(&sst, jev);
            } else
                handle_bg_event(&sst, jev);

            char *msg = get_jev_str(jev);
            LOG_INFO("%s", msg);

            if (sst.need_print) {
                if (!sst.prompt_upset) {
                    printf("\n");
                    sst.prompt_upset = true;
                }

                printf("%s\n", msg);
            }

            free(msg);
            free_job_event(jev);
        }
    }

    if (sigint_caught) {
        LOG_INFO("SIGINT caught");

        if (!sst.prompt_upset) {
            printf("\n");
            sst.prompt_upset = true;
        }
    }

    if (sighup_caught) {
        LOG_INFO("SIGHUP caught");
        exit(EXIT_FAILURE);
    }

    if (sst.need_prompt || (shell_in_fg() && sst.prompt_upset))
        display_prompt(sh_env.prompt_style);

    reset_sig_flags();
}

int main(int argc, char *const *argv) {
    if (arg_parse(argc, argv, opts, ARG_NO_NON_OPTS) == -1)
        exit(EXIT_FAILURE);

    env_setup(&sh_env);
    xatexit(hup_to_children);

    struct pollfd events = { .events = POLLIN, .fd = sh_env.tty_fd };

    display_prompt(sh_env.prompt_style);

    while (true) {
        int nfds = shell_in_fg() ? 1 : 0;

        int ready = ppoll(&events, nfds, NULL, &sh_env.og_mask);

        if (ready == -1) {
            if (errno == EINTR) {
                process_signals();
                continue;
            } else {
                LOG_ERR("ppoll: %m");
                err_exit("ppoll");
            }
        }

        ps_ast *ast = line_to_ast();
        if (!ast) {
            display_prompt(sh_env.prompt_style);
            continue;
        }

        if (ast->andors.size == 0)
            xfatal("parsed empty ast");

        ps_pline *first_pline = &ast->andors.data[0].pline;

        int jid = create_job_id();

        if (ast->bg) {
            printf("[%d] Started\n", jid);
            display_prompt(sh_env.prompt_style);
        } else {
            sh_env.fg_jid = jid;
        }

        launch_job(first_pline, ast->bg, jid);
        add_plan(jid, ast);
    }

    return EXIT_SUCCESS;
}
