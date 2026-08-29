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
    // da_dstr lines;
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

extern int sh_pipe[2];
extern int val_pipe[2];
extern dstr unity_buf;

extern struct termios tio;
extern struct pollfd pollfds[NFDS];/* some fds need to be reinitialized */

extern bool first;
extern int val_test_errs;

/* CREATED FOR EACH TEST */

extern pty_test *cur_test;

extern int mfd;
extern int cpid;
extern dstr ptydata;
extern size_t pred_i;

extern dstr reqdata;

/****** SETUP *******/

void test_init(void);

/****** HANDLERS *******/

XFATAL_HANDLER(pty_xfatal_func);
void curs_err(const char *msg);
LLOG_SINK(pty_test_sink);
void unity_output_char(int c);

/****** UTILITIES *******/

dstr *push_line(da_dstr *lines);
dstr *last_line(da_dstr *lines);
void add_lines(int win_num, const char *str);

int mev_to_win(MEVENT *mev);
char *flush_stream(int fd, bool break_on_eio);
void flush_win(int win_num);

int start_timerfd(int ms);

void pty_ensure(const char *msg);
void child_pstat_snapshot(void);
char *request_chars(size_t nchars, size_t *index, int ms);
void pty_send(const char *msg);
void pty_cc(int cc_code);

void new_shell(void);
void delete_shell(void);

#endif
