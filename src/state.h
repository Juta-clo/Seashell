#ifndef SHELL_STATE_H
#define SHELL_STATE_H

#include <bits/types/sigset_t.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <lua.h>
#include <lualib.h>
#include <linc/args.h>
#include <linc/log.h>
#include "map.h"

#define NOFG -1

#define SUBSH_NONE  0
#define SUBSH_PIPE  1
#define SUBSH_ASYNC 2

typedef struct {
    uint64_t subsh;
    bool job_ctrl;
    bool lua_mode;
    pid_t fg_jid;
    int fg_pending;

    int tty_fd;
    struct termios tio;
    sigset_t og_mask;

    map_t vars;
    map_t aliases;

    int log_fd;
    linc_log shell_log;
    linc_log assert_log;
} shell_env;

extern shell_env sh_env;

typedef enum {
    LOGFD,
    LOGFILE,
    CLEAN,
    CONFIG,
    OPT_MAX
} opt_names;

extern linc_args opts[];
extern char **environ;

void env_setup(int argc, char *const *argv);
void assert_log_sink(const linc_log_ctx *ctx, void *user_ctx);
void shell_log_sink(const linc_log_ctx *ctx, void *user_ctx);

#endif
