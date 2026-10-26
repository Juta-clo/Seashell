#include "shell.h"
#include "state.h"

int main(int argc, char *const *argv) {
    env_setup(argc, argv);
    shell_run();
}
