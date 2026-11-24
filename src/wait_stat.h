#ifndef WAITSTAT_H
#define WAITSTAT_H

#include <sys/wait.h>

#include "linc/array.h"

#define WOPT_JOB (WUNTRACED | WCONTINUED | WNOHANG)

typedef enum {
    PEXITED,
    PSIGNALED,
    PSTOPPED,
    PCONTINUED,
} wait_type;

struct wait_event {
    pid_t pid;
    wait_type type;

    union {
        int term_sig;
        int stop_sig;
        int exit_stat;
    };
};

typedef struct wait_event wait_event;
LINC_ARRAY_TYPE(da_wevent, wait_event);

extern wait_event last_exit_event;

bool last_interrupted(void);
char *get_wstat_str(pid_t pid, int wstat);
int get_wstat(wait_event *wev, pid_t pid, int wopt);

#endif
