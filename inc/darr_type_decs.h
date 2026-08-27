/* lexer.h */
struct lx_part;
struct lx_tok;

/* parser.h */
struct ps_segment;
struct ps_word;
struct ps_redir;
struct ps_cmd;
struct ps_andor;
struct ps_ast;

/* map */
struct mpair;

/* waitstat.h */
struct wait_event;

/* job_state.h */
struct jc_proc;
struct jc_pgrp;
struct jc_job;
struct job_event;
struct proc_view;

/* shell_types.h */
struct job_plan;

/* proc_view.h */
struct ps_pstat;

/* dstr.h */
struct dstr;

#define DYN_ARR_TYPES(APPLY, arg) \
    /* lexer */ \
    APPLY(arg, da_part, struct lx_part) \
    APPLY(arg, da_tok, struct lx_tok) \
    /* parser */ \
    APPLY(arg, da_segment, struct ps_segment) \
    APPLY(arg, da_word, struct ps_word) \
    APPLY(arg, da_redir, struct ps_redir) \
    APPLY(arg, da_cmd, struct ps_cmd) \
    APPLY(arg, da_andor, struct ps_andor) \
    /* misc */ \
    APPLY(arg, da_int, int) \
    APPLY(arg, da_pid, pid_t) \
    APPLY(arg, da_charp, char *) \
    APPLY(arg, da_mpair, struct mpair) \
    APPLY(arg, da_wevent, struct wait_event) \
    APPLY(arg, da_proc, struct jc_proc) \
    APPLY(arg, da_pgrp, struct jc_pgrp) \
    APPLY(arg, da_job, struct jc_job) \
    APPLY(arg, da_jevent, struct job_event) \
    APPLY(arg, da_plan, struct job_plan) \
    APPLY(arg, da_pstat, struct ps_pstat) \
    APPLY(arg, da_dstr, struct dstr) \
    APPLY(arg, da_proc_view, struct proc_view)
