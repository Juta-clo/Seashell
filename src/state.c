#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/limits.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <string.h>

#include "config.h"
#include "linc.h"
#include "linc/args.h"
#include "linc/xsys.h"
#include "input.h"
#include "shell.h"
#include "variable.h"
#include "map.h"
#include "state.h"
#include "sig_funcs.h"
#include "utils.h"
#include "luart.h"

#define ALIAS_MAX 4096

shell_env env = {0};

linc_args opts[] = {
    [OPT_LOG_FD] = {
        .long_name = "log-fd",
        .short_name = 'l',
        .has_arg = LINC_ARGS_ARG_REQ,
        .arg_type = LINC_ARGS_ARG_INT
    },
    [OPT_LOG_FILE] = {
        .long_name = "log-file",
        .short_name = 'f',
        .has_arg = LINC_ARGS_ARG_REQ,
        .arg_type = LINC_ARGS_ARG_STR
    },
    [OPT_CLEAN] = {
        .long_name = "clean",
        .short_name = 'q',
        .has_arg = LINC_ARGS_ARG_NONE
    },
    [OPT_CONFIG] = {
        .long_name = "config-file",
        .short_name = 'e',
        .has_arg = LINC_ARGS_ARG_REQ,
        .arg_type = LINC_ARGS_ARG_STR
    },
    {0}
};

volatile sig_atomic_t sigchld_caught = false;
volatile sig_atomic_t sighup_caught = false;
volatile sig_atomic_t sigint_caught = false;

void linc_init(int argc, char *const *argv) {
    if (linc_args_parse(argc, argv, opts, LINC_ARGS_NO_NON_OPTS) == -1)
        exit(EXIT_FAILURE);

    env.shell_fatal = (linc_fatal_target){ .handler = shell_fatal_handler };
    env.shell_log = (linc_log){ .sink = shell_log_sink };
    env.assert_log = (linc_log){ .sink = assert_log_sink };

    linc_fatal_set_target(env.shell_fatal);
    linc_log_set_default(env.shell_log);
}

void sigchld_handler(int _) { sigchld_caught = true; }
void sighup_handler(int _) { sighup_caught = true; }
void sigint_handler(int _) { sigint_caught = true; }

void sig_restore(sigset_t *og_mask) {
    xsigprocmask(SIG_SETMASK, og_mask, nullptr);

    sig_set_handler(SIGTTOU, SIG_DFL);
    sig_set_handler(SIGTTIN, SIG_DFL);
    sig_set_handler(SIGTSTP, SIG_DFL);
    sig_set_handler(SIGQUIT, SIG_DFL);
    sig_set_handler(SIGTERM, SIG_DFL);

    sig_set_handler(SIGCHLD, SIG_DFL);
    sig_set_handler(SIGHUP, SIG_DFL);
    sig_set_handler(SIGINT, SIG_DFL);
}

static void sig_init(void) {
    sig_set_handler(SIGTTOU, SIG_IGN);
    sig_set_handler(SIGTTIN, SIG_IGN);
    sig_set_handler(SIGTSTP, SIG_IGN);
    sig_set_handler(SIGQUIT, SIG_IGN);
    sig_set_handler(SIGTERM, SIG_IGN);

    sig_set_handler(SIGCHLD, sigchld_handler);
    sig_set_handler(SIGHUP, sighup_handler);
    sig_set_handler(SIGINT, sigint_handler);

    env.og_mask = sig_block(SIGCHLD);
    sig_block(SIGHUP);
    sig_block(SIGINT);
}

static void log_init(void) {
    if (opts[OPT_LOG_FD].found) {
        if (fcntl(opts[OPT_LOG_FD].val_int, F_GETFD) == -1) {
            perror("Error using log fd");
            exit(EXIT_FAILURE);
        }

        env.log_fd = opts[OPT_LOG_FD].val_int;
    } else if (opts[OPT_LOG_FILE].found) {
        env.log_fd = open(opts[OPT_LOG_FILE].val_str,
                O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (env.log_fd == -1) {
            perror("Failed to open log file");
            exit(EXIT_FAILURE);
        }
    } else {
        env.log_fd = -1;
    }
}

static void var_init(void) {
    mp_init(&env.vars, VAR_KEY_SIZE);

    for (char **ep = environ; *ep != nullptr; ++ep) {
        char *match = strchr(*ep, '=');
        if (!match)
            fatal("invalid environ entry");

        char *lhs = strndup(*ep, match - *ep);
        char *rhs = xstrdup(match + 1);
                /* no need to re-export, already in env */
        if (add_var(&env.vars, lhs, rhs, false) == -1)
            LOG_ERR("Failed to add environment variable");

        free(lhs);
        free(rhs);
    }

    add_var(&env.vars, "?", "0", false);
}

void env_init(int argc, char *const *argv) {
    xsetvbuf(stdin, nullptr, _IONBF, 0);
    xsetvbuf(stdout, nullptr, _IONBF, 0);
    xsetvbuf(stderr, nullptr, _IONBF, 0);

    env.fg_jid = NOFG;
    env.tty_fd = xopen("/dev/tty", O_RDWR | O_CLOEXEC);

    linc_init(argc, argv);
    mp_init(&env.aliases, ALIAS_MAX);

    log_init();
    var_init();
    input_init();
    luart_init();
    sig_init();

    input_set_suspend_mask(env.og_mask);
    input_sig_cb = process_signals;

    if (!opts[OPT_CLEAN].found)
        config_init();

    LOG_INFO("seashell PID(%d)", getpid());
}
