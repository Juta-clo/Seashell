#ifndef INPUT_H
#define INPUT_H

#include <sys/signal.h>

typedef void (*input_cb)(void);
extern void (*input_sig_cb)(void);

void input_init(void);
void input_deinit(void);

void input_set_suspend_mask(sigset_t suspend_mask);
void input_clear_history(void);
void input_reset_line_state(void);
void input_on_new_line(void);
char *input_get_line(const char *prompt);

#endif
