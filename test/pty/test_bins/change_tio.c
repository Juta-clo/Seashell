#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

void err_exit(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
    long sleep_time = 0;

    if (argc > 1) {
        if (argv[1][0] == '\0') {
            fprintf(stderr, "%s: bad input: %s\n", argv[0], argv[1]);
            exit(EXIT_FAILURE);
        }

        char *endptr;
        sleep_time = strtol(argv[1], &endptr, 10);
        if (*endptr != '\0') {
            fprintf(stderr, "%s: bad input: %s\n", argv[0], argv[1]);
            exit(EXIT_FAILURE);
        }
    }

    struct termios tio;
    if (tcgetattr(STDIN_FILENO, &tio) == -1)
        err_exit("tcgetattr");

    tio.c_lflag &= ~ECHO;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &tio) == -1)
        err_exit("tcsetattr");

    printf("x\n");
    sleep(sleep_time);
}
