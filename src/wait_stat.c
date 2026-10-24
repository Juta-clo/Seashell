#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <sys/wait.h>

#include "utils.h"
#include "wait_stat.h"

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
        xfatal("unexpected wstat");

    return buf;
}

int get_wstat(wait_event *wev) {
    assert(wev);

    int wstat;
    pid_t cpid = waitpid(-1, &wstat, WUNTRACED | WCONTINUED | WNOHANG);

    if (cpid == -1 && errno != ECHILD) {
        LOG_ERR("waitpid: %m");
        err_exit("waitpid");
    }

    if (cpid == 0 || errno == ECHILD)
        return -1;

    wev->pid = cpid;

    if (WIFEXITED(wstat)) {
        wev->type = PEXITED;
        wev->exit_stat = WEXITSTATUS(wstat);

    } else if (WIFSIGNALED(wstat)) {
        wev->type = PSIGNALED;
        wev->term_sig = WTERMSIG(wstat);

    } else if (WIFSTOPPED(wstat)) {
        wev->type = PSTOPPED;
        wev->stop_sig = WSTOPSIG(wstat);

    } else if (WIFCONTINUED(wstat)) {
        wev->type = PCONTINUED;
    }

    /* child might not have received hup before getting here from
     * hup_to_children. */
    LOG_INFO("%s", get_wstat_str(cpid, wstat));
    return 0;
}
