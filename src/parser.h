#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>

#include <linc/array.h>
#include "lexer.h"
#include "utils.h"

typedef enum {
    PS_NO_IF,
    PS_AND_IF,
    PS_OR_IF,
} ps_andor_op;

typedef enum {
    PS_Q_NONE,
    PS_Q_SINGLE,
    PS_Q_DOUBLE,
} ps_quote;

typedef enum {
    PS_CMD_SIMPLE,
    PS_CMD_SUBSH,
    PS_CMD_IF,
    PS_CMD_WHILE,
    PS_CMD_FOR,
} ps_cmd_kind;

typedef enum {
    PS_INFO_SUBSH  = 1,
    PS_INFO_CHAIN  = 2,
    PS_INFO_AND_IF = 4,
    PS_INFO_OR_IF  = 8,
    PS_INFO_NO_IF  = 16,
    PS_INFO_CMD    = 32,
    PS_INFO_REDIR  = 64,
    PS_INFO_NAME   = 128,
    PS_INFO_ARG    = 256,
    PS_INFO_SEG    = 512,
    PS_INFO_EMPTY  = 1024,
} ps_kind;

typedef struct ps_segment ps_segment;
typedef struct ps_word ps_word;
typedef struct ps_redir ps_redir;
typedef struct ps_cmd ps_cmd;
typedef struct ps_pline ps_pline;
typedef struct ps_andor ps_andor;
typedef struct ps_ast ps_ast;
typedef struct ps_chain ps_chain;
typedef struct ps_info ps_info;
typedef struct ps_simple ps_simple;
typedef struct ps_if ps_if;
typedef struct ps_while ps_while;
typedef struct ps_for ps_for;

LINC_ARRAY_TYPE(da_segment, ps_segment);
LINC_ARRAY_TYPE(da_word, ps_word);
LINC_ARRAY_TYPE(da_redir, ps_redir);
LINC_ARRAY_TYPE(da_cmd, ps_cmd);
LINC_ARRAY_TYPE(da_andor, ps_andor);
LINC_ARRAY_TYPE(da_ast, ps_ast);
LINC_ARRAY_TYPE(da_chain, ps_chain);

struct ps_simple {
    da_word words;
    char **argv;
    int argc;
};

struct ps_ast {
    da_chain chains;
    linc_allo allo;
};

struct ps_chain {
    da_andor andors;
    int bg;
    int i, n;
};

struct ps_segment {
    char *raw;
    ps_quote quote;
    int i, n;
};

struct ps_word {
    char *arg;
    int i, n;
    da_segment segments;
    da_int proc_sub_fds;
};

struct ps_redir {
    int io_num;
    int append;
    bool rdr_in;
    bool fd_dup;

    union {
        ps_word target;
        int fd_target;
    };
};

struct ps_if {
    ps_ast cond;
    ps_ast body;
};

struct ps_while {
    ps_ast cond;
    ps_ast body;
};

struct ps_for {
    ps_word ind;
    da_word vals;
    ps_ast body;
    da_charp parts;
};

struct ps_cmd {
    ps_cmd_kind kind;
    da_redir redirs;
    int i, n;

    union {
        ps_simple simple;
        ps_ast subsh;
        ps_if if_st;
        ps_while while_st;
        ps_for for_st;
    };
};

struct ps_pline {
    da_cmd cmds;
    int i, n;
};

struct ps_andor {
    ps_pline pline;
    ps_andor_op op;
    int i, n;
};

struct ps_info {
    uint64_t kinds;
    linc_allo allo;

    ps_ast *subsh;
    ps_chain *chain;
    ps_andor *andor;
    ps_cmd *cmd;
    ps_redir *redir;
    ps_word *word;
    ps_segment *seg;
};

typedef struct {
    bool print_err;
    bool alias;
    linc_allo allo;
    da_tok err_toks;
    const da_tok *toks;
} ps_scan;

int ps_parse(ps_scan *scan, ps_ast *ast);
void ps_free(ps_ast *ast);

void ps_get_info(int i, ps_ast *ast, ps_info *info);

#endif
