#include <errno.h>

#include "event.h"
#include "job_state.h"
#include "linc/xsys.h"
#include "parser.h"
#include "plan.h"
#include "utils.h"
#include "config.h"
#include "variable.h"

#define PROMPT_DEFAULT "> "

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

    if (config_field_prompt(&prompt) == -1)
        linc_string_append(&prompt, PROMPT_DEFAULT);

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
