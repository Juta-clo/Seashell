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
#include "sig_funcs.h"
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

int ex_tokenize(const char *s, da_ex_tok *toks, bool perr, bool *has_glob) {
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

        if (has_glob && *c == '*')
            *has_glob = true;

        if (*c == '~') {
            try_push_tok(toks, start, c, type);
            push_key(toks, "HOME", sizeof("HOME") - 1);
            type = EXT_REGULAR;
            start = c + 1;
        }

        if (*c == ' ' || *c == '/' || *c == '.' || *c == '*') {
            if (type != EXT_VARIABLE)
                continue;

            try_push_tok(toks, start, c, type);
            type = EXT_REGULAR;
            start = c;
        }

        if (*c == '<' || *c == '$') {
            if (c[1] == '(') {
                try_push_tok(toks, start, c, type);

                start = c;

                int rp = lx_get_rparen(&c[1], strlen(&c[1]));
                if (rp == -1) {
                    if (perr)
                        err_msg("expand error finding matching ')'");

                    c = &s[strlen(s) - 1];
                    try_push_tok(toks, start, c, EXT_ERROR);
                    return -1;
                }

                type = (*c == '<') ? EXT_PROC_SUB : EXT_CMD_SUB;
                c += rp + 1;

                try_push_tok(toks, start, c, type);
                type = EXT_REGULAR;
                start = c + 1;
            } else if (*c == '$') {
                try_push_tok(toks, start, c, type);
                type = EXT_VARIABLE;
                start = c + 1;
            }
        }
    }

    try_push_tok(toks, start, c, type);
    return 0;
}

static void split_words(const char *data, size_t n, da_charp *result,
        bool *has_glob) {
    linc_string word = LINC_STRING_INIT;
    bool escape_active = false;

    for (size_t i = 0; i < n; ++i) {
        if (data[i] == '\n' || data[i] == ' ') {
            if (word.len > 0)
                *linc_array_push(result) = word.cstr;

            word = LINC_STRING_INIT;
        } else if (data[i] != '\0') {
            if (!escape_active) {
                if (has_glob && data[i] == '*')
                    *has_glob = true;

                if (data[i] == '\\') {
                    escape_active = true;
                    continue;
                }
            } else {
                escape_active = false;
            }

            linc_string_push(&word, data[i]);
        }
    }

    if (word.len > 0)
        *linc_array_push(result) = word.cstr;

    /* This is bad preformance */
    LOG_INFO("split '%s' into %zu parts", data, result->size);
}

static void expand_seg_input(ps_segment *seg, linc_string *out) {
    da_ex_tok toks = {0};

    ex_tokenize(seg->raw, &toks, false, nullptr);

    for (size_t i = 0; i < toks.size; ++i) {
        ex_tok *tok = &toks.data[i];
        if (tok->type == EXT_ERROR) {
            linc_string_append_n(out, tok->str, tok->str_len);
        } else if (tok->type == EXT_VARIABLE) {
            sh_var *var = lookup_var_n(&env.vars, tok->str, tok->str_len);
            if (var)
                linc_string_append(out, var->value);
        } else if (tok->type == EXT_REGULAR) {
            linc_string_append_n(out, tok->str, tok->str_len);
        } else {
            linc_string_append_n(out, tok->str, tok->str_len);
        }
    }

    linc_array_free(&toks);
}

void expand_word_input(ps_word *word) {
    ASSERT(word);

    linc_allo allo = word->segments.allo;
    linc_string arg = { .cstr = "", .allo = allo };

    /* initial variable expansion and concatenation */

    for (size_t i = 0; i < word->segments.size; ++i) {
        linc_string exp_seg = LINC_STRING_INIT;
        ps_segment *seg = &word->segments.data[i];

        if (seg->quote == PS_Q_SINGLE) {
            linc_string_append(&arg, seg->raw);
        } else {
            expand_seg_input(seg, &exp_seg);
            linc_string_append(&arg, exp_seg.cstr);
        }

        linc_string_free(&exp_seg);
    }

    word->arg = strdup_alloc(arg.cstr, &allo);
}

