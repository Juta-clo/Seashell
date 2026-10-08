#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int _, char **argv) {
    if (write(STDOUT_FILENO, "x", 1) != 1) {
        fprintf(stderr, "%s: write: failed/partial write\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    sleep(100);
}
