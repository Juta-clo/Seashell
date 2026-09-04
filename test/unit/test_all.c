#include <unistd.h>

#include "unity_fixture.h"

#define DEFAULT_LOG_DIR "/home/juta/Projects/Seashell/logs/test/"

void run_all(void) {
    RUN_TEST_GROUP(lexer);
    RUN_TEST_GROUP(parser);
    RUN_TEST_GROUP(jobs);
}

int main(int argc, const char **argv) {
    return UnityMain(argc, argv, run_all);
}
