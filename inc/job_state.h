#ifndef JOB_STATE_H
#define JOB_STATE_H

#include "darr.h"
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
    JTERMINATED,
    JSTOPPED,
    JCONTINUED,
} job_event_type;

typedef struct jc_proc jc_proc;
typedef struct jc_pgrp jc_pgrp;
typedef struct jc_job jc_job;
typedef struct job_table job_table;
typedef struct job_event job_event;

struct jc_proc {
    pid_t pid;
    proc_stat stat;

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

struct job_event {
    pid_t jid;
    job_event_type type;
    bool success;

    union {
        int term_sig;
        int stop_sig;
    };
};

pid_t request_job_id(pid_t jid);
pid_t create_job_id(void);

job_table *get_jctl(void);
job_event *pop_job_event(void);

int update_job_proc(wait_event wev);
char *get_jev_str(job_event jev);

void clear_job_table(void);
void clear_job_events(void);

void queue_builtin_exit_event(pid_t jid, int status);
void add_job(pid_t jid, da_pid *pids, pid_t pgid);

#endif
