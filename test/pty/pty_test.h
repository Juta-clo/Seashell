#ifndef PTY_TEST_H
#define PTY_TEST_H

#include <ncurses.h>

#include "darr.h"
#include "dstr.h"
#include "llog.h"
#include "xfuncs.h"

#define PROMPT "> "
#define PTY_RBUF 100000

struct pty_test {
    int mfd;
    int cpid;
    dstr ptydata;
    size_t pred_i;
    char *test_name;
    bool success;
    bool val_err;
    da_dstr raw_lines;
    da_dstr shell_log_lines;
    da_dstr val_lines;
    da_dstr test_lines;
};

typedef struct pty_test pty_test;

typedef enum {
    POLL_MFD,
    POLL_SHELL,
    POLL_VAL,
    POLL_TFD,
    NFDS,
} poll_fds;

/* PERSISTS ACROSS TESTS */

extern da_pty_test all_tests;
extern bool verbose_mode;
extern bool run_valgrind;

extern int sh_pipe[2];
extern int val_pipe[2];
extern char *shell_lfd_arg;
extern char *val_lfd_arg;

extern struct termios tio;
extern struct pollfd pollfds[NFDS];/* some fds need to be reinitialized */

extern bool first;
extern int val_test_errs;

/* CREATED FOR EACH TEST */

extern pty_test *cur_test;
extern dstr reqdata;

/****** SETUP *******/

void harness_init(int argc, const char *const *argv);

/****** HANDLERS *******/

XFATAL_HANDLER(pty_xfatal_func);
void curs_err(const char *msg);
LLOG_SINK(pty_test_sink);

/****** UTILITIES *******/

void print_valgrind_overview(void);
dstr *push_line(da_dstr *lines);
dstr *last_line(da_dstr *lines);
void add_lines(da_dstr *lines, const char *str);
char *flush_stream(int fd, bool err_on_eio);
int start_timerfd(int ms);
void pty_ensure(const char *msg);
void child_pstat_snapshot(void);
char *request_chars(size_t nchars, size_t *index, int ms);
void pty_send(const char *msg);
void pty_cc(int cc_code);
int test_fail_count(void);
int val_test_fail_count(void);
void new_test(void);
void new_shell(void);
void delete_shell(void);

#endif
