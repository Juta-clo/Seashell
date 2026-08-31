#include <stdio.h>
#include <string.h>

#include "parser.h" // IWYU pragma: keep - See 2026-06-25 Notes
#include "expander.h"
#include "darr.h"
#include "dstr.h"
#include "variable.h"
#include "xfuncs.h"
#include "shell_state.h"

static bool try_push_tok(da_ex_tok *toks, const char *start, const char *c,
        ex_tok_type type) {
    if (c == start)
        return false;

    ex_tok *tok = da_push(toks);
    tok->type = type;
    tok->str = start;
    tok->str_len = c - start;

    return true;
}

static void  ex_tokenize(const char *s, da_ex_tok *toks) {
    *toks = (da_ex_tok){0};

    ex_tok_type type = EXT_REG;
    const char *c = s;
    const char *start = s;

    for (; *c != '\0'; ++c) {
        if (*c == '\\') {
            ++c;
            continue;
        }

        if (*c == ' ' || *c == '/' || *c == '.') {
            if (type == EXT_REG)
                continue;

            try_push_tok(toks, start, c, type);
            type = EXT_REG;
            start = c;
        }

        if (*c == '$') {
            try_push_tok(toks, start, c, type);
            type = EXT_KEY;
            start = c + 1;
        }
    }

    try_push_tok(toks, start, c, type);
}

static char *expand_seg(const char *seg) {
    dstr out;
    dstr_init(&out);

    da_ex_tok toks;
    ex_tokenize(seg, &toks);

    for (size_t i = 0; i < toks.size; ++i) {
        ex_tok *tok = &toks.data[i];
        if (tok->type == EXT_KEY) {
            char key[VAR_KEY_SIZE];
            strncpy(key, tok->str, tok->str_len);
            key[tok->str_len] = '\0';

            sh_var *var = lookup_var(&sh_env.vars, key);
            if (var)
                dstrcat(&out, var->value);

        } else if (tok->type == EXT_REG) {
            dstrncat(&out, tok->str, tok->str_len);
        }
    }

    da_free(&toks);
    return out.c_str;
}

static int create_arg(ps_word *word) {
    dstr arg;
    dstr_init(&arg);

    for (size_t i = 0; i < word->segments.size; ++i) {
        ps_segment *seg = &word->segments.data[i];

        if (seg->quote == PS_Q_SINGLE) {
            dstrcat(&arg, xstrdup(seg->raw));
        } else {
            char *exp_seg = expand_seg(seg->raw);
            dstrcat(&arg, exp_seg);
            free(exp_seg);
        }
    }

    word->arg = arg.c_str;
    return 0;
}

static int create_argv(ps_cmd *cmd) {
    size_t argc = cmd->words.size;

    char **argv = xcalloc(argc + 1, sizeof(char *));

    for (size_t i = 0; i < argc; ++i)
        argv[i] = cmd->words.data[i].arg;

    argv[argc] = NULL;

    cmd->argv = argv;

    return 0;
}

int ex_expand(ps_ast *ast) {
    for (size_t i = 0; i < ast->andors.size; ++i) {
        ps_andor *andor = &ast->andors.data[i];
        ps_pline *pline = &andor->pline;

        for (size_t j = 0; j < pline->cmds.size; ++j) {
            ps_cmd *cmd = &pline->cmds.data[j];

            for (size_t k = 0; k < cmd->words.size; ++k) {
                ps_word *word = &cmd->words.data[k];
                if (create_arg(word) == -1)
                    return -1;
            }

            if (create_argv(cmd) == -1)
                return -1;

            for (size_t k = 0; k < cmd->redirs.size; ++k) {
                ps_redir *redir = &cmd->redirs.data[k];
                if (create_arg(&redir->target) == -1)
                    return -1;
            }
        }
    }

    return 0;
}
