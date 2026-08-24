#define _GNU_SOURCE

#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <poll.h>
#include <unistd.h>
#include <wait.h>
#include <getopt.h>

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

#define SIG_READY 1
#define STDIN_READY 2

static da_plan plans = {0};

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
        err_msg("expansion error");
        ps_free(ast);
        free(ast);
        lx_free(&toks);
        return NULL;
    }

    lx_free(&toks);
    return ast;
}

static void launch_job(ps_pline *pline, bool bg, pid_t jid) {
    bool handled = false;

    if (pline->cmds.size == 1 && !bg) {
        sh_builtin *builtin = get_builtin(pline->cmds.data[0].argv[0]);

        if (builtin) {
            LOG_INFO("[%d] started (builtin)", jid);

            int status = builtin->func(pline->cmds.data[0].argv, &sh_env);
            queue_builtin_exit_event(jid, status);

            handled = true;
        }
    }

    if (!handled) {
        LOG_INFO("[%d] started", jid);

        pline_data pld = exec_pline(pline, bg);
        add_job(jid, pld.pids, pld.pgid);

        free_pline_data(&pld);
    }
}

static ps_andor *pnxt(job_plan *plan) {
    return &plan->ast->andors.data[plan->index];
}

static void add_plan(pid_t jid, ps_ast *ast) {
    LOG_INFO("added plan jid=%d", jid);

    job_plan *plan = da_push(&plans);

    plan->jid = jid;
    plan->index = 1;
    plan->ast = ast;
}

static void remove_plan(size_t index) {
    LOG_INFO("removed plan jid=%d", plans.data[index].jid);

    ps_free(plans.data[index].ast);
    free(plans.data[index].ast);

    da_delete(&plans, index);
}

static job_plan *lookup_plan(pid_t jid, size_t *index) {
    for (size_t i = 0; i < plans.size; ++i) {
        if (jid == plans.data[i].jid) {
            *index = i;
            return &plans.data[i];
        }
    }

    return NULL;
}

static bool run_next_job_in_plan(job_event *jev, bool bg) {
    size_t plan_i;
    bool execed_pline = false;

    job_plan *plan = lookup_plan(jev->jid, &plan_i);
    if (!plan)
        return execed_pline;

    for (; plan->index < plan->ast->andors.size; ++plan->index) {

        if (jev->success && pnxt(plan)->op == PS_OR_IF)
            continue;

        if (!jev->success && pnxt(plan)->op == PS_AND_IF)
            continue;

        plan->jid = request_job_id(plan->jid);
        if (plan->jid == -1)
            xfatal("job id unexpectedly unavailable");

        launch_job(&pnxt(plan)->pline, bg, plan->jid);

        execed_pline = true;
        break;
    }

    if (++plan->index >= plan->ast->andors.size)
        remove_plan(plan_i);

    return execed_pline;
}

static bool run_next_job_fg(job_event *jev) {
    return run_next_job_in_plan(jev, false);
}

static bool run_next_job_bg(job_event *jev) {
    return run_next_job_in_plan(jev, true);
}

static void print_job_event(job_event *jev, bool *prompt_upset) {
    assert(jev && prompt_upset);

    if (!*prompt_upset) {
        printf("\n");
        *prompt_upset = true;
    }

    printf("%s\n", get_jev_str(*jev));
}

void reclaim_terminal(void) {
    if (xtcsetpgrp(sh_env.tty_fd, getpgrp()) == -1)
        err_exit("tcsetpgrp");

    sh_env.fg_jid = NOFG;
}

bool fg_event(job_event *jev) {
    return sh_env.fg_jid  == jev->jid;
}

static void process_signals(void) {
    bool need_prompt = false;
    bool need_terminal = false;
    bool prompt_upset = !shell_in_fg();

    if (sigchld_caught) {
        wait_event wev;
        while (get_wstat(&wev) != -1)
            if (update_job_proc(wev) == -1)
                xfatal("update_job_table");

        job_event *jev;
        while ((jev = pop_job_event())) {
            LOG_INFO("%s", get_jev_str(*jev));

            if (!fg_event(jev)) {
                if ((jev->type != JEXITED && jev->type != JTERMINATED)
                        || !run_next_job_bg(jev))
                    print_job_event(jev, &prompt_upset);

            } else if (jev->type == JEXITED) {
                if (!run_next_job_fg(jev))
                    need_terminal = true;

            } else if (jev->type == JTERMINATED) {
                if (jev->term_sig == SIGINT) {
                    printf("\n"); /* INT cancels entire andor chain */
                    need_terminal = true;

                } else {
                    print_job_event(jev, &prompt_upset);
                    if (!run_next_job_fg(jev))
                        need_terminal = true;
                }

            } else if (jev->type == JSTOPPED) {
                if (jev->stop_sig == SIGTSTP)
                    printf("\n");

                print_job_event(jev, &prompt_upset);
                need_terminal = true;
            }
        }
    }

    if (sigint_caught) {
        LOG_INFO("sigint caught");

        if (!prompt_upset) {
            printf("\n");
            prompt_upset = true;
        }
    }

    if (sighup_caught) {
        LOG_INFO("sighup caught");
        exit(EXIT_FAILURE);
    }

    if (need_terminal) {
        reclaim_terminal();
        need_prompt = true;
    }

    if (need_prompt || (shell_in_fg() && prompt_upset))
        display_prompt(PROMPT_SIMPLE);

    reset_sig_flags();
}

int main(int argc, char *const *argv) {
    if (arg_parse(argc, argv, opts, ARG_NO_NON_OPTS) == -1)
        exit(EXIT_FAILURE);

    env_setup(&sh_env);
    xatexit(hup_to_children);

    struct pollfd events = { .events = POLLIN, .fd = sh_env.tty_fd };

    display_prompt(PROMPT_SIMPLE);

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
            display_prompt(PROMPT_SIMPLE);
            continue;
        }

        if (ast->andors.size == 0)
            xfatal("parsed empty ast");

        ps_pline *first_pline = &ast->andors.data[0].pline;

        int jid = create_job_id();

        if (ast->bg) {
            printf("[%d] started\n", jid);
            display_prompt(PROMPT_SIMPLE);
        } else {
            sh_env.fg_jid = jid;
        }

        launch_job(first_pline, ast->bg, jid);

        if (ast->andors.size > 1) {
            add_plan(jid, ast);

        } else {
            ps_free(ast);
            free(ast);
        }
    }

    return EXIT_SUCCESS;
}
