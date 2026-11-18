#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void err_exit(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

int main(void) {
    ssize_t capacity = fcntl(STDOUT_FILENO, F_GETPIPE_SZ);
    if (capacity == -1)
        err_exit("fcntl");

    char *buf = malloc(capacity + 1);
    if (!buf)
        err_exit("malloc");

    ssize_t total = capacity + 1;
    memset(buf, 'x', total);

    ssize_t sent = 0;

    while (sent != capacity + 1) {
        ssize_t n = write(STDOUT_FILENO, buf, total - sent);
        if (n == -1)
            err_exit("write");

        sent += n;
    }
}
