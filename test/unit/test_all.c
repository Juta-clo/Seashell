#include <unistd.h>

#include "unity_fixture.h"
#include "linc_tools.h"

static FILE *log_file;

void assert_llog_sink(const llog_info *info) {
    char log_buf[BUFSIZ];

    int n = snprintf(log_buf, BUFSIZ, "ASSERT: %s\n", info->msg);

    if (n < 0)
        return;

    if (n >= BUFSIZ) {
        log_buf[BUFSIZ - 2] = '\n';
        log_buf[BUFSIZ - 1] = '\0';
    }

    fprintf(log_file, "%s", log_buf);
}

void shell_llog_sink(const llog_info *info) {
    char *lvl_str;
    switch (info->log_level) {
    case LLOG_INFO: lvl_str = "INFO"; break;
    case LLOG_WARN: lvl_str = "WARN"; break;
    case LLOG_ERR:  lvl_str = "ERROR"; break;
    }

    char log_buf[BUFSIZ];

    int n = snprintf(log_buf, BUFSIZ, "%s: %s\n", lvl_str, info->msg);

    if (n < 0)
        return;

    if (n >= BUFSIZ) {
        log_buf[BUFSIZ - 2] = '\n';
        log_buf[BUFSIZ - 1] = '\0';
    }

    fprintf(log_file, "%s", log_buf);
}

llog unit_llog = { .sink = shell_llog_sink };
LLOG_DEFAULT(&unit_llog);

void run_all(void) {
    RUN_TEST_GROUP(lexer);
    RUN_TEST_GROUP(parser);
    RUN_TEST_GROUP(jobs);
    RUN_TEST_GROUP(input);
}

int main(int argc, const char **argv) {
    if (!(log_file = fopen("./logs/test/unit_test.log", "w+"))) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    return UnityMain(argc, argv, run_all);
}
