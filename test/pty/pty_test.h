#ifndef PTY_TEST_H
#define PTY_TEST_H

#include <ncurses.h>
#include <vterm.h>

#include "linc_tools.h"

#define PROMPT "> "
#define PROMPT_LEN 2
#define PTY_RBUF 100000

#define PSTATE_ZOMBIE   "Z"
#define PSTATE_RUNNING  "R"
#define PSTATE_SLEEPING "S"
#define PSTATE_STOPPED  "T"

#define PTY_POLL_TFD   1
#define PTY_POLL_SHELL 2
#define PTY_POLL_VAL   3
#define PTY_POLL_MFD   4

#define MAX(a, b) \
    ((a) > (b)) ? a : b

#define MIN(a, b) \
    ((a) < (b)) ? a : b

#define ARR_LEN(arr) \
    (sizeof(arr) / sizeof(typeof(*arr)))

DA_TYPE(da_charp, char *);

struct vt_snap {
    VTermPos curs_pos;
    da_charp lines;
    int rows, cols;
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
    double dur;
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

extern pty_test *test;

/****** SETUP *******/

void harness_init(int argc, const char *const *argv);

/****** HANDLERS *******/

XFATAL_HANDLER(pty_xfatal_func);
void curs_err(const char *msg);
void pty_test_sink(const llog_info *info);

/****** UTILITIES *******/

#define LN(...) (char *[]){ __VA_ARGS__, NULL }

#define assert_term(_row, _col, ...) \
    do { TEST_ASSERT_MESSAGE(term_ensure(LN(__VA_ARGS__), _row, _col), \
            "term_ensure failed"); \
        take_vt_snapshot(); } while (false)

#define assert_term_clear(_row, _col, ...) \
    do { TEST_ASSERT_MESSAGE(term_ensure(LN(__VA_ARGS__), _row, _col), \
            "term_ensure failed"); \
        take_vt_snapshot(); \
        vterm_send_char('l', VTERM_MOD_CTRL); } while (false)

#define assert_termios(exp) \
    TEST_ASSERT_MESSAGE(verify_termios(exp), "bad termios");

int pty_test_poll(int tfd, bool log_mfd);
void take_vt_snapshot(void);
bool verify_termios(struct termios exp);
bool verify_pstate(const char *name, const char *state);
void test_resize_term(int row, int col);
bool kill_child(const char *name);
void sleep_ms(int ms);
void pty_poll_sleep(int ms);
bool pty_poll_str(const char *str);
void print_valgrind_overview(void);
dstr *push_line(da_dstr *lines);
dstr *last_line(da_dstr *lines);
void add_lines(da_dstr *lines, const char *str);
char *flush_stream(int fd, bool err_on_eio);
int start_timerfd(int ms);
void write_ps_output(void);
void child_pstat_snapshot(void);
void write_to_term(const char *str);
bool term_ensure(char **lines, int row, int col);
void vterm_send_str(const char *msg);
void vterm_send_key(VTermKey key, VTermModifier mod);
void vterm_send_char(char ch, VTermModifier mod);
int test_fail_count(void);
int val_test_fail_count(void);
void new_shell(void);
void delete_shell(void);

#endif
