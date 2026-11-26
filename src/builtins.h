#ifndef BUILTINS_H
#define BUILTINS_H

#include "state.h"

#define BUILTIN_COUNT 16

typedef int (*builtin_func)(char **, shell_env *);

typedef struct {
    const char *name;
    builtin_func func;
} sh_builtin;

sh_builtin *get_builtin(const char *arg);

#endif
