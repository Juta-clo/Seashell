#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <libgen.h>

#include "parser.h"
#include "expander.h"
#include "path_glob.h"
#include "utils.h"
#include "variable.h"
#include "shell_state.h"
#include "darr.h"

#include <linc/xsys.h>

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

static void push_key(da_ex_tok *toks, const char *start, size_t n) {
    ex_tok *tok = da_push(toks);
    tok->type = EXT_KEY;
    tok->str = start;
    tok->str_len = n;
}

static void  ex_tokenize(const char *s, da_ex_tok *toks, bool *has_glob) {
    *toks = (da_ex_tok){0};

    ex_tok_type type = EXT_REG;
    const char *c = s;
    const char *start = s;

    for (; *c != '\0'; ++c) {
        if (*c == '\\') {
            try_push_tok(toks, start, c, type);
            type = EXT_REG;
            start = c + 1;

            ++c;

            /* sanity check. not really necessary, lexer ensures
             * segment can not end with \ */
            if (*c == '\0')
                xfatal("bad segment contents");

            continue;
        }

        if (has_glob && type == EXT_REG && *c == '*')
            *has_glob = true;

        if (*c == '~') {
            try_push_tok(toks, start, c, type);
            push_key(toks, "HOME", sizeof("HOME") - 1);
            type = EXT_REG;
            start = c + 1;
        }

        if (*c == ' ' || *c == '/' || *c == '.') {
        /* as a convenience, you can achieve the same by just
         * escaping the character after the variable name */
            if (type != EXT_KEY)
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

static char *expand_seg(const char *seg, bool *has_glob) {
    dstr out;
    dstr_init(&out);

    da_ex_tok toks;
    ex_tokenize(seg, &toks, has_glob);

    for (size_t i = 0; i < toks.size; ++i) {
        ex_tok *tok = &toks.data[i];
        if (tok->type == EXT_KEY) {
            sh_var *var = lookup_var_n(&sh_env.vars, tok->str, tok->str_len);
            if (var)
                dstrcat(&out, var->value);

        } else if (tok->type == EXT_REG) {
            dstrncat(&out, tok->str, tok->str_len);

        } else
            xfatal("unexpected ex_tok type");
    }

    da_free(&toks);
    return out.c_str;
}

static int expand_word(ps_word *word, da_charp *argv, bool redir) {
    assert(redir || argv);

    int rv = -1;

    dstr arg;
    dstr_init(&arg);

    bool has_glob = false;

    /* initial variable expansion and concatenation */

    for (size_t i = 0; i < word->segments.size; ++i) {
        ps_segment *seg = &word->segments.data[i];
        char *exp_seg;

        if (seg->quote == PS_Q_SINGLE) {
            exp_seg = xstrdup(seg->raw);
            dstrcat(&arg, exp_seg);

        } else if (seg->quote == PS_Q_NONE) {
            exp_seg = expand_seg(seg->raw, &has_glob);
            dstrcat(&arg, exp_seg);

        } else {
            exp_seg = expand_seg(seg->raw, NULL);
            dstrcat(&arg, exp_seg);
        }

        free(exp_seg);
    }

    /* globbing */

    if (has_glob) {
        da_charp glob_result;

        if (glob_pattern(arg.c_str, arg.len, &glob_result) == -1)
            goto cleanup;

        if (glob_result.size == 0) {
            err_msg("no matches found: %s", arg.c_str);
            goto cleanup;
        }

        if (redir) {
            if (glob_result.size > 1) {
                err_msg("ambiguous redirect");
                free_charp_arr(&glob_result);
                goto cleanup;

            } else {
                word->arg = xstrdup(glob_result.data[0]);
                free_charp_arr(&glob_result);
            }

        } else if (!redir) {
            word->arg = xstrdup(arg.c_str);
            for (size_t i = 0; i < glob_result.size; ++i)
                *da_push(argv) = glob_result.data[i];

            da_free(&glob_result);
        }

    } else {
        word->arg = xstrdup(arg.c_str);

        if (!redir)
            *da_push(argv) = xstrdup(arg.c_str);
    }

    rv = 0;

cleanup:
    dstr_free(&arg);
    return rv;
}

static int create_argv(ps_cmd *cmd) {
    da_charp argv = {0};

    for (size_t i = 0; i < cmd->words.size; ++i)
        if (expand_word(&cmd->words.data[i], &argv, false) == -1)
            goto fail;

    *da_push(&argv) = NULL;
    cmd->argv = argv.data;

    return 0;

fail:
    free_charp_arr(&argv);
    return -1;
}

int ex_expand_pline(const ps_pline *pline) {
    for (size_t j = 0; j < pline->cmds.size; ++j) {
        ps_cmd *cmd = &pline->cmds.data[j];

        if (create_argv(cmd) == -1)
            return -1;

        for (size_t k = 0; k < cmd->redirs.size; ++k) {
            ps_redir *redir = &cmd->redirs.data[k];
            if (expand_word(&redir->target, NULL, true) == -1)
                return -1;
        }
    }

    return 0;
}
