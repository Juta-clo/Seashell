#include <unistd.h>

#include <linc/log.h>
#include "unity_fixture.h"
#include "state.h"

static FILE *log_file;
const char *test_group;

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
    fprintf(log_file, "TEST_GROUP(%s): %s %s:%d %s(): %s\n",
        test_group, linc_log_level_str(ctx->level),
        ctx->site.file, ctx->site.line, ctx->site.func,
        ctx->msg
    );
}

linc_log unit_shell_log = { .sink = unit_shell_sink };
linc_log unit_assert_log = { .sink = unit_assert_sink };

void run_all(void) {
    test_group = "lexer";
    RUN_TEST_GROUP(lexer);
    test_group = "parser";
    RUN_TEST_GROUP(parser);
    test_group = "jobs";
    RUN_TEST_GROUP(jobs);
    test_group = "input";
    RUN_TEST_GROUP(input);
    test_group = "glob";
    RUN_TEST_GROUP(glob);
}

int main(int argc, const char **argv) {
    if (!(log_file = fopen("/tmp/shlog-unit", "a"))) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    env.shell_log = unit_shell_log;
    env.assert_log = unit_assert_log;

    return UnityMain(argc, argv, run_all);
}
