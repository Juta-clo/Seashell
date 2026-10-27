#include <signal.h>

#include <linc/xsys.h>
#include "input.h"
#include "utils.h"
#include "sig_funcs.h"
#include "event.h"

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

static void set_sig_action(int sig, sighandler_t handler, int flags,
        sigset_t *mask) {
    struct sigaction sa;
    sa.sa_flags = flags;
    sa.sa_handler = handler;

    if (mask) {
        sa.sa_mask = *mask;
    } else
        sigemptyset(&sa.sa_mask);

    xsigaction(sig, &sa, NULL);
}

static void procmask_add(int sig, int how) {
    sigset_t set;

    xsigemptyset(&set);
    xsigaddset(&set, sig);
    xsigprocmask(how, &set, NULL);
}

static void block_sig(int sig) {
    procmask_add(sig, SIG_BLOCK);
}

void reset_sig_flags(void) {
    sigchld_caught = false;
    sighup_caught = false;
    sigint_caught = false;
}

void sig_restore(sigset_t *og_mask) {
    xsigprocmask(SIG_SETMASK, og_mask, NULL);
    set_sig_action(SIGTTOU, SIG_DFL, 0, NULL);
    set_sig_action(SIGTTIN, SIG_DFL, 0, NULL);
    set_sig_action(SIGTSTP, SIG_DFL, 0, NULL);
    set_sig_action(SIGQUIT, SIG_DFL, 0, NULL);
    set_sig_action(SIGTERM, SIG_DFL, 0, NULL);
}

void sig_setup(sigset_t *og_mask) {
    xsigprocmask(0, NULL, og_mask);

    set_sig_action(SIGTTOU, SIG_IGN, 0, NULL);
    set_sig_action(SIGTTIN, SIG_IGN, 0, NULL);
    set_sig_action(SIGTSTP, SIG_IGN, 0, NULL);
    set_sig_action(SIGQUIT, SIG_IGN, 0, NULL);
    set_sig_action(SIGTERM, SIG_IGN, 0, NULL);

    block_sig(SIGCHLD);
    set_sig_action(SIGCHLD, sigchld_handler, 0, NULL);

    block_sig(SIGHUP);
    set_sig_action(SIGHUP, sighup_handler, 0, NULL);

    block_sig(SIGINT);
    set_sig_action(SIGINT, sigint_handler, 0, NULL);

    input_set_suspend_mask(*og_mask);
    input_sig_cb = process_signals;
}
