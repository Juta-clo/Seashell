#ifndef INPUT_H
#define INPUT_H

#include "shell_state.h"

#define PROMPT_SIMPLE 1
#define PROMPT_CWD 2
#define PROMPT_PS 3

void input_init(shell_env *env);
void input_deinit(shell_env *env);

void input_drop_line(void);
char *input_get_line(void);

#endif
