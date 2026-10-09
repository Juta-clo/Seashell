#include "shell_state.h"
#include "linc_tools.h"

void assert_llog_sink(const llog_info *info) {
    dstr output_msg;
    dstr_init(&output_msg);

    dstrcat(&output_msg, "ASSERT ");
    dstrcat(&output_msg, info->msg);
    dstrcat(&output_msg, "\n");

    write(sh_env.log_fd, output_msg.c_str, output_msg.len);
    dstr_free(&output_msg);
}

void shell_llog_sink(const llog_info *info) {
    dstr output_msg;
    dstr_init(&output_msg);

    char *lvl_str;

    switch (info->log_level) {
    case LLOG_INFO: lvl_str = "INFO"; break;
    case LLOG_ERR: lvl_str = "ERROR"; break;
    case LLOG_WARN: lvl_str = "WARN"; break;
    }

    dstr_printf(&output_msg, "%s %s:%d: %s\n",
            lvl_str, info->site->file, info->site->line, info->msg);
    write(sh_env.log_fd, output_msg.c_str, output_msg.len);
    dstr_free(&output_msg);
}

static llog shell_log = { .sink = shell_llog_sink };
LLOG_DEFAULT(&shell_log);
