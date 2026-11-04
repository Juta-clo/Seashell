#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "linc/array.h"
#include "linc/xsys.h"

#include "state.h"
#include "utils.h"

#define BUF_SIZE 1024

/* error functions */

static void output_err(const char *fmt, va_list *va, bool print_err) {
    char user_msg[BUF_SIZE] = "";
    char err_str[BUF_SIZE] = "";

    vsnprintf(user_msg, BUF_SIZE, fmt, *va);

    if (print_err) {
        strncat(err_str, strerror(errno), BUF_SIZE);
        fprintf(stderr, "seashell: %s: %s\n", user_msg, err_str);
    } else
        fprintf(stderr, "seashell: %s\n", user_msg);
}

void fatal_imp(const char *fmt, ...) {
    if (fmt) {
        va_list va;

        va_start(va, fmt);
        output_err(fmt, &va, false);
        va_end(va);
    }

    if (env.subsh == SUBSH_NONE) {
        exit(EXIT_FAILURE);
    } else {
        _exit(EXIT_FAILURE);
    }
}

void assert_bool_imp(linc_site site, ...) {
    va_list va;

    va_start(va, site);
    bool a = va_arg(va, int);
    char *ap = va_arg(va, char *);
    va_end(va);

    if (!a) {
        linc_log_write(
            env.assert_log, LINC_LOG_ERROR, site,
            "%s",
            ap
        );

        fatal_imp(nullptr);
    }
}

void err_exit_imp(const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    output_err(fmt, &va, true);
    va_end(va);

    fatal(nullptr);
}

void err_msg(const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    output_err(fmt, &va, false);
    va_end(va);
}

/* general */

int get_int(const char *arg, int *num) {
    ASSERT(num);

    if (arg[0] == '\0')
        return -1;

    char *endptr;
    *num = (int) strtol(&arg[0], &endptr, 10);

    if (*endptr != '\0')
        return -1;

    return 0;
}

bool is_dir(const char *path) {
    struct stat sb;
    if (stat(path, &sb) == -1)
        return false;

    return S_ISDIR(sb.st_mode);
}

void argv_free_free(da_charp *arr) {
    for (size_t i = 0; i < arr->size; ++i)
        free(arr->data[i]);

    linc_array_free(arr);

    *arr = (da_charp){0};
}

size_t argv_len(char **argv) {
    size_t len = 0;
    for (char **arg = argv; *arg != nullptr; ++arg)
        ++len;
    return len;
}

void argv_free(char **argv) {
    for (char **arg = argv; *arg != nullptr; ++arg)
        free(*arg);
    free(argv);
}

char **argv_copy(char **argv) {
    int argc = 0;
    for (char **arg = argv; *arg != nullptr; ++arg)
        ++argc;

    char **out = xcalloc(argc + 1, sizeof(char *));
    for (int i = 0; i < argc; ++i)
        out[i] = xstrdup(argv[i]);

    return out;
}

char *strdup_alloc(const char *str, linc_allo *allo) {
    ASSERT(str && allo);

    size_t n = strlen(str) + 1;
    char *out = allo->alloc(n, allo->ctx);
    memcpy(out, str, n);

    return out;
}
