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
#include "llog_sinks.h"

shell_env sh_env = {0};

// XFUNCS SET UP
XFATAL_HANDLER(xfatal_func) {
    LOG_ERR("%s: %s", XSYSNAME, strerror(XERRNO));
    err_exit("%s", XSYSNAME);
}
static xfatal xf = { .handler = xfatal_func };
XFATAL_DEFAULT(&xf);

args_opt_data opts[] = {
    [LOGFD] = {
        .long_name = "log-fd",
        .short_name = 'l',
        .has_arg = ARGS_REQUIRED_ARG,
        .arg_type = ARGS_INT_ARG
    },
    [LOGFILE] = {
        .long_name = "log-file",
        .short_name = 'f',
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

static void seashell_shutdown(void) {
    input_deinit(&sh_env);

    for (size_t i = 0; i < get_jctl()->jobs.size; ++i) {
        jc_job *job = &get_jctl()->jobs.data[i];

        if (getpgrp() == job->pgrp.pgid || job->pgrp.pgid <= 1)
            xfatal("unexpected pgid %d", job->pgrp.pgid);

        if (kill(-job->pgrp.pgid, SIGHUP) == -1 && errno != ESRCH) {
            LOG_ERR("kill: %m");
            err_exit("kill");
        }

        if (kill(-job->pgrp.pgid, SIGCONT) == -1 && errno != ESRCH) {
            LOG_ERR("kill: %m");
            err_exit("kill");
        }
    }

    while (get_wstat(&(wait_event){0}) != -1)
        continue;

    clear_job_events();
    clear_job_table();

    LOG_INFO("seashell shutting down");
}

static void log_setup(int *log_fd) {
    if (opts[LOGFD].found) {
        if (fcntl(opts[LOGFD].val_int, F_GETFD) == -1) {
            perror("Error using log fd");
            exit(EXIT_FAILURE);
        }

        *log_fd = opts[LOGFD].val_int;
    } else if (opts[LOGFILE].found) {
        *log_fd = open(opts[LOGFILE].val_str, O_WRONLY | O_CREAT, 0666);
        if (*log_fd == -1) {
            perror("Failed to open log file");
            exit(EXIT_FAILURE);
        }
    } else {
        *log_fd = -1;
    }

    sh_env.assert_llog.sink = assert_llog_sink;
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

void env_setup(shell_env *env, int argc, char *const *argv) {
    if (args_parse(argc, argv, opts, ARGS_NO_NON_OPTS) == -1)
        exit(EXIT_FAILURE);

    xatexit(seashell_shutdown);

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
