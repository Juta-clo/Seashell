#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "linc/array.h"
#include "linc/string.h"
#include "linc/xsys.h"

#include "state.h"
#include "utils.h"

#define BUF_SIZE 1024

/* error functions */

static void output_err(const char *fmt, va_list *va, bool print_err) {
    char user_msg[BUF_SIZE] = "";
    char err_str[BUF_SIZE] = "";

    int n = vsnprintf(user_msg, BUF_SIZE, fmt, *va);
    if (n < 0)
        return;

    if (print_err) {
        strncat(err_str, strerror(errno), BUF_SIZE);
        fprintf(stderr, "sea: %s: %s\n", user_msg, err_str);
    } else {
        fprintf(stderr, "sea: %s\n", user_msg);
    }
}

void fatal_imp(const char *fmt, ...) {
    if (fmt) {
        va_list va;

        va_start(va, fmt);
        output_err(fmt, &va, false);
        va_end(va);
    }

    if (env.subsh) {
        _exit(EXIT_FAILURE);
    } else {
        exit(EXIT_FAILURE);
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

    fatal_imp(nullptr);
}

void err_msg(const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    output_err(fmt, &va, false);
    va_end(va);
}

/* general */

char *string_combine_n(const char *s1, const char *s2, size_t n,
        linc_allo *allo) {
    linc_string out = LINC_STRING_INIT;

    if (allo)
        out.allo = *allo;

    linc_string_append(&out, s1);
    linc_string_append_n(&out, s2, n);

    if (out.cap == 0) {
        if (allo) {
            return strdup_alloc("", allo);
        } else {
            return xstrdup("");
        }
    }

    return out.cstr;
}

char *string_combine(const char *s1, const char *s2, linc_allo *allo) {
    linc_string out = LINC_STRING_INIT;

    if (allo)
        out.allo = *allo;

    linc_string_appendf(&out, "%s%s", s1, s2);

    if (out.cap == 0) {
        if (allo) {
            return strdup_alloc("", allo);
        } else {
            return xstrdup("");
        }
    }

    return out.cstr;
}

void trim_newlines(linc_string *data) {
    if (data->len == 0)
        return;

    for (size_t i = data->len - 1; data->cstr[i] == '\n'; --i) {
        data->cstr[i] = '\0';
        --data->len;

        if (i == 0)
            break;
    }
}

/* Block endlessly until EOF. All poll errors are fatal
 * including EAGAIN and EINTR. */

void read_to_eof(int fd, linc_string *data) {
    *data = LINC_STRING_INIT;
    char read_buf[4096] = "";

    struct pollfd pollfd = { .fd = fd, .events = POLLIN };
    int wakeup = POLLHUP | POLLIN | POLLERR | POLLNVAL;

    while (true) {
        xpoll(&pollfd, 1, -1);

        if (pollfd.revents & wakeup) {
            if (pollfd.revents & POLLNVAL)
                fatal("poll: invalid file descriptor");

            ssize_t num_read = read(fd, read_buf, sizeof(read_buf));
            if (num_read == -1)
                err_exit("read");

            linc_string_append_n(data, read_buf, num_read);

            if (num_read == 0)
                break;
        }
    }
}

int fd_dup(int fd, bool cloexec) {
    int type = (cloexec) ? F_DUPFD_CLOEXEC : F_DUPFD;

    int newfd = fcntl(fd, type, STATE_FD_PRIVATE_BASE);
    if (newfd == -1)
        err_exit("fcntl");

    return newfd;
}

int fd_move(int fd1, int fd2, bool cloexec, bool replace) {
    if (fd1 == fd2) {
        if (cloexec) {
            int flags = fcntl(fd1, F_GETFD);
            if (flags == -1)
                err_exit("fcntl");

            if (cloexec) {
                flags |= FD_CLOEXEC;
            } else {
                flags &= ~FD_CLOEXEC;
            }

            if (fcntl(fd1, F_SETFD, flags) == -1)
                err_exit("fcntl");
        }

        return fd1;
    }

    int newfd;

    if (replace) {
        int flags = (cloexec) ? O_CLOEXEC : 0;

        if (dup3(fd1, fd2, flags) == -1)
            err_exit("dup3");

        newfd = fd2;
    } else {
        int type = (cloexec) ? F_DUPFD_CLOEXEC : F_DUPFD;

        newfd = fcntl(fd1, type, fd2);
        if (newfd == -1)
            err_exit("fcntl");
    }

    xclose(fd1);
    return newfd;
}

int get_int(const char *arg, int *num) {
    ASSERT(arg && num);

    if (arg[0] == '\0')
        return -1;

    int saved_errno = errno;
    char *end;
    long out;

    errno = 0;
    out = strtol(arg, &end, 10);

    if (*end != '\0')
        goto fail;

    if (errno == ERANGE)
        goto fail;

    if (out > INT_MAX || out < INT_MIN)
        goto fail;

    errno = saved_errno;
    *num = out;
    return 0;

fail:
    errno = saved_errno;
    return -1;
}

bool is_bin(const char *path) {
    if (is_dir(path))
        return false;

    return (access(path, X_OK) == 0);
}

bool is_file(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
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

char *strndup_alloc(const char *str, size_t n, linc_allo *allo) {
    ASSERT(str && allo);

    char *out = allo->alloc(n + 1, allo->ctx);
    memcpy(out, str, n);
    out[n] = '\0';

    return out;
}
