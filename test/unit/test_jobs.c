#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "unity_fixture.h"
#include "job_state.h"
#include "exec_funcs.h"
#include "wait_stat.h"

TEST_GROUP(jobs);

/************ Shared utils ************/

#define EXP_DA(DA_T, ELEM_T, ...) \
    ((DA_T){ \
        .data = (ELEM_T[]) { __VA_ARGS__ }, \
        .size = sizeof((ELEM_T[]) { __VA_ARGS__ }) / sizeof(ELEM_T) \
     })

/* wait events */

#define WEVS(...) \
    EXP_DA(da_wevent, wait_event, __VA_ARGS__)

#define PCONT(_pid) \
    ((wait_event){ .pid = _pid, .type = PCONTINUED })

#define PSTOP(_pid) \
    ((wait_event){ .pid = _pid, .type = PSTOPPED })

#define PSIG(_pid, _term_sig) \
    ((wait_event){ .pid = _pid, .type = PSIGNALED, .term_sig = _term_sig })

#define PEXIT(_pid, _exit_stat) \
    ((wait_event){ .pid = _pid, .type = PEXITED, .exit_stat = _exit_stat })

#define PEXIT_OK(_pid) \
    ((wait_event){ .pid = _pid, .type = PEXITED, .exit_stat = 0 })

#define PEXIT_FAIL(_pid) \
    ((wait_event){ .pid = _pid, .type = PEXITED, .exit_stat = 1 })

#define STAT(_exit_stat) _exit_stat
#define SIG(_term_sig) _term_sig

/* job events */

#define JEVS(...) \
    EXP_DA(da_jevent, job_event, __VA_ARGS__)

#define JCONT(_jid) \
    ((job_event){ .jid = _jid, .type = JCONTINUED })

#define JSTOP(_jid) \
    ((job_event){ .jid = _jid, .type = JSTOPPED })

#define JEXIT(_jid) \
    ((job_event){ .jid = _jid, .type = JEXITED })

/* jctl structure */

#define PROC(_pid, _stat) \
    ((exp_proc){ .pid = _pid, .stat = _stat })

#define PROCS(...) \
    EXP_DA(exp_da_proc, exp_proc, __VA_ARGS__)

#define PGRP(_pgid, ...) \
    ((exp_pgrp){ .pgid = _pgid, .procs = PROCS(__VA_ARGS__) })

#define JOB(_jid, _stat, _pgrp) \
    ((exp_job){ .jid = _jid, .stat = _stat, .pgrp = _pgrp })

#define JOBS(...) \
    EXP_DA(exp_da_job, exp_job, __VA_ARGS__)

#define JRUN_1P(_jid, _pid, ...) \
    JOB(_jid, JOB_RUN, PGRP(_pid, PROC(PID(_pid), PROC_RUN)))

#define JSTOP_1P(_jid, _pid, ...) \
    JOB(_jid, JOB_STOP, PGRP(_pid, PROC(PID(_pid), PROC_STOP)))

#define JRUN_2P(_jid, _pid, ...) \
    JOB(_jid, JOB_RUN, \
        PGRP(_pid, \
            PROC(_pid, PROC_RUN), \
            PROC(_pid + 1, PROC_RUN) \
        ) \
    )

#define JSTOP_2P(_jid, _pid, ...) \
    JOB(_jid, JOB_STOP, \
        PGRP(_pid, \
            PROC(_pid, PROC_STOP), \
            PROC(_pid + 1, PROC_STOP) \
        ) \
    )

#define JID(_jid) _jid
#define PGID(_pgid) _pgid
#define PID(_pid) _pid

#define TABLE(...) \
    ((exp_job_table){ .jobs = JOBS(__VA_ARGS__) })

typedef struct {
    pid_t pid;
    proc_stat stat;
} exp_proc;

typedef struct {
    exp_proc *data;
    size_t size;
} exp_da_proc;

typedef struct {
    pid_t pgid;
    exp_da_proc procs;
} exp_pgrp;

typedef struct {
    pid_t jid;
    exp_pgrp pgrp;
    job_stat stat;
} exp_job;

typedef struct {
    exp_job *data;
    size_t size;
} exp_da_job;

typedef struct {
    exp_da_job jobs;
} exp_job_table;

void validate_proc(exp_proc *exp, jc_proc *proc) {
    TEST_ASSERT_EQUAL(exp->pid, proc->pid);
    TEST_ASSERT_EQUAL(exp->stat, proc->stat);
}

void validate_pgrp(exp_pgrp *exp, jc_pgrp *pgrp) {

    TEST_ASSERT_EQUAL(exp->pgid, pgrp->pgid);
    TEST_ASSERT_EQUAL_size_t(exp->procs.size, pgrp->procs.size);

    for (size_t i = 0; i < pgrp->procs.size; ++i)
        validate_proc(&exp->procs.data[i], &pgrp->procs.data[i]);
}

void validate_job(exp_job *exp, jc_job *job) {

    TEST_ASSERT_EQUAL(exp->jid, job->jid);
    TEST_ASSERT_EQUAL(exp->stat, job->stat);
    TEST_ASSERT_NOT_NULL(job->last);

    validate_pgrp(&exp->pgrp, &job->pgrp);
}

void validate_job_table(exp_job_table *exp, job_table *jctl) {

    TEST_ASSERT_EQUAL_size_t(exp->jobs.size, jctl->jobs.size);

    for (size_t i = 0; i < jctl->jobs.size; ++i)
        validate_job(&exp->jobs.data[i], &jctl->jobs.data[i]);
}

