#ifndef SHELL_H
#define SHELL_H

#include <termios.h>

#include "linc/string.h"
#include "linc/array.h"
#include "jobs.h"

typedef struct sh_pgrp sh_pgrp;
typedef struct sh_proc sh_proc;

LINC_ARRAY_TYPE(da_sh_proc, sh_proc);

struct sh_pgrp {
    pid_t pgid;
    da_sh_proc procs;
};

struct sh_proc {
    pid_t pid;
    bool subsh;
    char *cmd;
};

void process_signals(void);
void print_job_event(const job_event *jev, bool verbose);

void seashell_shutdown(void);
linc_string shell_get_prompt(void);
int shell_run(const char *line);

#endif
