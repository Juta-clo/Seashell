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
#include "state.h"
#include "utils.h"
#include "shell.h"

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
    tok->type = EXT_VARIABLE;
    tok->str = start;
    tok->str_len = n;
}

static int ex_tokenize(const char *s, da_ex_tok *toks, bool *has_glob,
        bool print_err) {
    *toks = (da_ex_tok){0};

    ex_tok_type type = EXT_REGULAR;
    const char *c = s;
    const char *start = s;

    for (size_t i = 0; *c != '\0'; ++c, ++i) {
        if (*c == '\\') {
            try_push_tok(toks, start, c, type);
            type = EXT_REGULAR;
            start = c + 1;

            ++c;
            ++i;

            if (*c == '\0')
                break;

            continue;
        }

        if (has_glob && type == EXT_REGULAR && *c == '*')
            *has_glob = true;

        if (*c == '~') {
            try_push_tok(toks, start, c, type);
            push_key(toks, "HOME", sizeof("HOME") - 1);
            type = EXT_REGULAR;
            start = c + 1;
        }

        /* as a convenience, you can achieve the same by just
         * escaping the character after the variable name */
        if (*c == ' ' || *c == '/' || *c == '.' || *c == '*') {
            if (type != EXT_VARIABLE)
                continue;

            try_push_tok(toks, start, c, type);
            type = EXT_REGULAR;
            start = c;
        }

        if (*c == '$') {
            try_push_tok(toks, start, c, type);

            if (c[1] == '(') {
                int rp = lx_get_rparen(&c[1], strlen(&c[1]));
                if (rp == -1) {
                    if (print_err)
                        err_msg("expand error finding matching ')'");
                    linc_array_free(toks);
                    return -1;
                }

                start = c + 2;
                c += rp + 1;

                try_push_tok(toks, start, c, EXT_CMD_SUB);
                type = EXT_REGULAR;
                start = c + 1;
            } else {
                type = EXT_VARIABLE;
                start = c + 1;
            }
        }
    }

    try_push_tok(toks, start, c, type);
    return 0;
}

static int expand_seg(ps_segment *seg, bool *has_glob, linc_string *out,
        int type) {
    da_ex_tok toks = {0};
    int print_err = (type != EXPAND_COMP);

    if (ex_tokenize(seg->raw, &toks, has_glob, print_err) == -1)
        return -1;

    for (size_t i = 0; i < toks.size; ++i) {
        ex_tok *tok = &toks.data[i];
        if (tok->type == EXT_VARIABLE) {
            sh_var *var = lookup_var_n(&env.vars, tok->str, tok->str_len);
            if (var)
                linc_string_append(out, var->value);
        } else if (tok->type == EXT_REGULAR) {
            linc_string_append_n(out, tok->str, tok->str_len);
        } else if (tok->type == EXT_CMD_SUB) {
            if (type != EXPAND_COMP && type != EXPAND_BUILTIN) {
                ps_ast ast = {0};
                LOG_INFO("parsing cmd sub: '%.*s'", (int)tok->str_len, tok->str);

                if (line_to_ast(tok->str, tok->str_len, &ast) == -1) {
                    linc_array_free(&toks);
                    return -1;
                }

                char *capture = shell_run_capture(&ast);
                LOG_INFO("cmd_sub capture: '%s'", capture);

                linc_string_append(out, capture);
                ps_free(&ast);
            } else {
                linc_string_append_n(out, tok->str, tok->str_len);
            }
        } else {
            fatal("unexpected ex_tok type");
        }
    }

    linc_array_free(&toks);
    return 0;
}

int expand_word(ps_word *word, da_charp *argv, int type) {
    ASSERT(word);

    linc_allo allo = word->segments.allo;
    linc_string arg = LINC_STRING_INIT;
    arg.allo = allo;
    bool has_glob = false;

    /* initial variable expansion and concatenation */

    for (size_t i = 0; i < word->segments.size; ++i) {
        linc_string exp_seg = LINC_STRING_INIT;
        ps_segment *seg = &word->segments.data[i];

        if (seg->quote == PS_Q_SINGLE) {
            linc_string_append(&arg, seg->raw);
        } else if (seg->quote == PS_Q_NONE) {
            if (expand_seg(seg, &has_glob, &exp_seg, type) == -1)
                return -1;

            linc_string_append(&arg, exp_seg.cstr);
        } else {
            if (expand_seg(seg, nullptr, &exp_seg, type) == -1)
                return -1;

            linc_string_append(&arg, exp_seg.cstr);
        }

        linc_string_free(&exp_seg);
    }

    if (type != EXPAND_COMP)
        LOG_INFO("expanded word: '%s'", arg.cstr);

    /* globbing */

    if (has_glob && type != EXPAND_COMP) {
        da_charp matches;

        if (glob_pattern(arg.cstr, arg.len, &matches) == -1)
            return -1;

        if (matches.size == 0) {
            err_msg("no matches found: %s", arg.cstr);
            return -1;
        }

        if (type == EXPAND_REDIR) {
            if (matches.size > 1) {
                err_msg("ambiguous redirect");
                argv_free_free(&matches);
                return -1;
            } else if (matches.size == 1) {
                word->arg = strdup_alloc(matches.data[0], &allo);
                argv_free_free(&matches);
            }

        } else {
            word->arg = strdup_alloc(arg.cstr, &allo);

            for (size_t i = 0; i < matches.size; ++i)
                *linc_array_push(argv) = strdup_alloc(matches.data[i], &allo);

            argv_free_free(&matches);
        }
    } else {
        word->arg = strdup_alloc(arg.cstr, &allo);

        if (type == EXPAND_CMD || type == EXPAND_BUILTIN)
            *linc_array_push(argv) = word->arg;
    }

    return 0;
}

int expand_cmd(ps_cmd *cmd, int type) {
    if (cmd->kind == PS_CMD_SIMPLE) {
        da_charp argv = { .allo = cmd->words.allo };

        for (size_t i = 0; i < cmd->words.size; ++i)
            if (expand_word(&cmd->words.data[i], &argv, type) == -1)
                return -1;

        *linc_array_push(&argv) = nullptr;
        cmd->argv = argv.data;
    }

    for (size_t k = 0; k < cmd->redirs.size; ++k) {
        ps_redir *redir = &cmd->redirs.data[k];

        if (!redir->fd_dup)
            if (expand_word(&redir->target, nullptr, EXPAND_REDIR) == -1)
                return -1;
    }

    return 0;
}