#define JEV(_jid, _type) \
    (job_event){ .jid = _jid, .type = _type }

void validate_job_events(da_jevent *exp) {

    for (size_t i = 0; i < exp->size; ++i) {
        job_event *jev = pop_job_event();

        TEST_ASSERT_NOT_NULL(jev);
        TEST_ASSERT_EQUAL(exp->data[i].jid, jev->jid);
        TEST_ASSERT_EQUAL(exp->data[i].type, jev->type);

        free_job_event(jev);
    }

    TEST_ASSERT_NULL(pop_job_event());
}

#define VA_COUNT_I(_1, _2,_3, _4, _5, _6, _7, _8, N, ...) N

#define VA_COUNT(...) \
    VA_COUNT_I(__VA_ARGS__ __VA_OPT__(,) 8, 7, 6, 5, 4, 3, 2, 1, 0)

#define PPROC(_pid) \
    (pline_proc){ \
        .pid = _pid, \
        .argv = ({\
            char **p = calloc(1, sizeof(char *)); \
            p[0] = NULL; \
            p; \
        }) \
    }

#define apply_wevs(...) \
    do { \
        da_wevent *wevs = &WEVS(__VA_ARGS__); \
        for (size_t i = 0; i < wevs->size; ++i) \
            TEST_ASSERT_NOT_EQUAL(-1, update_job_proc(wevs->data[i])); \
    } while (false)

void add_one_proc_job(pid_t jid, pid_t pid) {
    pline_data pdata = { .pgid = pid };
    *da_push(&pdata.procs) = PPROC(pid);

    add_job(jid, &pdata);
    validate_job_table(&TABLE(JRUN_1P(jid, pid)), get_jctl());
    TEST_ASSERT_NULL(pop_job_event());
}

void add_two_proc_job(pid_t jid, pid_t pid) {
    pline_data pdata = { .pgid = pid };
    *da_push(&pdata.procs) = PPROC(pid);
    *da_push(&pdata.procs) = PPROC(pid + 1);

    add_job(jid, &pdata);
    validate_job_table(&TABLE(JRUN_2P(jid, pid)), get_jctl());
    TEST_ASSERT_NULL(pop_job_event());
}

/************ Fixture ************/

TEST_SETUP(jobs) {}

TEST_TEAR_DOWN(jobs) {
    clear_job_table();
    clear_job_events();
}

/************ Tests ************/

TEST(jobs, continue_running_job) {
    add_one_proc_job(JID(1), PID(1));
    apply_wevs(PCONT(1));

    validate_job_table(&TABLE(JRUN_1P(JID(1), PID(1))), get_jctl());
    TEST_ASSERT_NULL(pop_job_event());
}

TEST(jobs, one_proc_job_exit) {
    add_one_proc_job(JID(1), PID(1));
    apply_wevs(PEXIT_OK(1));

    validate_job_table(&(exp_job_table){0}, get_jctl());
    validate_job_events(&JEVS(JEXIT(JID(1))));
}

TEST(jobs, one_proc_job_stop) {
    add_one_proc_job(JID(1), PID(1));
    apply_wevs(PSTOP(1));

    validate_job_table(&TABLE(JSTOP_1P(JID(1), PID(1))), get_jctl());
    validate_job_events(&JEVS(JSTOP(JID(1))));
}

TEST(jobs, one_proc_job_cont) {
    add_one_proc_job(JID(1), PID(1));
    apply_wevs(PSTOP(1), PCONT(1));

    validate_job_table(&TABLE(JRUN_1P(JID(1), PID(1))), get_jctl());
    validate_job_events(&JEVS(JSTOP(JID(1)), JCONT(JID(1))));
}

TEST(jobs, two_proc_job_exit) {
    add_two_proc_job(JID(1), PID(10));
    apply_wevs(PEXIT_OK(10), PEXIT_OK(11));

    validate_job_table(&(exp_job_table){0}, get_jctl());
    validate_job_events(&JEVS(JEXIT(JID(1))));
}

TEST(jobs, two_proc_job_stop) {
    add_two_proc_job(JID(1), PID(10));
    apply_wevs(PSTOP(10), PSTOP(11));

    validate_job_table(&TABLE(JSTOP_2P(JID(1), PID(10))), get_jctl());
    validate_job_events(&JEVS(JSTOP(JID(1))));
}

TEST(jobs, two_proc_job_cont) {
    add_two_proc_job(JID(1), PID(10));
    apply_wevs(PSTOP(10), PSTOP(11), PCONT(10), PCONT(11));

    validate_job_table(&TABLE(JRUN_2P(JID(1), PID(10))), get_jctl());
    validate_job_events(&JEVS(JSTOP(1), JCONT(1)));
}

/************ Test runner ************/

TEST_GROUP_RUNNER(jobs) {
    LOG_INFO(""); LOG_INFO("STARTING TEST MODULE: JOBS"); LOG_INFO("");
    RUN_TEST_CASE(jobs, continue_running_job);
    RUN_TEST_CASE(jobs, one_proc_job_exit);
    RUN_TEST_CASE(jobs, one_proc_job_stop);
    RUN_TEST_CASE(jobs, one_proc_job_cont);
    RUN_TEST_CASE(jobs, two_proc_job_exit);
    RUN_TEST_CASE(jobs, two_proc_job_stop);
    RUN_TEST_CASE(jobs, two_proc_job_cont);
}
