#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "builtins.h"
#include "expander.h"
#include "job_state.h"
#include "plan.h"
#include "wait_stat.h"
#include "utils.h"
#include "exec_funcs.h"
#include "shell.h"

#include <linc/xsys.h>
#include <linc/array.h>
#include <linc/string.h>

static job_table jctl = {0};
static da_jevent jevs = {0};

static void free_job(jc_job *job) {
    for (size_t i = 0; i < job->pgrp.procs.size; ++i)
        free_argv(job->pgrp.procs.data[i].argv);

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
    linc_array_delete(&jctl.jobs, i);
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

static bool same_ev_type(const proc_view *p1, const proc_view *p2) {
    return (p1->stat == p2->stat) &&
        (p2->stat == PROC_RUN ||
        (p2->stat == PROC_EXIT && p1->exit_stat == p2->exit_stat) ||
        (p1->stat == PROC_SIG && p1->term_sig == p2->term_sig) ||
        (p1->stat == PROC_STOP && p1->stop_sig == p2->stop_sig));
}

static linc_string get_pline_str(const job_event *jev, int off, int argv_off,
        bool verbose)
{
    linc_string out = LINC_STRING_INIT;
    int min_offset = 1;

    for (size_t y = 0; y < jev->pviews.size; ++y) {
        proc_view *pview = &jev->pviews.data[y];
        size_t prev_len = out.len;

        if (verbose || y == 0 || !same_ev_type(pview, &pview[-1])) {
            if (pview->stat == PROC_EXIT) {
                if (pview->exit_stat == 0)
                    linc_string_appendf(&out, "done");
                else
                    linc_string_appendf(&out, "exit %d", pview->exit_stat);
            } else if (pview->stat == PROC_RUN) {
                linc_string_append(&out, "running");
            } else if (pview->stat == PROC_SIG) {
                linc_string_appendf(&out, "%s", strsignal(pview->term_sig));
            } else if (pview->stat == PROC_STOP) {
                linc_string_appendf(&out, "%s", strsignal(pview->stop_sig));
            } else
                argv_off -= 1;

            int len = off + (out.len - prev_len);
            int off = MAX(argv_off - len, min_offset);
            linc_string_appendf(&out, "%*s", off, "");
        }

        if (verbose)
            linc_string_appendf(&out, "%d ", pview->pid);

        size_t len = argv_len(pview->argv);

        for (size_t i = 0; i < len; ++i) {
            linc_string_append(&out, pview->argv[i]);
            if (i != len - 1)
                linc_string_append(&out, " ");
        }

        if (y != jev->pviews.size - 1) {
            if (!verbose && same_ev_type(pview, &pview[1])) {
                linc_string_appendf(&out, " | ");
            } else {
                linc_string_appendf(&out, " |\n%*s", off, "");
            }
        }
    }

    return out;
}

static void display_msg(const char *msg) {
    int crow, ccol;

    if (sh_env.fg_jid == NOFG) {
        xprintty_get_cursor_pos(sh_env.pt, &crow, &ccol);

        if (ccol != 0)
            printf("\n");
    }

    printf("%s\n", msg);
}

void print_job_event(pid_t jid, const job_event *jev, bool verbose) {
    sh_env.need_prompt = true;

    linc_string msg = LINC_STRING_INIT;
    linc_string_appendf(&msg, "[%d] ", jid);

    job_plan *plan = lookup_plan(jid, NULL);
    int offset = msg.len;

    for (size_t i = 0; i < plan->exit_events.size; ++i) {
        job_event *next = &plan->exit_events.data[i];
        linc_string s = get_pline_str(next, offset, 21, verbose);
        linc_string_append(&msg, s.cstr);
        linc_string_free(&s);

        if (i != plan->exit_events.size - 1 || jev) {
            size_t index = plan->exit_event_indexes.data[i];

            if (plan->ast->andors.data[index].op == PS_AND_IF) {
                linc_string_append(&msg, " &&");
            } else if (plan->ast->andors.data[index].op == PS_OR_IF) {
                linc_string_append(&msg, " ||");
            } else {
                ASSERT(false && "should not be possible");
            }

            linc_string_appendf(&msg, "\n%*s", offset, "");
        }
    }

    if (jev) {
        linc_string s = get_pline_str(jev, offset, 21, verbose);
        linc_string_append(&msg, s.cstr);
        linc_string_free(&s);
    }

    display_msg(msg.cstr);
    LOG_INFO("%s", msg.cstr);

    linc_string_free(&msg);
}

pid_t create_job_id(void) {
    pid_t jid = 1;

    if (jctl.jobs.size == 0)
        return jid;

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

void free_job_event(job_event *jev) {
    for (size_t i = 0; i < jev->pviews.size; ++i)
        free_argv(jev->pviews.data[i].argv);
    linc_array_free(&jev->pviews);
}

void clear_job_table(void) {
    for (size_t i = 0; i < jctl.jobs.size; ++i) {
        LOG_INFO("[%d] removed", jctl.jobs.data[i].jid);
        free_job(&jctl.jobs.data[i]);
    }

    linc_array_free(&jctl.jobs);
}

void clear_job_events(void) {
    job_event *jev;
    while ((jev = pop_job_event()))
        free_job_event(jev);

    linc_array_free(&jevs);
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

    linc_array_delete(&jevs, 0);

    return &jev;
}

void add_job_event(job_event jev) {
    *linc_array_push(&jevs) = jev;
}

void capture_job_state(jc_job *job, job_event *jev) {
    jev->pviews = (da_proc_view){0};

    for (size_t i = 0; i < job->pgrp.procs.size; ++i) {
        jc_proc *proc = &job->pgrp.procs.data[i];
        proc_view *pview = linc_array_push(&jev->pviews);

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

void push_continue_event(jc_job *job) {
    job_event *jev = linc_array_push(&jevs);
    *jev = (job_event){0};

    jev->type = JCONTINUED;

    capture_job_state(job, jev);
}

void push_stop_event(jc_job *job) {
    job_event *jev = linc_array_push(&jevs);
    *jev = (job_event){0};

    jev->type = JSTOPPED;
    jev->intr_stop = job_inter_stopped(job);;

    capture_job_state(job, jev);
}

void push_exit_event(jc_job *job) {
    job_event *jev = linc_array_push(&jevs);
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

    capture_job_state(job, jev);
}

int update_job_proc(wait_event wev) {
    jc_job *job;
    jc_proc *proc;

    if (!identify_proc(wev.pid, &job, &proc))
        return -1;

    update_proc(proc, wev);

    job_stat stat = calc_job_stat(job);

    if (job->stat == JOB_RUN && stat == JOB_STOP) {
        job->stat = stat;
        push_stop_event(job);

    } else if (job->stat == JOB_STOP && stat == JOB_RUN) {
        job->stat = stat;
        push_continue_event(job);

    } else if (stat == JOB_EXIT) {
        push_exit_event(job);
        remove_job(job->jid);
    }

    return 0;
}

bool set_job_continue(pid_t jid, job_event *jev) {
    size_t i = get_job_index(jid);
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

void queue_builtin_exit_event(pid_t jid, char **argv, int status) {
    job_event jev = {
        .jid = jid,
        .type = JEXITED,
        .stat = JOB_EXIT,
        .success = (status == EXIT_SUCCESS),
        .exit_type = JEXIT_NORMAL,
    };

    proc_view* pview = linc_array_push(&jev.pviews);
    *pview = (proc_view){
        .stat = PROC_EXIT,
        .exit_stat = status
    };

    pview->argv = copy_argv(argv);
    jev.last = &jev.pviews.data[0];
    add_job_event(jev);

    xkill(getpid(), SIGCHLD);
}

void add_job(pid_t jid, pline_data *pld) {
    assert(pld);

    if (pld->procs.size == 0)
        xfatal("empty pline proc array");

    jc_job *job = linc_array_push(&jctl.jobs);
    *job = (jc_job){0};

    job->jid = jid;
    job->stat = JOB_RUN;
    job->pgrp.pgid = pld->pgid;

    for (size_t i = 0; i < pld->procs.size; ++i) {
        jc_proc *proc = linc_array_push(&job->pgrp.procs);
        pline_proc *pproc = &pld->procs.data[i];

        proc->pid = pproc->pid;
        proc->stat = PROC_RUN;
        proc->argv = pproc->argv;

        if (i == pld->procs.size - 1)
            job->last = proc;
    }

    linc_array_free(&pld->procs);
}

int launch_job(ps_pline *pline, bool bg, pid_t jid) {
    if (ex_expand_pline(pline) == -1)
        return -1;

    if (!bg) {
        if (sh_env.fg_jid == NOFG)
            printty_stop(sh_env.pt);

        sh_env.fg_jid = jid;

        xtcgetattr(sh_env.tty_fd, &sh_env.tty_state);

        job_plan *plan = lookup_plan(jid, NULL);
        ASSERT(plan);

        if (plan->tty_state_saved) {
            xtcsetattr(sh_env.tty_fd, TCSANOW, &plan->tty_state);
            plan->tty_state_saved = false;
        }
    }

    bool handled = false;
    if (pline->cmds.size == 1 && pline->cmds.data[0].redirs.size == 0 && !bg) {
        sh_builtin *builtin = get_builtin(pline->cmds.data[0].argv[0]);

        if (builtin) {
            LOG_INFO("[%d] Started %s (no fork)", jid, builtin->name);

            int status = builtin->func(pline->cmds.data[0].argv, &sh_env);

            /* fg "replaces" itself with the existing job. Because we
             * don't queue fg's exit event we only reclaim the termianl
             * when the job that was fg'd exits (or stops) */
            if (strcmp("fg", builtin->name) != 0 || status == EXIT_FAILURE) {
                LOG_INFO("queued \"%s\" exit event", builtin->name);
                queue_builtin_exit_event(jid, pline->cmds.data[0].argv, status);
            } else {
                sh_env.fg_pending = jid;
            }

            handled = true;
        }
    }

    if (!handled) {
        pline_data pld = exec_pline(pline, bg);
        add_job(jid, &pld);

        LOG_INFO("[%d] Started %s %d", jid, pline->cmds.data[0].argv[0],
                pld.pgid);
    }

    return 0;
}
