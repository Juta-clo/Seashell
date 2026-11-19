#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>

#include <linc/array.h>
#include "lexer.h"

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
} ps_cmd_kind;

typedef enum {
    PS_INFO_SUBSH = 1,
    PS_INFO_AND_IF = 2,
    PS_INFO_OR_IF  = 4,
    PS_INFO_NO_IF  = 8,
    PS_INFO_CMD   = 16,
    PS_INFO_REDIR = 32,
    PS_INFO_NAME  = 64,
    PS_INFO_ARG   = 128,
    PS_INFO_SEG   = 256,
    PS_INFO_EMPTY = 512,
} ps_kind;

typedef struct ps_segment ps_segment;
typedef struct ps_word ps_word;
typedef struct ps_redir ps_redir;
typedef struct ps_cmd ps_cmd;
typedef struct ps_pline ps_pline;
typedef struct ps_andor ps_andor;
typedef struct ps_ast ps_ast;
typedef struct ps_info ps_info;

LINC_ARRAY_TYPE(da_segment, ps_segment);
LINC_ARRAY_TYPE(da_word, ps_word);
LINC_ARRAY_TYPE(da_redir, ps_redir);
LINC_ARRAY_TYPE(da_cmd, ps_cmd);
LINC_ARRAY_TYPE(da_andor, ps_andor);
LINC_ARRAY_TYPE(da_int, int);

struct ps_ast {
    da_andor andors;
    linc_allo allo;
    bool nested;
    bool bg;
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

struct ps_cmd {
    ps_cmd_kind kind;
    da_redir redirs;
    int i, n;
    bool expanded;

    union {
        struct {
            da_word words;
            char **argv;
            int argc;
        };

        ps_ast subsh;
    };
};

struct ps_pline {
    da_cmd cmds;
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
    ps_andor *andor;
    ps_cmd *cmd;
    ps_redir *redir;
    ps_word *word;
    ps_segment *seg;
};

typedef struct {
    bool print_err;
    bool alias;
    da_tok *toks; /* lx_tokenize(); */

    /* Internal, zero initialize */
    size_t i;
    lx_tok *cur_tok;
    ps_cmd *cur_cmd;
    ps_redir *queued_redir;
    ps_andor *cur_andor;
    ps_andor_op andor_op;
    int alias_shift;
    ps_ast *ast;
    da_tok err_toks;
    bool skip_advance;
} ps_scan;

void ps_get_info(int i, ps_ast *ast, ps_info *info);
int ps_parse(ps_scan *scan, ps_ast *ast);

void ps_free(ps_ast *ast);

#endif
