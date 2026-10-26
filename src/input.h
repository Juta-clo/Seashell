#ifndef INPUT_H
#define INPUT_H

#include "shell.h"

#define PROMPT_SIMPLE 1
#define PROMPT_CWD 2
#define PROMPT_PS 3

typedef struct {
    int start, end;
} input_word;

int next_word(const char *line, int start, input_word *word);
int prev_word(const char *line, int start, input_word *word);
int cur_word(const char *line, int start, input_word *word);

void input_init(shell_env *env);
void input_deinit(shell_env *env);

void input_drop_line(void);
char *input_get_line(void);

#endif
