#include <linux/limits.h>

#include <wait.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

__attribute__ ((format(printf, 1, 2)))
static void err_exit(const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);

    fprintf(stderr, "\n");
    _exit(EXIT_FAILURE);
}

__attribute__ ((format(printf, 1, 2)))
static void errExit(const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);

    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}

/* I have a sway (wm) bind to launch this program with foot (tty). That
 * gives seashell a fresh sessoin and default termios structure. */

int main(void) {
    int cpid = fork();
    if (cpid == -1)
        errExit("fork: %m");

    const char *home = getenv("HOME");
    if (!home)
        err_exit("getenv: HOME not defined");

    char seashell[PATH_MAX];

    if (snprintf(seashell, PATH_MAX - 1, "%s/Projects/Seashell", home) == -1)
        err_exit("snprintf");

    if (chdir(seashell) == -1)
        err_exit("chdir(%s)", seashell);

    if (cpid == 0) {
        if (setenv("XMAKE_COLORTERM", "color8", 1) == -1)
            err_exit("setenv");

        execlp("xmake", "xmake", (char *)NULL);
        err_exit("execlp");
    }

    const char *path = getenv("PATH");
    const char *add = "test/pty/test_bins";

    char new_path[PATH_MAX];
    snprintf(new_path, PATH_MAX - 1, "%s:%s/%s", (path) ? path : "", seashell, add);

    if (setenv("PATH", new_path, 1) == -1)
        err_exit("setenv");

    int wstat;
    if (waitpid(cpid, &wstat, 0) == -1)
        errExit("waitpid: %m");

    if (!WIFEXITED(wstat) || WEXITSTATUS(wstat) != 0)
        errExit("make process exited abnormally");

    execlp("valgrind",
            "valgrind",
                "--vgdb=no", "--enable-debuginfod=no", "--quiet",
                "--leak-check=full",
                "--errors-for-leak-kinds=definite,indirect",
                "--show-leak-kinds=definite,indirect",
                "--track-fds=bad", "--modify-fds=high",
            "./seashell", "--config=./.seashell.lua", "--log-file=/tmp/shlog",
            (char *)NULL);

    errExit("execl: %m");
}
