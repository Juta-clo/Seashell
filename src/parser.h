#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>

#include <linc/array.h>
#include "lexer.h"

typedef enum {
    PS_AND_IF,
    PS_OR_IF,
    PS_NO_IF,
} ps_andor_op;

typedef enum {
    PS_Q_SINGLE,
    PS_Q_DOUBLE,
    PS_Q_NONE,
} ps_quote;

typedef enum {
    PS_CMD_SIMPLE,
    PS_CMD_SUBSH,
} ps_cmd_kind;

typedef struct ps_segment ps_segment;
typedef struct ps_word ps_word;
typedef struct ps_redir ps_redir;
typedef struct ps_cmd ps_cmd;
typedef struct ps_pline ps_pline;
typedef struct ps_andor ps_andor;
typedef struct ps_ast ps_ast;

LINC_ARRAY_TYPE(da_segment, ps_segment);
LINC_ARRAY_TYPE(da_word, ps_word);
LINC_ARRAY_TYPE(da_redir, ps_redir);
LINC_ARRAY_TYPE(da_cmd, ps_cmd);
LINC_ARRAY_TYPE(da_andor, ps_andor);

struct ps_ast {
    da_andor andors;
    bool bg;
    linc_allo allo;
    bool nested;
};

struct ps_segment {
    char *raw;
    ps_quote quote;
};

struct ps_word {
    da_segment segments;
    char *arg;
};

struct ps_redir {
    ps_word target;
    int io_num;
    int append;
    bool rdr_in;
};

struct ps_cmd {
    ps_cmd_kind kind;
    da_redir redirs;

    union {
        struct {
            da_word words;
            char **argv;
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
};

void ps_free(ps_ast *ast);
int ps_parse(da_tok *tokens, ps_ast *ast, bool print_err);

#endif
