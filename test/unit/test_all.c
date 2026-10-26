#include <unistd.h>

#include <linc/log.h>
#include "unity_fixture.h"
#include "state.h"

static FILE *log_file;

static void unit_assert_sink(const linc_log_ctx *ctx, void *_) {
    char log_buf[BUFSIZ];

    int n = snprintf(log_buf, BUFSIZ, "ASSERT: %s\n", ctx->msg);

    if (n < 0)
        return;

    if (n >= BUFSIZ) {
        log_buf[BUFSIZ - 2] = '\n';
        log_buf[BUFSIZ - 1] = '\0';
    }

    fprintf(stderr, "\x1b[41m%s\x1b[m", log_buf);
}

static void unit_shell_sink(const linc_log_ctx *ctx, void *_) {
    char log_buf[BUFSIZ];

    int n = snprintf(log_buf, BUFSIZ, "%s: %s\n",
        linc_log_level_str(ctx->level),
        ctx->msg
    );

    if (n < 0)
        return;

    if (n >= BUFSIZ) {
        log_buf[BUFSIZ - 2] = '\n';
        log_buf[BUFSIZ - 1] = '\0';
    }

    fprintf(log_file, "%s", log_buf);
}

linc_log unit_shell_log = { .sink = unit_shell_sink };
linc_log unit_assert_log = { .sink = unit_assert_sink };

void run_all(void) {
    fprintf(log_file, "--Starting test group: lexer--\n");
    RUN_TEST_GROUP(lexer);
    fprintf(log_file, "\n--Starting test group: parser--\n");
    RUN_TEST_GROUP(parser);
    fprintf(log_file, "\n--Starting test group: jobs--\n");
    RUN_TEST_GROUP(jobs);
    fprintf(log_file, "\n--Starting test group: input--\n");
    RUN_TEST_GROUP(input);
    fprintf(log_file, "\n--Starting test group: glob--\n");
    RUN_TEST_GROUP(glob);
}

int main(int argc, const char **argv) {
    if (!(log_file = fopen("/tmp/seashell-log.unit", "w+"))) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    sh_env.shell_log = unit_shell_log;
    sh_env.assert_log = unit_assert_log;

    return UnityMain(argc, argv, run_all);
}
