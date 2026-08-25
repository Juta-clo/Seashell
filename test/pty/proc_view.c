#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <linux/limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>

#include "darr.h"
#include "llog.h"
#include "proc_view.h"

#define PPID "PPid"
#define NAME "Name"

#define BUF_SIZE 4096

static void err_exit(char *msg) {
    LOG_ERR("%s: %m", msg);
    exit(EXIT_FAILURE);
}

static void fatal(char *msg) {
    LOG_ERR("%s", msg);
    exit(EXIT_FAILURE);
}

static char *stat_val_str(pid_t pid, char *field) {
    char stat_path[PATH_MAX];
    snprintf(stat_path, PATH_MAX, "/proc/%d/status", pid);

    FILE *stat = fopen(stat_path, "r");
    if (!stat)
        err_exit("fopen");

    static char buf[BUF_SIZE];

    char *rv;
    while ((rv = fgets(buf, BUF_SIZE, stat)))
        if (strncmp(field, buf, strlen(field)) == 0)
            break;

    if (!rv) {
        if (feof(stat))
            fatal("couldn't find specified field");

        else if (ferror(stat) && errno == ESRCH)
        /* /proc file gone; proc exited */
            goto fail;

        err_exit("fgets");
    }

    if (buf[strlen(buf) - 1] == '\n')
        buf[strlen(buf) - 1] = '\0';

    char *c = strchr(buf, ':');
    if (!c)
        err_exit("strchr");

    for (++c; *c == ' ' || *c == '\t'; ++c)
        continue;

    if (fclose(stat) == EOF)
        err_exit("fclose");
    return c;

fail:
    if (fclose(stat) == EOF)
        err_exit("fclose");
    return NULL;
}

ps_pstat *lookup_pstat(da_pstat *pstats, char *name) {
    for (size_t i = 0; i < pstats->size; ++i)
        if (strcmp(name, pstats->data[i].name) == 0)
            return &pstats->data[i];

    return NULL;
}

int child_pstat(pid_t pid, da_pstat *pstats) {
    assert(pstats);
    *pstats = (da_pstat){0};

    char child_path[PATH_MAX];
    snprintf(child_path, PATH_MAX, "/proc/%d/task/%d/children", pid, pid);

    FILE *stat = fopen(child_path, "r");
    if (!stat)
        err_exit("fopen");

    static char buf[BUF_SIZE];

    if (!fgets(buf, BUF_SIZE - 1, stat)) {
        if (feof(stat)) {
        /* file exists but empty means no children */
            fclose(stat);
            return 0;
        }

        if (ferror(stat) && errno == ESRCH)
        /* /proc file gone; proc exited */
            goto fail;

        err_exit("fgets");
    }

    char *endptr = buf;

    while (true) {
        char *start = endptr;

        long child_pid = strtol(start, &endptr, 10);
        if (child_pid == LONG_MAX || child_pid == LONG_MIN)
            fatal("strtol overflow");

        if (start == endptr)
            break;

        ps_pstat *pstat = da_push(pstats);

        pstat->pid = child_pid;

        char *child_name = stat_val_str(child_pid, NAME);
        if (!child_name)
            goto fail;

        strcpy(pstat->name, child_name);
    }

    return 0;

fail:
    if (fclose(stat) == EOF)
        err_exit("fclose");
    return -1;
}
