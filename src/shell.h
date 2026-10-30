#ifndef SHELL_H
#define SHELL_H

#include "linc/string.h"

void shell_sleep(void);
linc_string shell_get_prompt(void);
int shell_run(const char *line);

#endif
