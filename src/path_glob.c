#include <errno.h>
#include <linc/xsys.h>
#include <linc/array.h>
#include "path_glob.h"
#include "utils.h"

/* qsort */
static int cmp_match(const void *p1, const void *p2) {
    return strcmp(*(char **)p1, *(char **)p2);
}

static void get_path_components(char *str, da_charp *comps) {
    char *tok = strtok(str, "/");
    if (!tok)
        xfatal("unexpected strtok return value");

    do {
        *linc_array_push(comps) = tok;
    } while ((tok = strtok(NULL, "/")));
}

static da_charp get_substrs(const char *comp, size_t *pre_n, size_t *suf_n,
        bool *exact)
{
    /* Return the prefix and suffix length and an ordered array
     * of substrings. Consecutive '*' characters are represented
     * by an empty c string. Make 'exact' true if there is no glob
     * character. */

    ASSERT(comp && pre_n && suf_n && exact);

    da_charp sub_strings = {0};
    *exact = false;

    size_t i = strcspn(comp, "*");
    *pre_n = i;

    /* no glob, exact match */
    if (comp[i] == '\0') {
        *pre_n = 0;
        *suf_n = 0;
        *exact = true;
        return sub_strings;
    }

    /* ends with glob, prefix match only */
    if (comp[++i] == '\0') {
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

        *linc_array_push(&sub_strings) = xstrndup(&comp[i - n], n);

        if (comp[++i] == '\0') {
            *suf_n = 0;
            return sub_strings;
        }
    }
}

/* for prefix_ok(), suffix_ok() and substrs_ok() if pre_n, suf_n,
 * or substrs.data[i].size == 0, they return true. */

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

static void get_dir_entries(bool inc_prev, bool dirs, da_charp *dents) {
    size_t og_dent_size = dents->size;

    for (size_t i = 0; i < og_dent_size; ++i) {
        char *path = dents->data[i];

        DIR *dir = xopendir(path);
        LOG_INFO("opened path: %s", path);

        while (true) {
            errno = 0;
            struct dirent *dirent = readdir(dir);
            if (!dirent) {
                if (errno != 0)
                    err_exit("readdir");
                break;
            }

            if (strcmp(dirent->d_name, ".")  == 0)
                continue;

            bool is_prev_dirent = (strcmp(dirent->d_name, "..") == 0);

            if (!inc_prev && is_prev_dirent)
                continue;

            if (dirs && !(dirent->d_type == DT_DIR || is_prev_dirent))
                continue;

            char **dent = linc_array_push(dents);
            *dent = xmalloc(strlen(path) + strlen(dirent->d_name) + 2);

            if (strcmp(path, "/") == 0)
                path[0] = '\0';

            strcpy(*dent, path);
            strcat(*dent, "/");
            strcat(*dent, dirent->d_name);
        }

        xclosedir(dir);
    }

    for (size_t i = 0; i < og_dent_size; ++i) {
        free(dents->data[0]);
        linc_array_delete(dents, 0);
    }

    LOG_INFO("dents updated; new size %ld", dents->size);
}

static void filter_dents(const char *glob, da_charp *dents) {
    bool exact;
    size_t pre_n, suf_n;
    da_charp substrs = get_substrs(glob, &pre_n, &suf_n, &exact);

    for (size_t i = 0; i < dents->size; ++i) {
        char *dent_cpy = xstrdup(dents->data[i]);
        char *base_name = basename(dent_cpy);

        if ((exact && strcmp(base_name, glob) != 0) ||
            !prefix_ok(base_name, glob, pre_n) ||
            !suffix_ok(base_name, glob, suf_n) ||
            !substrs_ok(base_name, &substrs, pre_n, suf_n))
        {
            free(dents->data[i]);
            linc_array_delete(dents, i--);
        }

        free(dent_cpy);
    }

    free_charp_arr(&substrs);

    LOG_INFO("filtered dents; new size %ld [%s]", dents->size, glob);
}

int glob_pattern(const char *glob, size_t len, da_charp *matches) {
    ASSERT(glob && matches);
    ASSERT_G_INT(len, 0);

    *matches = (da_charp){0};

    int rv = -1;
    bool slash_last = (glob[len - 1] == '/');
    da_charp comps = {0};
    da_charp dents = {0};
    char *glob_text = xstrndup(glob, len);

    get_path_components(glob_text, &comps);

    if (glob[0] == '/') {
        *linc_array_push(&dents) = xstrdup("/");

    } else {
        if (strcmp(".", comps.data[0]) == 0 ||
            strcmp("..", comps.data[0]) == 0)
        {
            *linc_array_push(&dents) = xstrdup(comps.data[0]);
            linc_array_delete(&comps, 0);

        } else {
            *linc_array_push(&dents) = xstrdup(".");
        }
    }

    for (size_t comp_i = 0; comp_i < comps.size; ++comp_i) {
        char *comp = comps.data[comp_i];
        bool last_comp = (comp_i == comps.size - 1);
        bool inc_prev = (strcmp("..", comp) == 0);

        get_dir_entries(inc_prev, !last_comp || slash_last, &dents);
        filter_dents(comp, &dents);

        if (dents.size == 0) {
            rv = 0;
            goto cleanup;
        }

        if (last_comp) {
            qsort(dents.data, dents.size, sizeof(char *), cmp_match);

            for (size_t dent_i = 0; dent_i < dents.size; ++dent_i)
                *linc_array_push(matches) = xstrdup(dents.data[dent_i]);
        }
    }

    rv = 0;

cleanup:
    free(glob_text);
    free_charp_arr(&dents);
    linc_array_free(&comps);

    return rv;
}
