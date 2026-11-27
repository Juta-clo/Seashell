#ifndef LEXER_H
#define LEXER_H

#include <stdint.h>

#include "linc/array.h"

typedef enum {
    LX_SP_NONE,
    LX_SP_SIMPLE,
    LX_SP_IF,
    LX_SP_ELSE,
    LX_SP_WHILE,
    LX_SP_FOR,
} lx_special;

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
    LX_TOK_SEMI,
    LX_TOK_LBRACE,
    LX_TOK_RBRACE,
    LX_TOK_WORD,
    LX_TOK_UNKNOWN,
    LX_CMD_SUB_START,
    LX_PROC_SUB_START,
    LX_WHITE_SPACE,
    LX_EOF,
} lx_kind;

typedef enum {
    LX_Q_SINGLE,
    LX_Q_DOUBLE,
    LX_Q_NONE,
} lx_quote;

typedef enum {
    LX_FAIL = -1,
    LX_OK,
    LX_ERRNOENDQUOTE,
    LX_ERREMPTYESC,
    LX_ERRMISSINGPARAEN
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
    lx_special spec;
    bool is_num;
    int i, n;

    union {
        da_part parts;
        int io_num;
    };
};

typedef struct lx_tok lx_tok;
LINC_ARRAY_TYPE(da_tok, lx_tok);

#define PREFIX_ALL(p, ...) \
    CAT(PREFIX_, NARGS(__VA_ARGS__))(p, __VA_ARGS__)

#define TOK_IN(_kind, ...) \
    tok_kind_in((_kind), \
        (lx_kind[]){ __VA_ARGS__ }, \
        sizeof((lx_kind[]){ __VA_ARGS__ }) / sizeof(lx_kind) \
    )

static inline bool tok_kind_in(lx_kind kind, lx_kind *vals, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (kind == vals[i])
            return true;

    return false;
}

int lx_get_rparen(const char *str, size_t n);
int lx_tokenize(const char *cmd, size_t n, da_tok *tokens);

void lx_free_tok(lx_tok *tok);
void lx_free(da_tok *tokens);
void lx_tkcpy(lx_tok *dest, lx_tok *src, linc_allo *allo);
char *tok_word_str(lx_tok *tok);
char *tok_str(lx_kind kind);
bool lx_is_redir(lx_kind kind);
void lx_shift_toks(int shift, size_t shift_n, lx_tok *toks);
char *lx_errstr(lx_status stat);
void log_toks(da_tok *toks);

#endif
