#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <poll.h>
#include <unistd.h>
#include <wait.h>
#include <getopt.h>

#include <linc/xsys.h>
#include "parser.h"
#include "lexer.h"
#include "input.h"
#include "shell.h"
#include "utils.h"
#include "job_state.h"
#include "plan.h"
#include "event.h"

static ps_ast *line_to_ast(char *line) {
    if (strcmp("", line) == 0) {
        input_drop_line();
        return NULL;
    }

    da_tok toks;
    int rv = lx_tokenize(line, &toks);
    input_drop_line();

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

int main(int argc, char *const *argv) {
    env_setup(&sh_env, argc, argv);

    char *line;
    ps_ast *ast;

    while (true) {
        process_signals();

        if (sh_env.fg_jid != NOFG) {
            sigsuspend(&sh_env.og_mask);
            if (errno != EINTR)
                err_exit("sigsuspend");

            continue;
        } else {
            if (!(line = input_get_line()))
                continue;
        }

        if (!(ast = line_to_ast(line)))
            continue;

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
