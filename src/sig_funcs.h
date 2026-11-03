#ifndef SIGNAL_FUNCS_H
#define SIGNAL_FUNCS_H

#include <sys/signal.h>

typedef void(sig_handler)(int);

bool sig_is_pending(int sig);
sig_handler *sig_set_handler(int sig, sig_handler *handler);
sigset_t sig_block(int sig);

#endif
