#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "parser.h" // IWYU pragma: keep - See 2026-06-25 Notes
#include "expander.h"
#include "darr.h"
#include "dstr.h"
#include "utils.h"
#include "variable.h"
#include "xfuncs.h"
#include "shell_state.h"

static void free_charp_array(da_charp *arr) {
    for (size_t i = 0; i < arr->size; ++i)
        free(arr->data[i]);

    da_free(arr);
}

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

        if (has_glob && *c == '*')
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

static da_charp get_dir_entries(const char *path, bool dirs) {
    DIR *dir = xopendir(path);
    LOG_INFO("opendir(%s)", path);

    struct dirent *dirent;

    da_charp dents = {0};

    while (true) {
        errno = 0;
        dirent = readdir(dir);
        if (!dirent) {
            if (errno != 0)
                err_exit("readir");
            break;
        }

        if (strcmp(dirent->d_name, ".")  == 0 ||
            strcmp(dirent->d_name, "..") == 0)
            continue;

        if (dirs && dirent->d_type != DT_DIR)
            continue;

        *da_push(&dents) = xstrdup(dirent->d_name);
        LOG_INFO("dents[%ld] = %s", dents.size - 1, dirent->d_name);
    }

    xclosedir(dir);
    return dents;
}

static void get_path_components(char *str, da_charp *comps) {
    char *tok = strtok(str, "/");
    if (!tok)
        xfatal("unexpected strtok return value");

    do {
        *da_push(comps) = tok;
    } while ((tok = strtok(NULL, "/")));
}

static da_charp get_substrs(const char *comp, size_t *pre_n, size_t *suf_n) {
    /* Return the prefix and suffix length and an ordered array
     * of substrings. Consecutive '*' characters are represented
     * by an empty c string. */

    assert(pre_n && suf_n);

    da_charp sub_strings = {0};

    size_t i = strcspn(comp, "*");
    *pre_n = i;

    /* no glob in this component or ends with glob. no suffix
     * in either case. */
    if (comp[i] == '\0' || comp[++i] == '\0') {
        *suf_n = 0;
        return sub_strings;
    }

    while (true) {
        size_t n = strcspn(&comp[i], "*");
        i += n;

        if (comp[i] == '\0') {
            *suf_n = n;
            return sub_strings;
        }

        *da_push(&sub_strings) = xstrndup(&comp[i - n], n);

        if (comp[++i] == '\0') {
            *suf_n = 0;
            return sub_strings;
        }
    }
}

static bool prefix_ok(const char *match, const char *comp, size_t pre_n) {
    return (strncmp(match, comp, pre_n) == 0);
}

static bool suffix_ok(const char *match, const char *comp, size_t suf_n) {
    size_t comp_len = strlen(comp);
    size_t match_len = strlen(match);

    return (strncmp(&match[match_len - suf_n], &comp[comp_len - suf_n],
                suf_n) == 0);
}

static bool substrs_ok(const char *match, da_charp *substrs, size_t pre_n,
        size_t suf_n) {
    assert(match);

    char *trim = xstrndup(&match[pre_n], strlen(&match[pre_n]) - suf_n);
    char *start = trim;
    bool all_found = true;

    for (size_t i = 0; i < substrs->size; ++i) {
        if (substrs->data[i][0] == '\0')
            continue;

        char *needle = strstr(start, substrs->data[i]);

        if (!needle) {
            all_found = false;
            break;
        }

        start = needle + strlen(substrs->data[i]);
    }

    free(trim);
    return all_found;
}

static int expand_globs(dstr *arg, da_charp *argv) {
    dstr open_path;
    bool ends_with_slash = (arg->c_str[arg->size - 1] == '/');
    da_charp comps = {0};
    size_t pre_n;
    size_t suf_n;
    bool is_dir_comp;
    da_charp matches = {0};

    dstr_init(&open_path);
    if (arg->c_str[0] == '/') {
        dstrcat(&open_path, "/");
    } else
        dstrcat(&open_path, "./");

    /* returns at least 1 comp; c_str is modified */
    char *orig_arg = xstrdup(arg->c_str);
    get_path_components(arg->c_str, &comps);

    for (size_t comp_i = 0; comp_i < comps.size; ++comp_i) {
        char *comp = comps.data[comp_i];

        da_charp substrs = get_substrs(comp, &pre_n, &suf_n);
        is_dir_comp = (comp_i < comps.size - 1 || ends_with_slash);
        da_charp dents = get_dir_entries(open_path.c_str, is_dir_comp);

        for (size_t dent_i = 0; dent_i < dents.size; ++dent_i) {
            char *dent = dents.data[dent_i];

            if (!prefix_ok(dent, comp, pre_n))
                continue;

            if (!suffix_ok(dent, comp, suf_n))
                continue;

            if (!substrs_ok(dent, &substrs, pre_n, suf_n))
                continue;

            *da_push(&matches) = xstrdup(dent);
        }

        /* assumes its a open_path compenent for now */
        if (matches.size == 0) {
            err_msg("no matches found: %s", orig_arg);
            goto fail;
        }

        for (size_t match_i = 0; match_i < matches.size; ++match_i)
            *da_push(argv) = xstrdup(matches.data[match_i]);

        free_charp_array(&substrs);
        free_charp_array(&dents);
        free_charp_array(&matches);
    }

    free(orig_arg);
    dstr_free(&open_path);
    da_free(&comps);

    return 0;

fail:
    free(orig_arg);
    dstr_free(&open_path);
    da_free(&comps);

    return -1;
}

static int expand_word(ps_word *word, da_charp *argv, bool redir) {
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

    word->arg = xstrdup(arg.c_str);

    /* globbing */

    if (has_glob) {
        expand_globs(&arg, argv);

        if (redir && argv->size > 1) {
            err_msg("ambiguous redirect");
            goto fail;
        }
    } else {
        *da_push(argv) = xstrdup(arg.c_str);
    }

    dstr_free(&arg);
    return 0;

fail:
    dstr_free(&arg);
    free_charp_array(argv);
    return -1;
}

static int create_argv(ps_cmd *cmd) {
    da_charp argv = {0};

    for (size_t i = 0; i < cmd->words.size; ++i)
        if (expand_word(&cmd->words.data[i], &argv, false) == -1)
            return -1;

    *da_push(&argv) = NULL;
    cmd->argv = argv.data;

    return 0;
}

int ex_expand(ps_ast *ast) {
    for (size_t i = 0; i < ast->andors.size; ++i) {
        ps_andor *andor = &ast->andors.data[i];
        ps_pline *pline = &andor->pline;

        for (size_t j = 0; j < pline->cmds.size; ++j) {
            ps_cmd *cmd = &pline->cmds.data[j];

            if (create_argv(cmd) == -1)
                return -1;

            for (size_t k = 0; k < cmd->redirs.size; ++k) {
                ps_redir *redir = &cmd->redirs.data[k];
                if (expand_word(&redir->target, &(da_charp){0}, true) == -1)
                    return -1;
            }
        }
    }

    return 0;
}
