#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 2 || argv[1][0] == '\0')
        return EXIT_FAILURE;

    if (strcmp("-KILL", argv[1]) == 0)
        kill(getpid(), SIGKILL);
    if (strcmp("-STOP", argv[1]) == 0)
        kill(getpid(), SIGSTOP);
    if (strcmp("-TSTP", argv[1]) == 0)
        kill(getpid(), SIGTSTP);
    if (strcmp("-INT", argv[1]) == 0)
        kill(getpid(), SIGINT);
    if (strcmp("-CONT", argv[1]) == 0)
        kill(getpid(), SIGCONT);
    if (strcmp("-HUP", argv[1]) == 0)
        kill(getpid(), SIGHUP);
    if (strcmp("-TERM", argv[1]) == 0)
        kill(getpid(), SIGTERM);

    char *endptr = argv[1];
    long exit_stat = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0')
        return EXIT_FAILURE;

    return exit_stat;
}