void text_has_glob(char *text, size_t n, bool *has_glob) {
    if (!has_glob)
        return;

    bool escape_active = false;

    for (size_t i = 0; i < n; ++i) {
        if (text[i] != '\0') {
            if (!escape_active) {
                if (text[i] == '*') {
                    *has_glob = true;
                    return;
                }

                if (text[i] == '\\') {
                    escape_active = true;
                    continue;
                }
            } else {
                escape_active = false;
            }
        }
    }
}

static void add_part(da_charp *parts, const char *new, size_t n, bool append) {
    if (!append || parts->size == 0) {
        *linc_array_push(parts) = xstrndup(new, n);
    } else {
        char **part = &parts->data[parts->size - 1];
        char *tmp = string_combine_n(*part, new, n, nullptr);
        free(*part);
        *part = tmp;
    }
}

static int expand_seg(ps_segment *seg, da_charp *parts, bool *has_glob,
        ex_res *res) {
    int status = -1;
    da_ex_tok toks = {0};
    wait_event wev = {0};
    int pid = 0;
    linc_string data = LINC_STRING_INIT;

    if (parts->size == 0)
        add_part(parts, "", 0, false);

    if (ex_tokenize(seg->raw, &toks, true, has_glob) == -1)
        goto cleanup;

    for (size_t i = 0; i < toks.size; ++i) {
        ex_tok *tok = &toks.data[i];
        if (tok->type == EXT_VARIABLE) {
            sh_var *var = lookup_var_n(&env.vars, tok->str, tok->str_len);
            if (var) {
                text_has_glob(var->value, strlen(var->value), has_glob);
                add_part(parts, var->value, strlen(var->value), true);
            }
        } else if (tok->type == EXT_REGULAR) {
            add_part(parts, tok->str, tok->str_len, true);
        } else if (tok->type == EXT_CMD_SUB) {
            ps_ast *ast = ast_acquire(&tok->str[2], tok->str_len - 2);
            if (!ast)
                goto cleanup;

            int rfd = shell_run_capture(ast, &pid);
            read_to_eof(rfd, &data);
            trim_newlines(&data);

            if (seg->quote == PS_Q_NONE) {
                da_charp split_res = {0};

                split_words(data.cstr, data.len, &split_res, has_glob);

                for (size_t i = 0; i < split_res.size; ++i) {
                    add_part(
                        parts,
                        split_res.data[i],
                        strlen(split_res.data[i]),
                        (i == 0) ? true : false
                    );
                }

                argv_free_free(&split_res);
            } else {
                add_part(parts, data.cstr, data.len, true);
            }

            linc_string_free(&data);
            xclose(rfd);

            get_wstat(&wev, pid, 0);
            if (wev.type == PSIGNALED && wev.term_sig == SIGINT) {
                sig_consume(SIGINT);
                goto cleanup;
            }
        } else if (tok->type == EXT_PROC_SUB) {
            ps_ast *ast = ast_acquire(&tok->str[2], tok->str_len - 2);
            if (!ast)
                goto cleanup;

            int rfd = shell_run_capture(ast, NULL);
            linc_string path = LINC_STRING_INIT;

            linc_string_appendf(&path, "/dev/fd/%d", rfd);
            add_part(parts, path.cstr, path.len, true);
            linc_string_free(&path);

            *linc_array_push(&res->proc_sub_fds) = rfd;
        } else {
            fatal("unexpected ex_tok type");
        }
    }

    status = 0;

cleanup:
    linc_array_free(&toks);
    return status;
}

int get_word_parts(ps_word *word, da_charp *parts, bool *has_glob, ex_res *res) {
    ASSERT(word);

    *parts = (da_charp){0};

    /* initial variable expansion and concatenation */

    for (size_t i = 0; i < word->segments.size; ++i) {
        ps_segment *seg = &word->segments.data[i];

        if (seg->quote == PS_Q_SINGLE) {
            add_part(parts, seg->raw, strlen(seg->raw), true);
        } else if (seg->quote == PS_Q_DOUBLE) {
            if (expand_seg(seg, parts, nullptr, res) == -1)
                goto fail;
        } else if (seg->quote == PS_Q_NONE) {
            if (expand_seg(seg, parts, has_glob, res) == -1)
                goto fail;
        }
    }

    return 0;

fail:
    argv_free_free(parts);
    return -1;
}

