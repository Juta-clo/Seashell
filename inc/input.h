#ifndef INPUT_H
#define INPUT_H

#include "shell_state.h"

#define LINE_BUF 8192

#define PROMPT_SIMPLE 1
#define PROMPT_CWD 2
#define PROMPT_PS 3

typedef char * hds;

typedef enum {
    COMP_CMD,
    COMP_ARG,
    COMP_REDIR
} comp_tok;

typedef struct {
    size_t i;
    size_t start_i;
    hds glob;
} input_comp;

typedef struct {
    char **lines;
    char *saved;
    size_t i;
} input_hist;

typedef struct {
    size_t start_i;
    size_t n;
} input_del_ctx;

typedef struct {
    char *line;
    int start_row;
    int off;
    int rows, cols;
    int insert_mode;
    int start;
    input_comp comp;
    input_hist hist;
} input_editor;

typedef struct {
    int start, end;
} input_word;

extern input_editor input;

void input_init(shell_env *env);
void input_deinit(shell_env *env);

int next_word(const char *line, int start, input_word *word);
int prev_word(const char *line, int start, input_word *word);
int cur_word(const char *line, int start, input_word *word);

void drop_line(void);
char *get_line(void);

#endif
