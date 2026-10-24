#ifndef EXPANDER_H
#define EXPANDER_H

#include "parser.h"
#include "darr.h"

typedef enum {
    EXT_REG,
    EXT_KEY,
} ex_tok_type;

struct ex_tok {
    ex_tok_type type;
    const char *str;
    size_t str_len;
};

typedef struct ex_tok ex_tok;
DA_TYPE(da_ex_tok, ex_tok);

int ex_expand_pline(const ps_pline *pline);

#endif
