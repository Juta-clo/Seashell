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

struct ps_segment {
    char *raw;
    ps_quote quote;
};

typedef struct ps_segment ps_segment;
LINC_ARRAY_TYPE(da_segment, ps_segment);

struct ps_word {
    da_segment segments;
    char *arg;
};

typedef struct ps_word ps_word;
LINC_ARRAY_TYPE(da_word, ps_word);

struct ps_redir {
    ps_word target;
    int io_num;
    int append;
};

typedef struct ps_redir ps_redir;
LINC_ARRAY_TYPE(da_redir, ps_redir);

struct ps_cmd {
    da_word words;
    da_redir redirs;
    char **argv;
};

typedef struct ps_cmd ps_cmd;
LINC_ARRAY_TYPE(da_cmd, ps_cmd);

struct ps_pline {
    da_cmd cmds;
};

typedef struct ps_pline ps_pline;

struct ps_andor {
    ps_pline pline;
    ps_andor_op op;
};

typedef struct ps_andor ps_andor;
LINC_ARRAY_TYPE(da_andor, ps_andor);

struct ps_ast {
    da_andor andors;
    bool bg;
    linc_allo allo;
};

typedef struct ps_ast ps_ast;

void ps_free(ps_ast *ast);
int ps_parse(da_tok *tokens, ps_ast *ast);

#endif
