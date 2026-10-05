#ifndef JOB_STATE_H
#define JOB_STATE_H

#include "linc_tools.h"
#include "exec_funcs.h"
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

typedef enum {
    JEXIT_NORMAL,
    JEXIT_INTR,
} job_exit_type;

typedef struct jc_proc jc_proc;
typedef struct jc_pgrp jc_pgrp;
typedef struct jc_job jc_job;
typedef struct job_table job_table;
typedef struct job_event job_event;
typedef struct proc_view proc_view;

DA_TYPE(da_proc, jc_proc);
DA_TYPE(da_pgrp, jc_pgrp);
DA_TYPE(da_job, jc_job);
DA_TYPE(da_jevent, job_event);
DA_TYPE(da_proc_view, proc_view);

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
    pid_t jid;
    jc_pgrp pgrp;
    job_stat stat;
    jc_proc *last;
};

struct job_table {
    da_job jobs;
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

struct job_event {
    pid_t jid;
    job_event_type type;
    job_exit_type exit_type;

    bool success;
    bool signaled;
    bool intr_stop;

    /* for display only */

    bool builtin; /* builtins don't have pviews */
    job_stat stat;
    proc_view *last;
    da_proc_view pviews;
};

pid_t request_job_id(pid_t jid);
pid_t create_job_id(void);

job_table *get_jctl(void);
da_jevent get_job_info(void);
job_event *pop_job_event(void);

int update_job_proc(wait_event wev);
void free_job_event(job_event *jev);

void clear_job_table(void);
void clear_job_events(void);

pid_t lookup_job_pgrp(pid_t jid);
void queue_builtin_exit_event(pid_t jid, int status);
void add_job(pid_t jid, pline_data *pld);

#endif
