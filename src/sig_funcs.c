#include <signal.h>

#include <linc/xsys.h>
#include "utils.h"
#include "sig_funcs.h"

volatile sig_atomic_t sigchld_caught = false;
volatile sig_atomic_t sighup_caught = false;
volatile sig_atomic_t sigint_caught = false;

void sigchld_handler(int _) {
    sigchld_caught = true;
}

void sighup_handler(int _) {
    sighup_caught = true;
}

void sigint_handler(int _) {
    sigint_caught = true;
}

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

void sig_restore(sigset_t *og_mask) {
    xsigprocmask(SIG_SETMASK, og_mask, NULL);

    sig_set_handler(SIGTTOU, SIG_DFL);
    sig_set_handler(SIGTTIN, SIG_DFL);
    sig_set_handler(SIGTSTP, SIG_DFL);
    sig_set_handler(SIGQUIT, SIG_DFL);
    sig_set_handler(SIGTERM, SIG_DFL);

    sig_set_handler(SIGCHLD, SIG_DFL);
    sig_set_handler(SIGHUP, SIG_DFL);
    sig_set_handler(SIGINT, SIG_DFL);
}

void sig_setup(sigset_t *og_mask) {
    sig_set_handler(SIGTTOU, SIG_IGN);
    sig_set_handler(SIGTTIN, SIG_IGN);
    sig_set_handler(SIGTSTP, SIG_IGN);
    sig_set_handler(SIGQUIT, SIG_IGN);
    sig_set_handler(SIGTERM, SIG_IGN);

    sig_set_handler(SIGCHLD, sigchld_handler);
    sig_set_handler(SIGHUP, sighup_handler);
    sig_set_handler(SIGINT, sigint_handler);

    *og_mask = sig_block(SIGCHLD);
    sig_block(SIGHUP);
    sig_block(SIGINT);
}
