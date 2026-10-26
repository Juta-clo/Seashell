#include <linc/string.h>
#include <linc/log.h>

#include "shell.h"

void assert_log_sink(const linc_log_ctx *ctx, void *_) {
    linc_string output_msg = LINC_STRING_INIT;

    linc_string_append(&output_msg, "ASSERT ");
    linc_string_append(&output_msg, ctx->msg);
    linc_string_append(&output_msg, "\n");

    if (sh_env.log_fd != -1)
        write(sh_env.log_fd, output_msg.cstr, output_msg.len);

    fprintf(stderr, "seashell: %s\n", output_msg.cstr);
    linc_string_free(&output_msg);
}

void shell_log_sink(const linc_log_ctx *ctx, void *_) {
    if (sh_env.log_fd == -1)
        return;

    linc_string output_msg = LINC_STRING_INIT;

    linc_string_appendf(&output_msg, "%s %s:%d: %s\n",
            linc_log_level_str(ctx->level),
            ctx->site.file,
            ctx->site.line,
            ctx->msg
    );

    write(sh_env.log_fd, output_msg.cstr, output_msg.len);
    linc_string_free(&output_msg);
}
