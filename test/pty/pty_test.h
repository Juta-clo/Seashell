#ifndef PTY_TEST_H
#define PTY_TEST_H

#include <ncurses.h>
#include <vterm.h>

#include "linc_tools.h"

#define PROMPT "> "
#define PTY_RBUF 100000

#define MAX(a, b) \
    ((a) > (b)) ? a : b

#define MIN(a, b) \
    ((a) < (b)) ? a : b

DA_TYPE(da_charp, char *);

struct vt_snap {
    VTermPos curs_pos;
    da_charp lines;
};

typedef struct vt_snap vt_snap;
DA_TYPE(da_vt_snap, vt_snap);
DA_TYPE(da_dstr, dstr);

struct virt_term {
    VTerm *tty;
    VTermScreen *scr;
    VTermState *st;
    int rows;
    int cols;
};

typedef struct virt_term virt_term;

struct pty_test {
    int mfd;
    int cpid;
    size_t pred_i;
    dstr ptydata;
    da_vt_snap snaps;
    virt_term vterm;

    /* for display purposes */
    char *test_name;
    bool success;
    bool val_err;
    da_dstr raw_lines;
    da_dstr shell_log_lines;
    da_dstr val_lines;
    da_dstr test_lines;
};

typedef struct pty_test pty_test;
DA_TYPE(da_pty_test, pty_test);

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

/****** SETUP *******/

void harness_init(int argc, const char *const *argv);

/****** HANDLERS *******/

XFATAL_HANDLER(pty_xfatal_func);
void curs_err(const char *msg);
void pty_test_sink(const llog_info *info);

/****** UTILITIES *******/

#define LN(...) (char *[]){ __VA_ARGS__, NULL }
#define assert_term(...) \
    TEST_ASSERT_MESSAGE(term_ensure_lines(LN(__VA_ARGS__)), "timeout");

void print_valgrind_overview(void);
dstr *push_line(da_dstr *lines);
dstr *last_line(da_dstr *lines);
void add_lines(da_dstr *lines, const char *str);
char *flush_stream(int fd, bool err_on_eio);
int start_timerfd(int ms);
void write_ps_output(void);
void child_pstat_snapshot(void);
void write_to_term(const char *str);
bool term_ensure(const char *msg, int row, int col);
bool term_ensure_lines(char **lines);
void vterm_send_str(const char *msg);
void vterm_send_char(char ch, VTermModifier mod);
int test_fail_count(void);
int val_test_fail_count(void);
void new_shell(void);
void delete_shell(void);

#endif
