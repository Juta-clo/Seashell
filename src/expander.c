#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <libgen.h>

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

    *arr = (da_charp){0};
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

static bool prefix_ok(const char *dent, const char *comp, size_t pre_n) {
    return (strncmp(dent, comp, pre_n) == 0);
}

static bool suffix_ok(const char *dent, const char *comp, size_t suf_n) {
    size_t comp_len = strlen(comp);
    size_t match_len = strlen(dent);

    return (strncmp(&dent[match_len - suf_n], &comp[comp_len - suf_n],
                suf_n) == 0);
}

static bool substrs_ok(const char *dent, da_charp *substrs, size_t pre_n,
        size_t suf_n) {
    assert(dent && substrs);

    if (substrs->size == 0)
        return true;

    char *trim = xstrndup(&dent[pre_n], strlen(&dent[pre_n]) - suf_n);
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

static void get_dir_entries(bool dirs, bool no_append, da_charp *dents) {
    LOG_INFO("get_dir_entries(dirs=%d, no_append=%d)", dirs, no_append);

    size_t og_dent_size = dents->size;

    for (size_t i = 0; i < og_dent_size; ++i) {
        const char *path = dents->data[i];

        DIR *dir = xopendir(path);
        LOG_INFO("opened path: %s", path);

        while (true) {
            errno = 0;
            struct dirent *dirent = readdir(dir);
            if (!dirent) {
                if (errno != 0)
                    err_exit("readir");
                break;
            }

            LOG_INFO("readdir found %s", dirent->d_name);

            if (strcmp(dirent->d_name, ".")  == 0 ||
                strcmp(dirent->d_name, "..") == 0)
                continue;

            if (dirs && dirent->d_type != DT_DIR)
                continue;

            char **dent = da_push(dents);

            if (no_append) {
                *dent = xstrdup(dirent->d_name);
            } else {
                *dent = xmalloc(strlen(path) + strlen(dirent->d_name) + 2);
                strcpy(*dent, path); /* +2 for '\0' and separating '/' */
                strcat(*dent, "/");
                strcat(*dent, dirent->d_name);
            }
        }

        xclosedir(dir);
    }

    for (size_t i = 0; i < og_dent_size; ++i) {
        free(dents->data[0]);
        da_delete(dents, 0);
    }

    LOG_INFO("dents updated:");
    for (size_t i = 0; i < dents->size; ++i)
        LOG_INFO("dents[%ld] = %s", i, dents->data[i]);
}

static void filter_dents(const char *comp, da_charp *dents) {
    size_t pre_n, suf_n;
    da_charp substrs = get_substrs(comp, &pre_n, &suf_n);

    for (size_t i = 0; i < dents->size; ++i) {
        char *dent_cpy = xstrdup(dents->data[i]);
        char *base_name = basename(dent_cpy);

        if (!prefix_ok(base_name, comp, pre_n) ||
            !suffix_ok(base_name, comp, suf_n) ||
            !substrs_ok(base_name, &substrs, pre_n, suf_n)
        ) {
            free(dents->data[i]);
            da_delete(dents, i--);
        }

        free(dent_cpy);
    }

    free_charp_array(&substrs);

    LOG_INFO("filtered dents; new size %ld [%s]:", dents->size, comp);
    for (size_t i = 0; i < dents->size; ++i)
        LOG_INFO("dents[%ld] = %s", i, dents->data[i]);
}

static int expand_globs(dstr *arg, da_charp *argv) {
    int rv = -1;
    bool slash_last = (arg->c_str[arg->size - 1] == '/');
    da_charp comps = {0};
    da_charp dents = {0};

    if (arg->c_str[0] == '/')
        *da_push(&dents) = xstrdup("/");

    char *glob_text = xstrdup(arg->c_str);
    get_path_components(glob_text, &comps);

    if (strcmp(".", comps.data[0]) == 0 || strcmp("..", comps.data[0]) == 0) {
        *da_push(&dents) = xstrdup(comps.data[0]);
        da_delete(&comps, 0); /* first component is really a path */
    }

    for (size_t comp_i = 0; comp_i < comps.size; ++comp_i) {
        char *comp = comps.data[comp_i];
        bool last_comp = (comp_i == comps.size - 1);

        get_dir_entries(!last_comp || slash_last, last_comp, &dents);
        filter_dents(comp, &dents);

        if (dents.size == 0) {
            err_msg("no matches found: %s", arg->c_str);
            goto cleanup;
        }

        if (last_comp) {
            for (size_t match_i = 0; match_i < dents.size; ++match_i)
                *da_push(argv) = xstrdup(dents.data[match_i]);
        }
    }

    rv = 0;

cleanup:
    free(glob_text);
    free_charp_array(&dents);
    da_free(&comps);

    return rv;
}

static int expand_word(ps_word *word, da_charp *argv, bool redir) {
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

    word->arg = xstrdup(arg.c_str);

    /* globbing */

    if (has_glob) {
        if (expand_globs(&arg, argv) == -1)
            goto cleanup;

        if (redir && argv->size > 1) {
            err_msg("ambiguous redirect");
            goto cleanup;
        }
    } else {
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
    free_charp_array(&argv);
    return -1;
}

int ex_expand(const ps_ast *ast) {
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
