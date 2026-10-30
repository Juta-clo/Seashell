#ifndef PLAN_H
#define PLAN_H

#include <termios.h>

#include "job_state.h"
#include "parser.h"
#include "utils.h"

typedef struct job_plan job_plan;
LINC_ARRAY_TYPE(da_plan, job_plan);

struct job_plan {
    pid_t jid;
    bool tty_state_saved;
    struct termios tty_state;
    size_t index;
    ps_ast *ast;
    da_jevent exit_events;
    da_int exit_event_indexes;
};

extern da_plan plans;

ps_andor *plan_next(job_plan *plan);
job_plan *add_plan(ps_ast *ast);
void remove_plan(size_t index);
void plan_done(pid_t jid);
job_plan *lookup_plan(pid_t jid, size_t *index);

#endif
