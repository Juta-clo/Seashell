#ifndef SHELL_H
#define SHELL_H

#include <termios.h>

#include "linc/string.h"
#include "linc/array.h"
#include "jobs.h"

typedef struct pline_data pline_data;
typedef struct pline_proc pline_proc;

LINC_ARRAY_TYPE(da_pline_proc, pline_proc);

struct pline_data {
    pid_t pgid;
    da_pline_proc procs;
};

struct pline_proc {
    pid_t pid;
    char **argv;
};

void process_signals(void);
void print_job_event(int jid, const job_event *jev, bool verbose);

void seashell_shutdown(void);
linc_string shell_get_prompt(void);
int shell_run(const char *line);

#endif
