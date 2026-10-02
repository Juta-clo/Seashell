#ifndef INPUT_H
#define INPUT_H

#include "shell_state.h"

#define LINE_BUF 8192

#define PROMPT_SIMPLE 1
#define PROMPT_CWD 2

typedef struct {
    char *line;
    int init_col;
    int init_row, ccol;
    int rows, cols;
    bool need_line;
    bool prompt_drawn;
} input_editor;

extern input_editor input;

char *get_line(void);
void display_prompt(int flags);
void input_init(shell_env *env);
void input_deinit(shell_env *env);

#endif
