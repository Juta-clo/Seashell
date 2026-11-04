#ifndef JOB_STATE_H
#define JOB_STATE_H

#include <termios.h>

#include "linc/array.h"
#include "wait_stat.h"

typedef enum {
    JOB_EXIT,
    JOB_STOP,
    JOB_RUN,
} job_stat;

typedef enum {
    PROC_EXIT,
    PROC_SIG,
    PROC_STOP,
    PROC_RUN,
} proc_stat;

typedef enum {
    JEXITED,
    JSTOPPED,
    JCONTINUED,
    JINFO,
} job_event_type;

typedef struct jc_proc jc_proc;
typedef struct jc_pgrp jc_pgrp;
typedef struct jc_job jc_job;
typedef struct job_table job_table;
typedef struct job_event job_event;
typedef struct proc_view proc_view;

LINC_ARRAY_TYPE(da_proc, jc_proc);
LINC_ARRAY_TYPE(da_job, jc_job);
LINC_ARRAY_TYPE(da_jevent, job_event);
LINC_ARRAY_TYPE(da_proc_view, proc_view);

struct job_table {
    da_job jobs;
};

struct jc_proc {
    pid_t pid;
    proc_stat stat;
    char **argv;

    union {
        int term_sig;
        int stop_sig;
        int exit_stat;
    };
};

struct jc_pgrp {
    pid_t pgid;
    da_proc procs;
};

struct jc_job {
    int jid;
    jc_pgrp pgrp;
    job_stat stat;
    jc_proc *last;

    struct termios tio;
    bool tio_saved;
};

struct job_event {
    job_event_type type;
    bool signaled;
    bool intr_sig;

    int jid;
    proc_view *last;
    da_proc_view pviews;
};

struct proc_view {
    pid_t pid;
    proc_stat stat;
    char **argv;

    union {
        int term_sig;
        int stop_sig;
        int exit_stat;
    };
};

struct sh_pgrp;

void job_hangup(void);
da_jevent job_get_snapshot(void);
job_table *job_get_table(void);

job_event *job_event_pop(void);
void job_event_free(job_event *jev);
void job_clear(void);

int job_update(wait_event wev);
bool job_set_running(int jid, job_event *jev);

int job_new_id(void);
void job_add(int jid, struct sh_pgrp *pld);

jc_job *job_lookup(int jid);
pid_t job_lookup_pgrp(int jid);
void job_queue_builtin_exit(int jid, char **argv, int status);

#endif
