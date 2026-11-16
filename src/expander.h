#ifndef EXPANDER_H
#define EXPANDER_H

#include <linc/array.h>
#include "parser.h"
#include "utils.h"

#define EXPAND_REDIR   1
#define EXPAND_CMD     2
#define EXPAND_COMP    3
#define EXPAND_BUILTIN 4

typedef enum {
    EXT_REGULAR,
    EXT_VARIABLE,
    EXT_CMD_SUB,
} ex_tok_type;

struct ex_tok {
    ex_tok_type type;
    const char *str;
    size_t str_len;
};

typedef struct ex_tok ex_tok;
LINC_ARRAY_TYPE(da_ex_tok, ex_tok);

int expand_word(ps_word *word, da_charp *argv, int type);
int expand_cmd(ps_cmd *cmd, int type);

#endif
