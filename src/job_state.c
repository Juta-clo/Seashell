#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "linc_tools.h"
#include "job_state.h"
#include "wait_stat.h"
#include "utils.h"
#include "exec_funcs.h"

static job_table jctl = {0};
static da_jevent jevs = {0};

static void init_job(jc_job *job) {
    *job = (jc_job){0};

    da_init(&job->pgrp.procs);
}

static void free_job(jc_job *job) {
    for (size_t i = 0; i < job->pgrp.procs.size; ++i)
        free_argv(job->pgrp.procs.data[i].argv);

    da_free(&job->pgrp.procs);
    *job = (jc_job){0};
}

static job_stat calc_job_stat(jc_job *job) {
    bool stopped = false;

    for (size_t i = 0; i < job->pgrp.procs.size; ++i) {

        jc_proc *proc = &job->pgrp.procs.data[i];

        if (proc->stat == PROC_RUN)
            return JOB_RUN;

        if (proc->stat == PROC_STOP) {
            LOG_INFO("stopped: %d", proc->pid);
            stopped = true;
        }
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

static void update_proc(jc_proc *proc, wait_event wev) {
    if (wev.type == PEXITED) {
        proc->stat = PROC_EXIT;
        proc->exit_stat = wev.exit_stat;
    }

    if (wev.type == PSIGNALED) {
        proc->stat = PROC_SIG;
        proc->term_sig = wev.term_sig;
    }

    else if (wev.type == PSTOPPED) {
        proc->stat = PROC_STOP;
        proc->stop_sig = wev.stop_sig;
    }

    else if (wev.type == PCONTINUED) {
        proc->stat = PROC_RUN;
    }
}

static size_t get_job_index(pid_t jid) {
    for (size_t i = 0; i < jctl.jobs.size; ++i)
        if (jid == jctl.jobs.data[i].jid)
            return i;

    xfatal("tried to get job index of non-existant job\n");
}

static void remove_job (pid_t jid) {
    size_t i = get_job_index(jid);
    free_job(&jctl.jobs.data[i]);
    da_delete(&jctl.jobs, i);
}

static bool job_inter_stopped(jc_job *job) {
    for (size_t i = 0; i < job->pgrp.procs.size; ++i)
        if (job->pgrp.procs.data[i].stat == PROC_STOP)
            if (job->pgrp.procs.data[i].stop_sig == SIGTSTP)
                return true;

    return false;
}

static bool job_interrupted(jc_job *job) {
    for (size_t i = 0; i < job->pgrp.procs.size; ++i)
        if (job->pgrp.procs.data[i].stat == PROC_SIG)
            if (job->pgrp.procs.data[i].term_sig == SIGINT)
                return true;

    return false;
}

static bool job_signaled(jc_job *job) {
    for (size_t i = 0; i < job->pgrp.procs.size; ++i)
        if (job->pgrp.procs.data[i].stat == PROC_SIG)
            if (job->pgrp.procs.data[i].term_sig != SIGINT)
                return true;

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

void free_job_event(job_event *jev) {
    if (!jev->builtin) {
        for (size_t i = 0; i < jev->pviews.size; ++i)
            free_argv(jev->pviews.data[i].argv);
        da_free(&jev->pviews);
    }
}

void clear_job_table(void) {
    for (size_t i = 0; i < jctl.jobs.size; ++i) {
        LOG_INFO("[%d] removed", jctl.jobs.data[i].jid);
        free_job(&jctl.jobs.data[i]);
    }

    da_free(&jctl.jobs);
}

void clear_job_events(void) {
    job_event *jev;
    while ((jev = pop_job_event()))
        free_job_event(jev);

    da_free(&jevs);
}

job_table *get_jctl(void) {
    return &jctl;
}

pid_t lookup_job_pgrp(pid_t jid) {
    for (size_t i = 0; i < jctl.jobs.size; ++i)
        if (jid == jctl.jobs.data[i].jid)
            return jctl.jobs.data[i].pgrp.pgid;

    return -1;
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

void capture_pviews(jc_job *job, job_event *jev) {
    da_init(&jev->pviews);

    for (size_t i = 0; i < job->pgrp.procs.size; ++i) {
        jc_proc *proc = &job->pgrp.procs.data[i];
        proc_view *pview = da_push(&jev->pviews);

        pview->pid = proc->pid;
        pview->stat = proc->stat;
        pview->argv = copy_argv(proc->argv);

        if (proc->stat == PROC_EXIT)      pview->exit_stat = proc->exit_stat;
        else if (proc->stat == PROC_SIG)  pview->term_sig = proc->term_sig;
        else if (proc->stat == PROC_STOP) pview->stop_sig = proc->stop_sig;
    }

    jev->jid = job->jid;
    jev->stat = job->stat;
    jev->last = &jev->pviews.data[jev->pviews.size - 1];
}

da_jevent get_job_info(void) {
    da_jevent jevs;
    da_init(&jevs);

    for (size_t i = 0; i < jctl.jobs.size; ++i) {
        jc_job *job = &jctl.jobs.data[i];

        job_event *jev = da_push(&jevs);
        *jev = (job_event){
            .jid = job->jid,
            .type = JINFO
        };

        da_init(&jev->pviews);
        capture_pviews(job, jev);
    }

    return jevs;
}

void push_continue_event(jc_job *job) {
    job_event *jev = da_push(&jevs);
    *jev = (job_event){0};

    jev->type = JCONTINUED;

    capture_pviews(job, jev);
}

void push_stop_event(jc_job *job) {
    job_event *jev = da_push(&jevs);
    *jev = (job_event){0};

    jev->type = JSTOPPED;
    jev->intr_stop = job_inter_stopped(job);;

    capture_pviews(job, jev);
}

void push_exit_event(jc_job *job) {
    job_event *jev = da_push(&jevs);
    *jev = (job_event){0};

    jev->type = JEXITED;
    jev->signaled = job_signaled(job);

    if (job_interrupted(job)) {
        jev->exit_type = JEXIT_INTR;
        jev->success = false;

    } else {
        jev->exit_type = JEXIT_NORMAL;

        if (job->last->stat == PROC_EXIT) {
            jev->success = (job->last->exit_stat == EXIT_SUCCESS);
        } else
            jev->success = false;
    }

    capture_pviews(job, jev);
}

int update_job_proc(wait_event wev) {
    jc_job *job;
    jc_proc *proc;

    if (!identify_proc(wev.pid, &job, &proc))
        return -1;

    update_proc(proc, wev);

    job_stat stat = calc_job_stat(job);

    if (job->stat == JOB_RUN && stat == JOB_STOP) {
        push_stop_event(job);
        job->stat = stat;

    } else if (job->stat == JOB_STOP && stat == JOB_RUN) {
        push_continue_event(job);
        job->stat = stat;

    } else if (stat == JOB_EXIT) {
        push_exit_event(job);
        remove_job(job->jid);
    }

    return 0;
}

void queue_builtin_exit_event(pid_t jid, int status) {
    add_job_event((job_event){
        .jid = jid,
        .type = JEXITED,
        .success = (status == EXIT_SUCCESS),
        .exit_type = JEXIT_NORMAL,
        .builtin = true
    });

    xkill(getpid(), SIGCHLD);
}

void add_job(pid_t jid, pline_data *pld) {
    assert(pld);

    if (pld->procs.size == 0)
        xfatal("empty pline proc array");

    jc_job *job = da_push(&jctl.jobs);

    init_job(job);

    job->jid = jid;
    job->stat = JOB_RUN;
    job->pgrp.pgid = pld->pgid;

    for (size_t i = 0; i < pld->procs.size; ++i) {
        jc_proc *proc = da_push(&job->pgrp.procs);
        pline_proc *pproc = &pld->procs.data[i];

        proc->pid = pproc->pid;
        proc->stat = PROC_RUN;
        proc->argv = pproc->argv;

        if (i == pld->procs.size - 1)
            job->last = proc;
    }

    da_free(&pld->procs);
}
