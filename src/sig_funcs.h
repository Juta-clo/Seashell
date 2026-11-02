#ifndef SIGNAL_FUNCS_H
#define SIGNAL_FUNCS_H

#include <signal.h>

typedef void(sig_handler)(int);

extern volatile sig_atomic_t sigchld_caught;
extern volatile sig_atomic_t sighup_caught;
extern volatile sig_atomic_t sigint_caught;

void sigchld_handler(int _);
void sighup_handler(int _);
void sigint_handler(int _);

sig_handler *sig_set_handler(int sig, sig_handler *handler);
sigset_t sig_block(int sig);

bool sig_is_pending(int sig);
void sig_restore(sigset_t *og_mask);
void sig_setup(sigset_t *og_mask);

#endif
