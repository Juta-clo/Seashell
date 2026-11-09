#include <linc/string.h>
#include <linc/log.h>
#include <stdio.h>

#include "state.h"
#include "utils.h"

void shell_fatal_handler(const linc_fatal_ctx *ctx, void *_) {
    char err_buf[BUFSIZ];

    if (ctx->type == LINC_FATAL_OOM) {
        snprintf(err_buf, BUFSIZ, "%s:%d %s: out of memory",
                ctx->site.file, ctx->site.line, ctx->msg);
    } else {
        int n = snprintf(err_buf, BUFSIZ, "%s", ctx->msg);

        if (n > 0 && n < BUFSIZ && ctx->type == LINC_FATAL_SYS) {
            snprintf(
                err_buf + n,
                BUFSIZ - n,
                ": %s",
                strerror(ctx->saved_errno)
            );
        }

        linc_log_write(
            env.shell_log,
            LINC_LOG_ERROR,
            ctx->site,
            "Linc: %s",
            err_buf
        );
    }

    fatal_imp("%s", err_buf);
}

void assert_log_sink(const linc_log_ctx *ctx, void *_) {
    linc_string output_msg = LINC_STRING_INIT;

    linc_string_appendf(&output_msg,
            "%s %s:%d: %s(): assertion '%s' failed\n",
            linc_log_level_str(ctx->level),
            ctx->site.file, ctx->site.line, ctx->site.func, ctx->msg);

    fprintf(stderr, "seashell: %s", output_msg.cstr);

    if (env.log_fd != -1)
        write(env.log_fd, output_msg.cstr, output_msg.len);

    linc_string_free(&output_msg);
}

void shell_log_sink(const linc_log_ctx *ctx, void *_) {
    if (env.log_fd == -1)
        return;

    if (opts[OPT_LOG_LEVEL].found) {
        const char *level = opts[OPT_LOG_LEVEL].val_str;

        if (STREQ("info", level) && ctx->level != LINC_LOG_INFO)
            return;
        if (STREQ("warn", level) && ctx->level != LINC_LOG_WARN)
            return;
        if (STREQ("error", level) && ctx->level != LINC_LOG_ERROR)
            return;
    }

    linc_string output_msg = LINC_STRING_INIT;

    linc_string_appendf(&output_msg, "%s %s:%d: %s\n",
        linc_log_level_str(ctx->level),
        ctx->site.file,
        ctx->site.line,
        ctx->msg
    );

    write(env.log_fd, output_msg.cstr, output_msg.len);
    linc_string_free(&output_msg);
}
