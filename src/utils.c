#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "linc_tools.h"
#include "utils.h"
#include "shell_state.h"

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

void fatal(const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    output_err(fmt, &va, false);
    va_end(va);

    if (sh_env.subshell)
        _exit(EXIT_FAILURE);
    else
        exit(EXIT_FAILURE);
}

void err_exit(const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    output_err(fmt, &va, true);
    va_end(va);

    if (sh_env.subshell)
        _exit(EXIT_FAILURE);
    else
        exit(EXIT_FAILURE);
}

void err_msg(const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    output_err(fmt, &va, false);
    va_end(va);
}

void errno_msg(const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    output_err(fmt, &va, true);
    va_end(va);
}

void usage_err(const char *fmt, ...) {
    fprintf(stderr, "Usage: ");

    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);

    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}

/* general */

void free_argv(char **argv) {
    for (char **arg = argv; *arg != NULL; ++arg)
        free(*arg);
    free(argv);
}

char **copy_argv(char **argv) {
    int argc = 0;
    for (char **arg = argv; *arg != NULL; ++arg)
        ++argc;

    char **out = xcalloc(argc + 1, sizeof(char *));
    for (int i = 0; i < argc; ++i)
        out[i] = xstrdup(argv[i]);

    return out;
}
