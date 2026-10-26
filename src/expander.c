#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <libgen.h>

#include <linc/array.h>
#include <linc/string.h>
#include <linc/xsys.h>

#include "parser.h"
#include "expander.h"
#include "path_glob.h"
#include "utils.h"
#include "variable.h"
#include "shell.h"
#include "utils.h"

static bool try_push_tok(da_ex_tok *toks, const char *start, const char *c,
        ex_tok_type type) {
    if (c == start)
        return false;

    ex_tok *tok = linc_array_push(toks);
    tok->type = type;
    tok->str = start;
    tok->str_len = c - start;

    return true;
}

static void push_key(da_ex_tok *toks, const char *start, size_t n) {
    ex_tok *tok = linc_array_push(toks);
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

static linc_string expand_seg(const char *seg, bool *has_glob) {
    linc_string out = LINC_STRING_INIT;

    da_ex_tok toks;
    ex_tokenize(seg, &toks, has_glob);

    for (size_t i = 0; i < toks.size; ++i) {
        ex_tok *tok = &toks.data[i];
        if (tok->type == EXT_KEY) {
            sh_var *var = lookup_var_n(&sh_env.vars, tok->str, tok->str_len);
            if (var)
                linc_string_append(&out, var->value);

        } else if (tok->type == EXT_REG) {
            linc_string_append_n(&out, tok->str, tok->str_len);

        } else
            xfatal("unexpected ex_tok type");
    }

    linc_array_free(&toks);
    return out;
}

static int expand_word(ps_word *word, da_charp *argv, bool redir) {
    assert(redir || argv);

    linc_allo allo = word->segments.allo;
    linc_string arg = LINC_STRING_INIT;
    arg.allo = allo;
    bool has_glob = false;

    /* initial variable expansion and concatenation */

    for (size_t i = 0; i < word->segments.size; ++i) {
        ps_segment *seg = &word->segments.data[i];
        linc_string exp_seg = LINC_STRING_INIT;

        if (seg->quote == PS_Q_SINGLE) {
            linc_string_append(&arg, seg->raw);

        } else if (seg->quote == PS_Q_NONE) {
            exp_seg = expand_seg(seg->raw, &has_glob);
            linc_string_append(&arg, exp_seg.cstr);

        } else {
            exp_seg = expand_seg(seg->raw, NULL);
            linc_string_append(&arg, exp_seg.cstr);
        }

        linc_string_free(&exp_seg);
    }

    /* globbing */

    if (has_glob) {
        da_charp matches;

        if (glob_pattern(arg.cstr, arg.len, &matches) == -1)
            return -1;

        if (matches.size == 0) {
            err_msg("no matches found: %s", arg.cstr);
            return -1;
        }

        if (redir) {
            if (matches.size > 1) {
                err_msg("ambiguous redirect");
                free_charp_arr(&matches);
                return -1;
            } else if (matches.size == 1) {
                word->arg = strdup_alloc(matches.data[0], &allo);
                free_charp_arr(&matches);
            }

        } else if (!redir) {
            word->arg = strdup_alloc(arg.cstr, &allo);

            for (size_t i = 0; i < matches.size; ++i)
                *linc_array_push(argv) = strdup_alloc(matches.data[i], &allo);

            free_charp_arr(&matches);
        }

    } else {
        word->arg = strdup_alloc(arg.cstr, &allo);

        if (!redir)
            *linc_array_push(argv) = word->arg;
    }

    return 0;
}

static int create_argv(ps_cmd *cmd) {
    da_charp argv = { .allo = cmd->words.allo };

    for (size_t i = 0; i < cmd->words.size; ++i)
        if (expand_word(&cmd->words.data[i], &argv, false) == -1)
            return -1;

    *linc_array_push(&argv) = NULL;
    cmd->argv = argv.data;

    return 0;
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
