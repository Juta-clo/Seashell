#ifndef EXEC_FUNCS_H
#define EXEC_FUNCS_H

#include <linc/array.h>
#include "parser.h"

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

#endif
