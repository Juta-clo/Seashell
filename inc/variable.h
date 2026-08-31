#ifndef VARIABLE_H
#define VARIABLE_H

#define VAR_KEY_SIZE 4096

#include "map.h"

typedef struct {
    char *value;
    bool export;
} sh_var;

int add_var(map_t *vars, char *key, char *value);
sh_var *lookup_var(map_t *vars, const char *key);
int delete_var(map_t *vars, char *key);

#endif
