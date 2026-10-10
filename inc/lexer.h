#ifndef LEXER_H
#define LEXER_H

#include <stdint.h>

#include "linc_tools.h"

typedef enum {
    LX_TOK_PIPE,
    LX_TOK_BG,
    LX_TOK_RDR_IN,
    LX_TOK_RDR_OUT,
    LX_TOK_RDR_ERR,
    LX_TOK_APPEND,
    LX_TOK_AND_IF,
    LX_TOK_OR_IF,
    LX_TOK_UNKNOWN,
    LX_TOK_WORD,
} lx_kind;

typedef enum {
    LX_Q_SINGLE,
    LX_Q_DOUBLE,
    LX_Q_NONE,
} lx_quote;

typedef enum {
    LX_OK,
    LX_ERRMEM,
    LX_ERRINPUT,
    LX_ERRNOENDQUOTE,
    LX_ERREMPTYESC
} lx_status;

typedef enum {
    LX_M_NORMAL,
    LX_M_DOUBLEQ,
    LX_M_SINGLEQ,
} lx_mode;

struct lx_part {
    char *raw;
    lx_quote quote;
    int start, end;
};

typedef struct lx_part lx_part;
DA_TYPE(da_part, lx_part);

struct lx_tok {
    lx_kind kind;
    da_part parts;
};

typedef struct lx_tok lx_tok;
DA_TYPE(da_tok, lx_tok);

char *lx_errstr(lx_status stat);
void lx_free(da_tok *tokens);
int lx_tokenize(const char *cmd, da_tok *tokens);

#endif
