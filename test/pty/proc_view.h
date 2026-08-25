#ifndef PROC_VIEW_H
#define PROC_VIEW_H

#include <sys/types.h>
#include <linux/limits.h>

#include "darr.h"

struct ps_pstat {
    pid_t pid;
    char name[PATH_MAX];
};

typedef struct ps_pstat ps_pstat;

ps_pstat *lookup_pstat(da_pstat *pstats, char *name);
int child_pstat(pid_t pid, da_pstat *pstats);

#endif
