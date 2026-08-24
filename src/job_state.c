#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "job_state.h"
#include "wait_stat.h"
#include "utils.h"
#include "xfuncs.h"

static job_table jctl = {0};
static da_jevent jevs = {0};

static void init_job(jc_job *job) {
    *job = (jc_job){0};

    da_init(&job->pgrp.procs);
}

static void free_job(jc_job *job) {
    da_free(&job->pgrp.procs);
    *job = (jc_job){0};
}

static job_stat calc_job_stat(jc_job *job) {
    bool stopped = false;

    for (size_t i = 0; i < job->pgrp.procs.size; ++i) {

        jc_proc *proc = &job->pgrp.procs.data[i];

        if (proc->stat == PROC_RUN)
            return JOB_RUN;

        if (proc->stat == PROC_STOP)
            stopped = true;
    }

    return stopped ? JOB_STOP : JOB_EXIT;
}

static bool identify_proc(pid_t pid, jc_job **job, jc_proc** proc) {
    assert(job && proc);

    for (size_t i = 0; i < jctl.jobs.size; ++i) {
        jc_job *in_job = &jctl.jobs.data[i];

        for (size_t y = 0; y < in_job->pgrp.procs.size; ++y) {
            jc_proc *in_proc = &in_job->pgrp.procs.data[y];

            if (pid == in_proc->pid) {
                *proc = in_proc;
                *job = in_job;

                return true;
            }
        }
    }

    return false;
}

pid_t request_job_id(pid_t jid) {
    for (size_t i = 0; i < jctl.jobs.size; ++i)
        if (jid == jctl.jobs.data[i].jid)
            return -1;

    return jid;
}

pid_t create_job_id(void) {
    pid_t jid = 1;

    if (jctl.jobs.size == 0)
        return jid;

    for (size_t i = 0; i < jctl.jobs.size; ++i) {
        if (jid == jctl.jobs.data[i].jid) {
            ++jid;
            i = 0;
            continue;
        }
    }

    return jid;
}

char *get_jev_str(job_event jev) {
    static char buf[4096];

    if (jev.type == JEXITED) {
        snprintf(buf, 4096, "[%d] exited", jev.jid);
    } else if (jev.type == JTERMINATED) {
        snprintf(buf, 4096, "[%d] %s by signal %d", jev.jid,
                strsignal(jev.term_sig), jev.term_sig);
    } else if (jev.type == JSTOPPED) {
        snprintf(buf, 4096, "[%d] %s by signal %d", jev.jid,
                strsignal(jev.stop_sig), jev.stop_sig);
    } else if (jev.type == JCONTINUED) {
        snprintf(buf, 4096, "[%d] continued", jev.jid);
    } else
        xfatal("unknown job event type");

    return buf;
}

void clear_job_table(void) {
    for (size_t i = 0; i < jctl.jobs.size; ++i) {
        LOG_INFO("[%d] removed", jctl.jobs.data[i].jid);
        free_job(&jctl.jobs.data[i]);
    }

    da_free(&jctl.jobs);
}

void clear_job_events(void) {
    da_free(&jevs);
}

job_table *get_jctl(void) {
    return &jctl;
}

size_t get_job_index(pid_t jid) {
    for (size_t i = 0; i < jctl.jobs.size; ++i)
        if (jid == jctl.jobs.data[i].jid)
            return i;

    xfatal("tried to get job index of non-existant job\n");
}

job_event *pop_job_event(void) {
    static job_event jev;

    if (jevs.size == 0)
        return NULL;

    jev = jevs.data[0];

    da_delete(&jevs, 0);

    return &jev;
}

void add_job_event(job_event jev) {
    job_event *new_jev = da_push(&jevs);
    *new_jev = jev;
}

int update_job_proc(wait_event wev) {
    jc_job *job;
    jc_proc *proc;

    if (!identify_proc(wev.pid, &job, &proc))
        return -1;

    if (wev.type == PEXITED) {
        proc->stat = PROC_EXIT;
        proc->exit_stat = wev.exit_stat;
    }

    if (wev.type == PSIGNALED) {
        proc->stat = PROC_SIG;
        proc->exit_stat = wev.term_sig;
    }

    else if (wev.type == PSTOPPED) {
        proc->stat = PROC_STOP;
        proc->stop_sig = wev.stop_sig;
    }

    else if (wev.type == PCONTINUED) {
        proc->stat = PROC_RUN;
    }

    job_stat stat = calc_job_stat(job);

    if (job->stat == JOB_RUN && stat == JOB_STOP) {
        add_job_event((job_event){
            .jid = job->jid,
            .type = JSTOPPED,
            .stop_sig = proc->stop_sig
        });
        job->stat = stat;

    } else if (job->stat == JOB_STOP && stat == JOB_RUN) {
        add_job_event((job_event){
            .jid = job->jid,
            .type = JCONTINUED
        });
        job->stat = stat;

    } else if (stat == JOB_EXIT) {
        if (job->last->stat == PROC_EXIT) {
            add_job_event((job_event){
                .jid = job->jid,
                .type = JEXITED,
                .success = job->last->exit_stat == EXIT_SUCCESS
            });

        } else if (job->last->stat == PROC_SIG) {
            add_job_event((job_event){
                .jid = job->jid,
                .type = JTERMINATED,
                .success = false,
                .term_sig = job->last->term_sig
            });

        } else {
            xfatal("unexpected process exit status");
        }

        /* delete job */
        size_t i = get_job_index(job->jid);
        free_job(&jctl.jobs.data[i]);
        da_delete(&jctl.jobs, i);
    }

    return 0;
}

static bool pg_leader_missing(pid_t pgid, da_pid *pids) {
    for (size_t i = 0; i < pids->size; ++i)
        if (pgid == pids->data[i])
            return false;

    return true;
}

void queue_builtin_exit_event(pid_t jid, int status) {
    add_job_event((job_event){
        .jid = jid,
        .type = JEXITED,
        .success = (status == EXIT_SUCCESS)
    });

    xkill(getpid(), SIGCHLD);
}

void add_job(pid_t jid, da_pid *pids, pid_t pgid) {
    assert(pids);

    if (pids->size == 0)
        xfatal("empty pid array");

    if (pg_leader_missing(pgid, pids))
        xfatal("process group leader missing");

    jc_job *job = da_push(&jctl.jobs);

    init_job(job);

    job->jid = jid;
    job->stat = JOB_RUN;
    job->pgrp.pgid = pgid;

    for (size_t i = 0; i < pids->size; ++i) {
        jc_proc *proc = da_push(&job->pgrp.procs);

        proc->pid = pids->data[i];
        proc->stat = PROC_RUN;

        if (i == pids->size - 1)
            job->last = proc;
    }
}
