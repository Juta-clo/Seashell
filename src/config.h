#ifndef CONFIG_H
#define CONFIG_H

#include <linux/limits.h>
#include <stdio.h>

#include <linc/string.h>

extern char config_path[PATH_MAX];

FILE *config_file_open(void);
void parse_config(void);
int config_field_prompt(linc_string *prompt);
void config_init(void);

#endif
