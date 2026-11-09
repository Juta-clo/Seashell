#ifndef LEXER_H
#define LEXER_H

#include <stdint.h>

#include <linc/array.h>

typedef enum {
    LX_TOK_PIPE,
    LX_TOK_AMPER,
    LX_TOK_RDR_IN,
    LX_TOK_RDR_OUT,
    LX_TOK_FD_OUT,
    LX_TOK_FD_IN,
    LX_TOK_APPEND,
    LX_TOK_AND_IF,
    LX_TOK_OR_IF,
    LX_TOK_LPAREN,
    LX_TOK_RPAREN,
    LX_TOK_IO_NUM,
    LX_TOK_UNKNOWN,
    LX_TOK_WORD,
    LX_WHITE_SPACE,
    LX_EOL,
} lx_kind;

typedef enum {
    LX_Q_SINGLE,
    LX_Q_DOUBLE,
    LX_Q_NONE,
} lx_quote;

typedef enum {
    LX_FAIL = -1,
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
    int i, n;
};

typedef struct lx_part lx_part;
LINC_ARRAY_TYPE(da_part, lx_part);

struct lx_tok {
    lx_kind kind;
    bool is_num;

    union {
        da_part parts;
        int io_num;
    };

    int i;
    int n;
};

typedef struct lx_tok lx_tok;
LINC_ARRAY_TYPE(da_tok, lx_tok);

char *lx_errstr(lx_status stat);

void lx_tok_copy(lx_tok *dest, lx_tok *src);
void lx_free_tok(lx_tok *tok);
void lx_shift_toks(int shift, size_t shift_n, lx_tok *toks);

void lx_free(da_tok *tokens);
int lx_tokenize(const char *cmd, da_tok *tokens);

#endif
