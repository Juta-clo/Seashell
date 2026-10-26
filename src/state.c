#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/limits.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <string.h>

#include <linc.h>
#include <linc/args.h>
#include <linc/xsys.h>
#include "input.h"
#include "variable.h"
#include "map.h"
#include "state.h"
#include "sig_funcs.h"
#include "utils.h"
#include "config.h"

typedef struct {
    void(*fatal)(const char *, ...);
} fatal_wrapper;

fatal_wrapper wrapper = { .fatal = fatal };

static void linc_fatal(const linc_fatal_ctx *ctx, void *user) {
    char err_buf[BUFSIZ];

    if (ctx->type == LINC_FATAL_OOM) {
        snprintf(err_buf, BUFSIZ, "%s: out of memory", ctx->msg);
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

        linc_site site = {
            .file = ctx->site.file,
            .func = ctx->site.func,
            .line = ctx->site.line
        };

        linc_log_write(
            sh_env.shell_log,
            LINC_LOG_ERROR,
            site,
            "Linc: %s",
            err_buf
        );
    }

    ((fatal_wrapper *)user)->fatal(err_buf);
}

linc_fatal_target linc_target = {
    .handler = linc_fatal,
    .ctx = &wrapper
};

linc_args opts[] = {
    [LOGFD] = {
        .long_name = "log-fd",
        .short_name = 'l',
        .has_arg = LINC_ARGS_ARG_REQ,
        .arg_type = LINC_ARGS_ARG_INT
    },
    [LOGFILE] = {
        .long_name = "log-file",
        .short_name = 'f',
        .has_arg = LINC_ARGS_ARG_REQ,
        .arg_type = LINC_ARGS_ARG_STR
    },
    [PROMPT] = {
        .long_name = "prompt",
        .short_name = 'x',
        .has_arg = LINC_ARGS_ARG_REQ,
        .arg_type = LINC_ARGS_ARG_STR
    },
    (linc_args){0}
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

    sh_env.shell_log = (linc_log){ .sink = shell_log_sink };
    sh_env.assert_log = (linc_log){ .sink = assert_log_sink };

    linc_log_set_default(sh_env.shell_log);
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

shell_env sh_env = {0};

void env_setup(int argc, char *const *argv) {
    linc_fatal_set_target(linc_target);

    if (linc_args_parse(argc, argv, opts, LINC_ARGS_NO_NON_OPTS) == -1)
        exit(EXIT_FAILURE);

    xatexit(seashell_shutdown);

    sh_env.subshell = false;
    sh_env.need_prompt = true;
    sh_env.fg_jid = NOFG;
    sh_env.tty_fd = xopen("/dev/tty", O_RDWR | O_CLOEXEC);

    if (opts[PROMPT].found) {
        if (strcmp("cwd", opts[PROMPT].val_str) == 0)
            sh_env.prompt_style = PROMPT_CWD;
        else if (strcmp("simple", opts[PROMPT].val_str) == 0)
            sh_env.prompt_style = PROMPT_SIMPLE;
        else if (strcmp("ps", opts[PROMPT].val_str) == 0)
            sh_env.prompt_style = PROMPT_PS;
    } else {
        sh_env.prompt_style = PROMPT_CWD;
    }

    xsetvbuf(stdin, NULL, _IONBF, 0);
    xsetvbuf(stdout, NULL, _IONBF, 0);
    xsetvbuf(stderr, NULL, _IONBF, 0);

    log_setup(&sh_env.log_fd);
    var_setup(&sh_env);
    config_init(&sh_env);
    sig_setup(&sh_env.og_mask);
    input_init(&sh_env);

    LOG_INFO("seashell PID(%d)", getpid());
}
