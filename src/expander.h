#ifndef EXPANDER_H
#define EXPANDER_H

#include <linc/array.h>
#include "parser.h"
#include "utils.h"

#define EXPAND_REDIR   1
#define EXPAND_CMD     2
#define EXPAND_COMP    3

typedef enum {
    EXT_REGULAR,
    EXT_VARIABLE,
    EXT_CMD_SUB,
    EXT_PROC_SUB,
    EXT_ERROR,
} ex_tok_type;

struct ex_tok {
    ex_tok_type type;
    const char *str;
    size_t str_len;
};

typedef struct ex_tok ex_tok;
LINC_ARRAY_TYPE(da_ex_tok, ex_tok);

void text_has_glob(char *text, size_t n, bool *has_glob);
void expand_word_input(ps_word *word);
int expand_word(ps_word *word, da_charp *argv);
int expand_cmd(ps_cmd *cmd);
void close_proc_subs(ps_cmd *cmd);

#endif
