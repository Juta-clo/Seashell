#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <sys/wait.h>

#include "state.h" // IWYU pragma: keep
#include "utils.h"
#include "wait_stat.h"

wait_event last_exit_event = {0};

bool last_interrupted(void) {
    return (
        last_exit_event.type == PSIGNALED &&
        last_exit_event.term_sig == SIGINT
    );
}

char *get_wstat_str(pid_t pid, int wstat) {
    static char buf[4096];

    if (WIFEXITED(wstat)) {
        snprintf(buf, 4096, "%d exited with status %d",
                pid, WEXITSTATUS(wstat));

    } else if (WIFSIGNALED(wstat)) {
        snprintf(buf, 4096, "%d termianted by signal %d (%s)",
                pid, WTERMSIG(wstat), strsignal(WTERMSIG(wstat)));

    } else if (WIFSTOPPED(wstat)) {
        snprintf(buf, 4096, "%d stopped by signal %d (%s)",
                pid, WSTOPSIG(wstat), strsignal(WSTOPSIG(wstat)));

    } else if (WIFCONTINUED(wstat)) {
        snprintf(buf, 4096, "%d continued", pid);
    } else
        fatal("unexpected wstat");

    return buf;
}

int get_wstat(wait_event *wev, pid_t pid, int wopt) {
    ASSERT(wev);

    *wev = (wait_event){0};

    int wstat;
    pid_t cpid = waitpid(pid, &wstat, wopt);

    if (cpid == -1) {
        if (errno != ECHILD)
            err_exit("waitpid");

        if (pid == -1) {
            return -1;
        } else {
            fatal("tried waiting for non-existant process");
        }
    }

    if (cpid == 0)
        return -1;

    wev->pid = cpid;

    if (WIFEXITED(wstat)) {
        wev->type = PEXITED;
        wev->exit_stat = WEXITSTATUS(wstat);
        last_exit_event = *wev;
    } else if (WIFSIGNALED(wstat)) {
        wev->type = PSIGNALED;
        wev->term_sig = WTERMSIG(wstat);
        last_exit_event = *wev;
    } else if (WIFSTOPPED(wstat)) {
        wev->type = PSTOPPED;
        wev->stop_sig = WSTOPSIG(wstat);
    } else if (WIFCONTINUED(wstat)) {
        wev->type = PCONTINUED;
    }

    return 0;
}
