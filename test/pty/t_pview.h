#ifndef T_PVIEW_H
#define T_PVIEW_H

#include <sys/types.h>
#include <linux/limits.h>

#include <linc/array.h>

struct ps_pstat {
    pid_t pid;
    char name[PATH_MAX];
    char state[PATH_MAX];
};

typedef struct ps_pstat ps_pstat;
LINC_ARRAY_TYPE(da_pstat, ps_pstat);

ps_pstat *lookup_pstat(da_pstat *pstats, char *name);
int child_pstat(pid_t pid, da_pstat *pstats);

#endif
