#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/limits.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <string.h>

#include "input.h"
#include "variable.h"
#include "map.h"
#include "shell_state.h"
#include "sig_funcs.h"
#include "xfuncs.h"
#include "llog.h"
#include "utils.h"
#include "dstr.h"

#define DEFAULT_LOG_DIR "/home/juta/Projects/Seashell/logs/shell/"

shell_env sh_env = {0};

LLOG_SINK(shell_llog_sink) {
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

XFATAL_HANDLER(xfatal_func) {
    LOG_ERR("%s: %s", XSYSNAME, strerror(XERRNO));
    err_exit("%s", XSYSNAME);
}

opt_data opts[] = {
    [LOGFD] = {
        .long_name = "logfd",
        .short_name = 'l',
        .has_arg = REQUIRED_ARG,
        .arg_type = INT_ARG
    },
    [LOGDIR] = {
        .long_name = "logdir",
        .short_name = 'p',
        .has_arg = REQUIRED_ARG,
        .arg_type = STR_ARG
    },
    [PROMPT] = {
        .long_name = "prompt",
        .short_name = 'x',
        .has_arg = REQUIRED_ARG,
        .arg_type = STR_ARG
    },
    (opt_data){0}
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

    llog_set_sink(shell_llog_sink);
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
}

bool shell_in_fg(void) {
    return sh_env.fg_jid == NOFG;
}

void env_setup(shell_env *env) {
    /* so fgets (and other stdio funcs) don't steal bytes */
    xsetvbuf(stdin, NULL, _IONBF, 0);
    xsetvbuf(stdout, NULL, _IONBF, 0);
    xsetvbuf(stderr, NULL, _IONBF, 0);

    log_setup(&env->log_fd);
    set_xfatal_handler(&xfatal_func);
    sig_setup(&env->og_mask);
    var_setup(env);

    env->subshell = false;
    env->fg_jid = NOFG;
    env->tty_fd = xopen("/dev/tty", O_RDWR | O_CLOEXEC);

    if (opts[PROMPT].found) {
        if (strcmp("cwd", opts[PROMPT].val_str) == 0)
            env->prompt_style = PROMPT_CWD;
        else if (strcmp("simple", opts[PROMPT].val_str) == 0)
            env->prompt_style = PROMPT_SIMPLE;
    } else {
        env->prompt_style = PROMPT_CWD;
    }

    LOG_INFO("seashell PID(%d)", getpid());
}
