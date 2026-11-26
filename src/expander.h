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

typedef struct {
    ex_tok_type type;
    const char *str;
    size_t str_len;
} ex_tok;

typedef struct {
    int io_num;
    int append;
    bool rdr_in;
    bool fd_dup;

    union {
        char *target;
     /* char *fd_target; */
        int fd_target;
    };
} ex_redir;

LINC_ARRAY_TYPE(da_ex_tok, ex_tok);
LINC_ARRAY_TYPE(da_ex_redir, ex_redir);

typedef struct {
    ps_cmd_kind kind;
    da_ex_redir redirs;
    da_int proc_sub_fds;

    union {
        struct {
            da_charp argv;
            int argc;
        };

        struct {
            char *ind;
            da_charp vals;
        };
    };
} ex_res;

void text_has_glob(char *text, size_t n, bool *has_glob);
void expand_word_input(ps_word *word);
int expand_word(ps_word *word, da_charp *argv, ex_res *res);
int expand_cmd(ps_cmd *cmd, ex_res *res);
void expand_free(ex_res *res);

#endif
