#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "jobs.h"
#include "shell.h"
#include "wait_stat.h"
#include "utils.h"

#include <linc/xsys.h>
#include <linc/array.h>
#include <linc/string.h>

static job_table jctl = {0};
static da_jevent jevs = {0};

static void free_job(jc_job *job) {
    for (size_t i = 0; i < job->pgrp.procs.size; ++i)
        free(job->pgrp.procs.data[i].cmd);

    linc_array_free(&job->pgrp.procs);
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

static bool identify_proc(int pid, jc_job **job, jc_proc** proc) {
    ASSERT(job && proc);

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

static size_t get_job_index(int jid) {
    for (size_t i = 0; i < jctl.jobs.size; ++i)
        if (jid == jctl.jobs.data[i].jid)
            return i;

    return SIZE_MAX;
}

static void remove_job (int jid) {
    size_t i = get_job_index(jid);
    if (i == SIZE_MAX)
        fatal("bad jctl state");

    free_job(&jctl.jobs.data[i]);
    linc_array_delete(&jctl.jobs, i, 1);
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

jc_job *job_lookup(int jid) {
    size_t i = get_job_index(jid);
    if (i == SIZE_MAX)
        return nullptr;

    return &jctl.jobs.data[i];
}

void job_event_free(job_event *jev) {
    if (!jev)
        return;

    for (size_t i = 0; i < jev->pviews.size; ++i)
        free(jev->pviews.data[i].cmd);
    linc_array_free(&jev->pviews);
}

void job_clear(void) {
    job_event *jev;
    while ((jev = job_event_pop()))
        job_event_free(jev);

    linc_array_free(&jevs);

    for (size_t i = 0; i < jctl.jobs.size; ++i) {
        LOG_INFO("[%d] removed", jctl.jobs.data[i].jid);
        free_job(&jctl.jobs.data[i]);
    }

    linc_array_free(&jctl.jobs);
}

job_table *job_get_table(void) {
    return &jctl;
}

pid_t job_lookup_pgrp(int jid) {
    for (size_t i = 0; i < jctl.jobs.size; ++i)
        if (jid == jctl.jobs.data[i].jid)
            return jctl.jobs.data[i].pgrp.pgid;

    return -1;
}

job_event *job_event_pop(void) {
    static job_event jev;

    if (jevs.size == 0)
        return nullptr;

    jev = jevs.data[0];

    linc_array_delete(&jevs, 0, 1);

    return &jev;
}

static void add_job_event(job_event jev) {
    *linc_array_push(&jevs) = jev;
}

static void capture_job_state(jc_job *job, job_event *jev) {
    jev->jid = job->jid;
    jev->pviews = (da_proc_view){0};

    for (size_t i = 0; i < job->pgrp.procs.size; ++i) {
        jc_proc *proc = &job->pgrp.procs.data[i];
        proc_view *pview = linc_array_push(&jev->pviews);

        pview->pid = proc->pid;
        pview->stat = proc->stat;
        pview->cmd = xstrdup(proc->cmd);

        if (proc->stat == PROC_EXIT) {
            pview->exit_stat = proc->exit_stat;
        } else if (proc->stat == PROC_SIG) {
            pview->term_sig = proc->term_sig;
        } else if (proc->stat == PROC_STOP) {
            pview->stop_sig = proc->stop_sig;
        }

        if (i == jev->pviews.size - 1)
            jev->last = pview;
    }
}

void job_hangup(void) {
    for (size_t i = 0; i < jctl.jobs.size; ++i) {
        jc_job *job = &jctl.jobs.data[i];

        if (getpgrp() == job->pgrp.pgid || job->pgrp.pgid <= 1) {
            LOG_ERR("unexpected pgid %d", job->pgrp.pgid);
            break;
        }

        if (kill(-job->pgrp.pgid, SIGHUP) == -1) {
            LOG_ERR("kill: %m");
            break;
        }

        if (kill(-job->pgrp.pgid, SIGCONT) == -1 && errno != ESRCH) {
            LOG_ERR("kill: %m");
            break;
        }
    }
}

da_jevent job_get_snapshot(void) {
    da_jevent jevs = {0};

    for (size_t i = 0; i < jctl.jobs.size; ++i) {
        jc_job *job = &jctl.jobs.data[i];

        job_event *jev = linc_array_push(&jevs);
        *jev = (job_event){
            .jid = job->jid,
            .type = JINFO,
            .pviews = {0}
        };

        capture_job_state(job, jev);
    }

    return jevs;
}

int job_update(wait_event wev) {
    jc_job *job;
    jc_proc *proc;

    if (!identify_proc(wev.pid, &job, &proc))
        fatal("bad jctl state");

    update_proc(proc, wev);

    job_stat stat = calc_job_stat(job);
    job_event jev;

    if (job->stat == JOB_RUN && stat == JOB_STOP) {
        jev.type = JSTOPPED;
        jev.intr_sig = job_inter_stopped(job);;
    } else if (job->stat == JOB_STOP && stat == JOB_RUN) {
        jev.type = JCONTINUED;
    } else if (stat == JOB_EXIT) {
        jev.type = JEXITED;
        jev.signaled = job_signaled(job);
        jev.intr_sig = job_interrupted(job);
    }

    if (stat != job->stat) {
        job->stat = stat;
        capture_job_state(job, &jev);
        add_job_event(jev);
    }

    if (job->stat == JOB_EXIT)
        remove_job(job->jid);

    return 0;
}

bool job_set_running(int jid, job_event *jev) {
    size_t i = get_job_index(jid);
    if (i == SIZE_MAX)
        fatal("bad jctl state");

    jc_job *job = &jctl.jobs.data[i];
    bool change = job->stat == JOB_STOP;

    job->stat = JOB_RUN;

    for (size_t i = 0; i < job->pgrp.procs.size; ++i)
        if (job->pgrp.procs.data[i].stat == PROC_STOP)
            job->pgrp.procs.data[i].stat = PROC_RUN;

    if (change) {
        *jev = (job_event){ .type = JCONTINUED };
        capture_job_state(job, jev);
    }

    return change;
}

void job_queue_builtin_exit(int jid, char *cmd, int status) {
    job_event jev = {
        .jid = jid,
        .type = JEXITED
    };

    proc_view* pview = linc_array_push(&jev.pviews);
    *pview = (proc_view){
        .stat = PROC_EXIT,
        .exit_stat = status
    };

    pview->cmd = xstrdup(cmd);
    jev.last = &jev.pviews.data[0];
    add_job_event(jev);

    xkill(getpid(), SIGCHLD);
}

int job_new_id(void) {
    int jid = 1;

    for (size_t i = 0; i < jctl.jobs.size;) {
        if (jid == jctl.jobs.data[i].jid) {
            ++jid;
            i = 0;
            continue;
        }

        ++i;
    }

    return jid;
}

void job_add(int jid, sh_pgrp *pld) {
    ASSERT(pld);

    if (pld->procs.size == 0)
        fatal("empty pline proc array");

    jc_job *job = linc_array_push(&jctl.jobs);
    *job = (jc_job){0};

    job->jid = jid;
    job->stat = JOB_RUN;
    job->pgrp.pgid = pld->pgid;
    xtcgetattr(env.tty_fd, &job->tio);

    for (size_t i = 0; i < pld->procs.size; ++i) {
        jc_proc *proc = linc_array_push(&job->pgrp.procs);
        sh_proc *pproc = &pld->procs.data[i];

        proc->pid = pproc->pid;
        proc->stat = PROC_RUN;
        proc->cmd = pproc->cmd;

        if (i == pld->procs.size - 1)
            job->last = proc;
    }

    linc_array_free(&pld->procs);
}
