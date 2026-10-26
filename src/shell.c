#include <errno.h>
#include <signal.h>

#include "event.h"
#include "input.h"
#include "job_state.h"
#include "linc/xsys.h"
#include "parser.h"
#include "plan.h"
#include "state.h"
#include "utils.h"

static const char *shell_get_prompt(void) {
    return "> ";
}

static ps_ast *line_to_ast(char *line) {
    if (strcmp("", line) == 0) {
        return NULL;
    }

    da_tok toks;
    int rv = lx_tokenize(line, &toks);

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

void shell_run(void) {
    char *line;
    ps_ast *ast;

    while (true) {
        if (sh_env.fg_jid != NOFG) {
            sigsuspend(&sh_env.og_mask);
            if (errno != EINTR)
                err_exit("sigsuspend");

            process_signals();

            continue;
        } else if (!(line = input_get_line(shell_get_prompt()))) {
            xfatal("input_get_line: EOF/Error");
        }

        if (!(ast = line_to_ast(line))) {
            free(line);
            continue;
        }

        free(line);

        int jid = create_job_id();
        job_plan *plan = add_plan(jid, ast);

        if (launch_job(&plan_next(plan)->pline, ast->bg, jid) == -1) {
            plan_done(jid);
            continue;
        }

        if (ast->bg)
            printf("[%d] %d\n", jid , lookup_job_pgrp(jid));
    }
}
