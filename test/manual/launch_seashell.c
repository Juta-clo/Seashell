#define SEASHELL_PATH "/home/juta/Projects/Seashell/"

#include <wait.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

__attribute__ ((format(printf, 1, 2)))
void err_exit(const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);

    fprintf(stderr, "\n");
    _exit(EXIT_FAILURE);
}

__attribute__ ((format(printf, 1, 2)))
void errExit(const char *fmt, ...) {
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

    if (cpid == 0) {
        execlp("make", "make", "-s", "-C", SEASHELL_PATH, (char *)NULL);
        err_exit("execlp");
    }

    int wstat;
    if (waitpid(cpid, &wstat, 0) == -1)
        errExit("waitpid: %m");

    if (!WIFEXITED(wstat) || WEXITSTATUS(wstat) != 0)
        errExit("make process exited abnormally");

    execlp("valgrind",
            "valgrind",
                "--vgdb=no", "--quiet", "--leak-check=full",
                "--errors-for-leak-kinds=definite,indirect,possible",
                "--show-leak-kinds=definite,indirect,possible",
            SEASHELL_PATH "seashell", (char *)NULL);

    errExit("execl: %m");
}
