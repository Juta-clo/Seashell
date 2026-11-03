#include <signal.h>

#include "linc/xsys.h"
#include "utils.h"
#include "sig_funcs.h"

bool sig_is_pending(int sig) {
    sigset_t set;
    sigpending(&set);

    int is_member = sigismember(&set, sig);
    if (is_member == -1)
        err_exit_imp("sigismember");

    return (bool)is_member;
}

sig_handler *sig_set_handler(int sig, sig_handler *handler) {
    struct sigaction sa;
    struct sigaction osa;

    sa.sa_flags = 0;
    sa.sa_handler = handler;
    xsigemptyset(&sa.sa_mask);

    xsigaction(sig, &sa, &osa);

    return osa.sa_handler;
}

sigset_t sig_block(int sig) {
    sigset_t set;
    sigset_t oset;

    xsigemptyset(&set);
    xsigaddset(&set, sig);

    xsigprocmask(SIG_BLOCK, &set, &oset);

    return oset;
}
