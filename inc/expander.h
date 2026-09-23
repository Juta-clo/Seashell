#ifndef EXPANDER_H
#define EXPANDER_H

#include "parser.h"

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
DA_TYPE(da_charp, char *);

int ex_expand(const ps_ast *ast);

#endif
