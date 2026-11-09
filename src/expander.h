#ifndef EXPANDER_H
#define EXPANDER_H

#include <linc/array.h>
#include "linc/string.h"
#include "parser.h"
#include "utils.h"

#define EXPAND_REDIR 1
#define EXPAND_CMD   2
#define EXPAND_COMP  3

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
LINC_ARRAY_TYPE(da_ex_tok, ex_tok);

int expand_word(ps_word *word, da_charp *argv, int type);
linc_string expand_seg(const char *seg, bool *has_glob);
int expand_pline(const ps_pline *pline);

#endif