int expand_word(ps_word *word, da_charp *args, ex_res *res) {
    ASSERT(word);

    da_charp parts = {0};
    bool has_glob = false;

    if (get_word_parts(word, &parts, &has_glob, res) == -1)
        return -1;

    if (word->segments.size == 1 &&
        word->segments.data[0].quote == PS_Q_NONE &&
        parts.size == 1 && STREQ(parts.data[0], ""))
    {
        argv_free_free(&parts);
        return 0;
    }

    for (size_t i = 0; i < parts.size; ++i) {
        da_charp hits = {0};
        char *part = parts.data[i];

        if (has_glob)
            glob_pattern(part, strlen(part), &hits);

        for (size_t y = 0; y < hits.size; ++y)
            *linc_array_push(args) = xstrdup(hits.data[y]);

        if (hits.size == 0)
            *linc_array_push(args) = xstrdup(part);

        argv_free_free(&hits);
    }

    argv_free_free(&parts);
    return 0;
}

int expand_target(ps_word *word, ex_redir *redir, ex_res *res) {
    ASSERT(word);

    int rv = -1;
    bool has_glob = false;

    da_charp parts = {0};
    da_charp hits = {0};

    if (get_word_parts(word, &parts, &has_glob, res) == -1)
        return -1;

    if (word->segments.size == 1 &&
        word->segments.data[0].quote == PS_Q_NONE &&
        parts.size == 1 && STREQ(parts.data[0], ""))
    {
        err_msg("ambiguous redirect");
        goto cleanup;
    }

    if (parts.size > 1) {
        err_msg("ambiguous redirect");
        goto cleanup;
    }

    /* globbing */

    if (has_glob) {
        glob_pattern(parts.data[0], strlen(parts.data[0]), &hits);

        if (hits.size > 1) {
            err_msg("ambiguous redirect");
            goto cleanup;
        }
    }

    redir->target = xstrdup((hits.size == 1) ? hits.data[0] : parts.data[0]);

    rv = 0;

cleanup:
    argv_free_free(&parts);
    argv_free_free(&hits);
    return rv;
}

int expand_cmd(ps_cmd *cmd, ex_res *res) {
    *res = (ex_res){ .kind = cmd->kind };

    if (cmd->kind == PS_CMD_SIMPLE) {
        for (size_t i = 0; i < cmd->simple.words.size; ++i)
            if (expand_word(&cmd->simple.words.data[i], &res->argv, res) == -1)
                goto fail;

        *linc_array_push(&res->argv) = nullptr;
        res->argc = res->argv.size - 1;
    } else if (cmd->kind == PS_CMD_FOR) {
        res->ind = xstrdup(cmd->for_st.ind.segments.data[0].raw);

        for (size_t i = 0; i < cmd->for_st.vals.size; ++i)
            if (expand_word(&cmd->for_st.vals.data[i], &res->vals, res) == -1)
                goto fail;
    }

    for (size_t i = 0;  i < cmd->redirs.size; ++i) {
        ps_redir *spec = &cmd->redirs.data[i];
        ex_redir *redir = linc_array_push(&res->redirs);
        *redir = (ex_redir){
            .io_num = spec->io_num,
            .append = spec->append,
            .rdr_in = spec->rdr_in,
            .fd_dup = spec->fd_dup
        };

        if (spec->fd_dup) {
            redir->fd_target = spec->fd_target;
        } else if (expand_target(&spec->target, redir, res) == -1) {
            goto fail;
        }
    }

    return 0;

fail:
    expand_free(res);
    return -1;
}

void expand_free(ex_res *res) {
    for (size_t i = 0; i < res->redirs.size; ++i)
        if (!res->redirs.data[i].fd_dup)
            free(res->redirs.data[i].target);

    for (size_t i = 0; i < res->proc_sub_fds.size; ++i)
        xclose(res->proc_sub_fds.data[i]);

    linc_array_free(&res->proc_sub_fds);
    linc_array_free(&res->redirs);

    if (res->kind == PS_CMD_SIMPLE)
        argv_free_free(&res->argv);

    if (res->kind == PS_CMD_FOR) {
        free(res->ind);
        argv_free_free(&res->vals);
    }

    *res = (ex_res){0};
}
