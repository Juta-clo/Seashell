#include <assert.h>

#include "plan.h"
#include "utils.h"

da_plan plans = {0};

ps_andor *plan_next(job_plan *plan) {
    if (plan->index >= plan->ast->andors.size)
        return NULL;

    return &plan->ast->andors.data[plan->index++];
}

job_plan *add_plan(pid_t jid, ps_ast *ast) {
    LOG_INFO("added plan jid=%d", jid);

    job_plan *plan = linc_array_push(&plans);
    *plan = (job_plan){0};

    plan->jid = jid;
    plan->ast = ast;

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
