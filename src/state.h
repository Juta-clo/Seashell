#ifndef SHELL_STATE_H
#define SHELL_STATE_H

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/signal.h>

#include <lua.h>
#include <lualib.h>

#include "linc.h"
#include "linc/args.h"
#include "linc/log.h"
#include "map.h"

#define NOFG -1

#define SUBSH_PIPE  1
#define SUBSH_ASYNC 2

typedef struct {
    int fg_jid;
    uint64_t subsh;
    bool lua;

    int tty_fd;
    struct termios tio;
    sigset_t og_mask;

    map_t vars;
    map_t aliases;

    linc_fatal_target shell_fatal;
    linc_log shell_log;
    linc_log assert_log;
    int log_fd;
} shell_env;

enum {
    OPT_LOG_FD,
    OPT_LOG_FILE,
    OPT_LOG_LEVEL,
    OPT_CLEAN,
    OPT_CONFIG,
    OPT_MAX,
};

extern shell_env env;
extern linc_args opts[];

extern volatile sig_atomic_t sigchld_caught;
extern volatile sig_atomic_t sighup_caught;
extern volatile sig_atomic_t sigint_caught;

void sigchld_handler(int);
void sighup_handler(int);
void sigint_handler(int);

void sig_restore(sigset_t *og_mask);
void env_init(int argc, char *const *argv);

void shell_fatal_handler(const linc_fatal_ctx *ctx, void *);
void assert_log_sink(const linc_log_ctx *ctx, void *user_ctx);
void shell_log_sink(const linc_log_ctx *ctx, void *user_ctx);

#endif
