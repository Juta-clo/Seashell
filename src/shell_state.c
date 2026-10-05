#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/limits.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <string.h>

#include "linc_tools.h"
#include "input.h"
#include "variable.h"
#include "map.h"
#include "shell_state.h"
#include "sig_funcs.h"
#include "utils.h"

#define DEFAULT_LOG_DIR "/home/juta/Projects/Seashell/logs/shell/"

shell_env sh_env = {0};

// LLOG SET UP
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

// XFUNCS SET UP
XFATAL_HANDLER(xfatal_func) {
    LOG_ERR("%s: %s", XSYSNAME, strerror(XERRNO));
    err_exit("%s", XSYSNAME);
}
static xfatal xf = { .handler = xfatal_func };
XFATAL_DEFAULT(&xf);

args_opt_data opts[] = {
    [LOGFD] = {
        .long_name = "logfd",
        .short_name = 'l',
        .has_arg = ARGS_REQUIRED_ARG,
        .arg_type = ARGS_INT_ARG
    },
    [LOGDIR] = {
        .long_name = "logdir",
        .short_name = 'p',
        .has_arg = ARGS_REQUIRED_ARG,
        .arg_type = ARGS_STR_ARG
    },
    [PROMPT] = {
        .long_name = "prompt",
        .short_name = 'x',
        .has_arg = ARGS_REQUIRED_ARG,
        .arg_type = ARGS_STR_ARG
    },
    (args_opt_data){0}
};

static int open_log_file(const char *log_dir) {
    char log_path[PATH_MAX] = "";
    char link_path[PATH_MAX] = "";

    strcpy(log_path, log_dir);
    strcat(log_path, "/log-XXXXXX");
    int fd = mkstemp(log_path);

    if (fd == -1) {
        perror("mkstemp");
        exit(EXIT_FAILURE);
    }

    strcpy(link_path, log_dir);
    strcat(link_path, "/latest");

    if (unlink(link_path) == -1 && errno != ENOENT) {
        perror("unlink");
        exit(EXIT_FAILURE);
    }

    char *abs_log_path = realpath(log_path, NULL);

    if (!abs_log_path) {
        perror("realpath");
        exit(EXIT_FAILURE);
    }

    if (symlink(abs_log_path, link_path) == -1) {
        perror("symlink");
        exit(EXIT_FAILURE);
    }

    free(abs_log_path);

    return fd;
}

static void log_setup(int *log_fd) {
    if (opts[LOGFD].found) {
        if (fcntl(opts[LOGFD].val_int, F_GETFD) == -1) {
            perror("Error using log fd");
            exit(EXIT_FAILURE);
        }

        *log_fd = opts[LOGFD].val_int;

    } else if (opts[LOGDIR].found) {
        *log_fd = open_log_file(opts[LOGDIR].val_str);

    } else
        *log_fd = open_log_file(DEFAULT_LOG_DIR);
}

static void var_setup(shell_env *env) {
    mp_init(&env->vars, VAR_KEY_SIZE);

    for (char **ep = environ; *ep != NULL; ++ep) {
        char *match = strchr(*ep, '=');
        if (!match)
            xfatal("invalid environ entry");

        char *lhs = strndup(*ep, match - *ep);
        char *rhs = xstrdup(match + 1);
                /* no need to re-export, already in env */
        if (add_var(&sh_env.vars, lhs, rhs, false) == -1)
            LOG_ERR("Failed to add environment variable");

        free(lhs);
        free(rhs);
    }

    add_var(&sh_env.vars, "?", "0", false);
}

void env_setup(shell_env *env) {
    env->subshell = false;
    env->need_prompt = true;
    env->fg_jid = NOFG;
    env->tty_fd = xopen("/dev/tty", O_RDWR | O_CLOEXEC);

    if (opts[PROMPT].found) {
        if (strcmp("cwd", opts[PROMPT].val_str) == 0)
            env->prompt_style = PROMPT_CWD;
        else if (strcmp("simple", opts[PROMPT].val_str) == 0)
            env->prompt_style = PROMPT_SIMPLE;
        else if (strcmp("ps", opts[PROMPT].val_str) == 0)
            env->prompt_style = PROMPT_PS;
    } else {
        env->prompt_style = PROMPT_CWD;
    }

    xsetvbuf(stdin, NULL, _IONBF, 0);
    xsetvbuf(stdout, NULL, _IONBF, 0);
    xsetvbuf(stderr, NULL, _IONBF, 0);

    log_setup(&env->log_fd);
    sig_setup(&env->og_mask);
    var_setup(env);
    input_init(env);

    LOG_INFO("seashell PID(%d)", getpid());
}
