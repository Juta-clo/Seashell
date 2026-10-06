#ifndef SHELL_STATE_H
#define SHELL_STATE_H

#include <bits/types/sigset_t.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include "linc_tools.h"
#include "map.h"
#include "parser.h"
#include "printty.h"

#define NOFG -1

typedef struct {
    bool subshell;
    pid_t fg_jid;
    int tty_fd;
    sigset_t og_mask;
    int log_fd;
    int prompt_style;
    map_t vars;
    struct termios tty_state;
    Printty *pt;
    bool need_prompt;
    llog assert_llog;
} shell_env;

extern shell_env sh_env;

struct job_plan {
    pid_t jid;
    bool tty_state_saved;
    struct termios tty_state;
    size_t index;
    ps_ast *ast;
};

typedef struct job_plan job_plan;

typedef enum {
    LOGFD,
    LOGDIR,
    PROMPT,
} opt_names;

extern args_opt_data opts[];
extern char **environ;

void env_setup(shell_env *env);

#endif
