#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>

static volatile sig_atomic_t sigint_caught = 0;

void sigint_handler(int _) {
    sigint_caught = 1;
}

int main(int argc, char **argv) {
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    printf("x\n");
    pause();

    return EXIT_SUCCESS;
}
