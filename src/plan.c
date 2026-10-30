#include <assert.h>

#include "plan.h"
#include "utils.h"
#include "job_state.h"

da_plan plans = {0};

static pid_t create_job_id(void) {
    pid_t jid = 1;

    for (size_t i = 0; i < plans.size;) {
        if (jid == plans.data[i].jid) {
            ++jid;
            i = 0;
            continue;
        }

        ++i;
    }

    return jid;
}

ps_andor *plan_next(job_plan *plan) {
    if (plan->index >= plan->ast->andors.size)
        return NULL;

    return &plan->ast->andors.data[plan->index++];
}

job_plan *add_plan(ps_ast *ast) {
    job_plan *plan = linc_array_push(&plans);
    *plan = (job_plan){0};

    plan->jid = create_job_id();
    plan->ast = ast;

    LOG_INFO("added plan jid=%d", plan->jid);

    return plan;
}

void remove_plan(size_t index) {
    LOG_INFO("removed plan jid=%d", plans.data[index].jid);

    for (size_t i = 0; i < plans.data[index].exit_events.size; ++i)
        free_job_event(&plans.data[index].exit_events.data[i]);
    linc_array_free(&plans.data[index].exit_events);
    linc_array_free(&plans.data[index].exit_event_indexes);

    ps_free(plans.data[index].ast);
    free(plans.data[index].ast);

    linc_array_delete(&plans, index, 1);
}

void plan_done(pid_t jid) {
    size_t plan_i;
    job_plan *plan = lookup_plan(jid, &plan_i);
    if (!plan)
        return;

    remove_plan(plan_i);
}

job_plan *lookup_plan(pid_t jid, size_t *index) {
    for (size_t i = 0; i < plans.size; ++i) {
        if (jid == plans.data[i].jid) {
            if (index)
                *index = i;
            return &plans.data[i];
        }
    }

    return NULL;
}
