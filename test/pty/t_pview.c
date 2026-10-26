#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <linux/limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>

#include <linc/array.h>
#include <linc/xsys.h>
#include "t_pview.h"
#include "t_utils.h"

#define PPID "PPid"
#define NAME "Name"
#define STATE "State"

#define BUF_SIZE 4096

static char *stat_val_str(pid_t pid, char *field) {
    char stat_path[PATH_MAX];
    snprintf(stat_path, PATH_MAX, "/proc/%d/status", pid);

    FILE *stat = fopen(stat_path, "r");
    if (!stat)
        return NULL;

    static char buf[BUF_SIZE];

    char *rv;
    while ((rv = fgets(buf, BUF_SIZE, stat)))
        if (strncmp(field, buf, strlen(field)) == 0)
            break;

    if (!rv) {
        if (feof(stat))
            xtest_fatal("couldn't find specified field");

        else if (ferror(stat) && errno == ESRCH)
        /* /proc file gone; proc exited */
            goto fail;

        xtest_fatal("fgets");
    }

    if (buf[strlen(buf) - 1] == '\n')
        buf[strlen(buf) - 1] = '\0';

    char *c = strchr(buf, ':');
    if (!c)
        xtest_fatal("strchr");

    for (++c; *c == ' ' || *c == '\t'; ++c)
        continue;

    if (fclose(stat) == EOF) {
        if (errno == ESRCH)
            return NULL;

        xtest_fatal("fclose: %m");
    }

    return c;

fail:
    if (fclose(stat) == EOF && errno != ESRCH)
        xtest_fatal("fclose: %m");

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

    ps_pstat *pstat = nullptr;
    char child_path[PATH_MAX];
    snprintf(child_path, PATH_MAX, "/proc/%d/task/%d/children", pid, pid);

    FILE *stat = fopen(child_path, "r");
    if (!stat)
        xtest_fatal("fopen: %m");

    static char buf[BUF_SIZE];

    if (!fgets(buf, BUF_SIZE - 1, stat)) {
        if (feof(stat) || (ferror(stat) && errno == ESRCH))
            goto cleanup;

        xtest_fatal("fgets: %m");
    }

    char *endptr = buf;

    while (true) {
        char *start = endptr;

        long child_pid = strtol(start, &endptr, 10);
        if (child_pid == LONG_MAX || child_pid == LONG_MIN)
            xtest_fatal("strtol overflow");

        if (start == endptr)
            break;

        pstat = xmalloc(sizeof(ps_pstat));
        pstat->pid = child_pid;

        char *child_name = stat_val_str(child_pid, NAME);
        if (!child_name)
            goto cleanup;

        strcpy(pstat->name, child_name);

        char *child_state = stat_val_str(child_pid, STATE);
        if (!child_state)
            goto cleanup;

        strcpy(pstat->state, child_state);

        *linc_array_push(pstats) = *pstat;
        free(pstat);
        pstat = nullptr;
    }

cleanup:
    if (fclose(stat) == EOF)
        xtest_fatal("fclose: %m");

    free(pstat);

    return 0;
}
