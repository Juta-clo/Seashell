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

void close_proc_subs(ps_cmd *cmd) {
    for (size_t word_i = 0; word_i < cmd->words.size; ++word_i) {
        ps_word *word = &cmd->words.data[word_i];

        for (size_t fd_i = 0; fd_i < word->proc_sub_fds.size; ++fd_i)
            xclose(word->proc_sub_fds.data[fd_i]);

        word->proc_sub_fds = (da_int){0};
    }

    for (size_t redir_i = 0; redir_i < cmd->redirs.size; ++redir_i) {
        ps_word *word = &cmd->redirs.data[redir_i].target;

        for (size_t fd_i = 0; fd_i < word->proc_sub_fds.size; ++fd_i)
            xclose(word->proc_sub_fds.data[fd_i]);

        word->proc_sub_fds = (da_int){0};
    }
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

static void split_words(char *data, size_t n, da_charp *result,
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

static int expand_seg(ps_segment *seg, da_charp *parts, ps_word *word,
        bool *has_glob) {
    int status = -1;
    linc_allo allo = word->segments.allo;
    da_ex_tok toks = {0};
    wait_event wev = {0};
    int pid = 0;
    linc_string data = LINC_STRING_INIT;

    if (parts->size == 0)
        *linc_array_push(parts) = "";

    char **cur = &parts->data[parts->size - 1];

    if (ex_tokenize(seg->raw, &toks, true, has_glob) == -1)
        goto cleanup;

    for (size_t i = 0; i < toks.size; ++i) {
        ex_tok *tok = &toks.data[i];
        if (tok->type == EXT_VARIABLE) {
            sh_var *var = lookup_var_n(&env.vars, tok->str, tok->str_len);
            if (var) {
                text_has_glob(var->value, strlen(var->value), has_glob);
                *cur = string_combine(*cur, var->value, &allo);
            }
        } else if (tok->type == EXT_REGULAR) {
            *cur = string_combine_n(*cur, tok->str, tok->str_len, &allo);
        } else if (tok->type == EXT_CMD_SUB) {
            int rfd = shell_run_capture(&tok->str[2], tok->str_len - 2, &pid);
            if (rfd == -1)
                goto cleanup;

            read_to_eof(rfd, &data);
            trim_newlines(&data);

            if (seg->quote == PS_Q_NONE) {
                da_charp split_res = {0};

                split_words(data.cstr, data.len, &split_res, has_glob);

                for (size_t i = 0; i < split_res.size; ++i) {
                    *cur = string_combine(*cur, split_res.data[i], &allo);

                    if (i != split_res.size - 1) {
                        cur = linc_array_push(parts);
                        *cur = "";
                    }
                }

                argv_free_free(&split_res);
            } else {
                *cur = string_combine(*cur, data.cstr, &allo);
            }

            linc_string_free(&data);
            xclose(rfd);

            get_wstat(&wev, pid, 0);
            if (wev.type == PSIGNALED && wev.term_sig == SIGINT) {
                sig_consume(SIGINT);
                goto cleanup;
            }
        } else if (tok->type == EXT_PROC_SUB) {
            int rfd = shell_run_capture(&tok->str[2], tok->str_len - 2, NULL);
            if (rfd == -1)
                goto cleanup;

            linc_string path = LINC_STRING_INIT;
            linc_string_appendf(&path, "/dev/fd/%d", rfd);
            *cur = string_combine(*cur, path.cstr, &allo);
            linc_string_free(&path);

            *linc_array_push(&word->proc_sub_fds) = rfd;
        } else {
            fatal("unexpected ex_tok type");
        }
    }

    status = 0;

cleanup:
    linc_array_free(&toks);
    return status;
}

int get_word_parts(ps_word *word, da_charp *parts, bool *has_glob) {
    ASSERT(word);

    linc_allo allo = word->segments.allo;
    *parts = (da_charp){ .allo = allo };

    /* initial variable expansion and concatenation */

    for (size_t i = 0; i < word->segments.size; ++i) {
        ps_segment *seg = &word->segments.data[i];

        if (seg->quote == PS_Q_SINGLE) {
            if (parts->size == 0)
                *linc_array_push(parts) = "";

            char **last_part = &parts->data[parts->size - 1];
            linc_string part = LINC_STRING_INIT;

            linc_string_appendf(&part, "%s%s", *last_part, seg->raw);
            *last_part = strdup_alloc(part.cstr, &allo);
            linc_string_free(&part);
        } else if (seg->quote == PS_Q_DOUBLE) {
            if (expand_seg(seg, parts, word, nullptr) == -1)
                return -1;
        } else if (seg->quote == PS_Q_NONE) {
            if (expand_seg(seg, parts, word, has_glob) == -1)
                return -1;
        }
    }

    return 0;
}

int expand_word(ps_word *word, da_charp *argv) {
    ASSERT(word);

    linc_allo allo = word->segments.allo;
    da_charp parts = {0};
    bool has_glob = false;

    if (get_word_parts(word, &parts, &has_glob) == -1)
        return -1;

    if (word->segments.size == 1 &&
        word->segments.data[0].quote == PS_Q_NONE &&
        parts.size == 1 && STREQ(parts.data[0], "")) {
        return 0;
    }

    /* globbing */

    for (size_t i = 0; i < parts.size; ++i) {
        char *part = parts.data[i];

        da_charp matches = {0};

        if (has_glob) {
            glob_pattern(part, strlen(part), &matches);

            for (size_t i = 0; i < matches.size; ++i)
                *linc_array_push(argv) = strdup_alloc(matches.data[i], &allo);
        }

        if (matches.size == 0)
            *linc_array_push(argv) = part;

        argv_free_free(&matches);
    }

    return 0;
}

int expand_target(ps_word *word) {
    ASSERT(word);

    linc_allo allo = word->segments.allo;
    da_charp parts = {0};
    bool has_glob = false;

    if (get_word_parts(word, &parts, &has_glob) == -1)
        return -1;

    if (word->segments.size == 1 &&
        word->segments.data[0].quote == PS_Q_NONE &&
        parts.size == 1 && STREQ(parts.data[0], "")) {
        err_msg("ambiguous redirect");
        return -1;
    }

    if (parts.size > 1) {
        err_msg("ambiguous redirect");
        return -1;
    }

    /* globbing */

    da_charp matches = {0};

    if (has_glob) {
        glob_pattern(parts.data[0], strlen(parts.data[0]), &matches);

        if (matches.size > 1) {
            err_msg("ambiguous redirect");
            argv_free_free(&matches);
            return -1;
        }

        if (matches.size == 1)
            word->arg = strdup_alloc(matches.data[0], &allo);
    }

    if (!has_glob || matches.size == 0)
        word->arg = strdup_alloc(parts.data[0], &allo);

    argv_free_free(&matches);

    return 0;
}

int expand_cmd(ps_cmd *cmd) {
    if (cmd->kind == PS_CMD_SIMPLE) {
        da_charp argv = { .allo = cmd->words.allo };

        for (size_t i = 0; i < cmd->words.size; ++i)
            if (expand_word(&cmd->words.data[i], &argv) == -1)
                return -1;

        *linc_array_push(&argv) = nullptr;
        cmd->argv = argv.data;
        cmd->argc = argv.size - 1;
    }

    for (size_t i = 0;  i < cmd->redirs.size; ++i) {
        ps_redir *redir = &cmd->redirs.data[i];
        if (redir->fd_dup)
            continue;

        if (expand_target(&redir->target) == -1)
            return -1;
    }

    return 0;
}
