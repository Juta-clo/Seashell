#ifndef SHELL_H
#define SHELL_H

#include <termios.h>

#include "linc/string.h"
#include "linc/array.h"
#include "parser.h"
#include "job_state.h"

typedef struct job_plan job_plan;
LINC_ARRAY_TYPE(da_plan, job_plan);

struct job_plan {
    pid_t jid;
    size_t index;
    ps_ast *ast;
};

extern da_plan plans;

typedef struct pline_data pline_data;
typedef struct pline_proc pline_proc;

LINC_ARRAY_TYPE(da_pline_proc, pline_proc);

struct pline_proc {
    pid_t pid;
    char **argv;
};

struct pline_data {
    pid_t pgid;
    da_pline_proc procs;
};

void move_fd(int fd1, int fd2);
void child_fd_setup(bool first, bool last, int next_pipe[2], int prev_rfd);
void child_redir_setup(da_redir *redirs);

pline_data exec_pline(const ps_pline *pline, bool bg);
ps_andor *plan_next(job_plan *plan);
job_plan *add_plan(ps_ast *ast);
void remove_plan(size_t index);
void plan_done(pid_t jid);
job_plan *lookup_plan(pid_t jid, size_t *index);
void process_signals(void);
void shell_sleep(void);
linc_string shell_get_prompt(void);
int shell_run(const char *line);

#endif
