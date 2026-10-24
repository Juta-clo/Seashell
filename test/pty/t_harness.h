#ifndef T_HARNESS_H
#define T_HARNESS_H

#include <linux/limits.h>
#include <ncurses.h>
#include <vterm.h>

#include "darr.h"
#include "dstr.h"
#include "t_vterm.h"

typedef struct {
    int mfd;
    int cpid;
    int fail_line;
    double duration;
    bool test_ok;
    bool val_err;
    bool diff_snaps;

    dstr pty_out_data;
    dstr pty_in_data;
    dstr shell_data;
    dstr val_data;

    dstr fail_msg;
    dstr exit_msg;
    dstr diff_msg;

    vt_vterm vt;
    vt_snap vt_exp, vt_act;
    da_vt_snap snaps;

    char *test_name;
    char *test_file;

    /* t_debug.c should own this */
    da_dstr shlog_lines;
    da_dstr val_lines;
    da_dstr test_lines;
} pty_test;

DA_TYPE(da_pty_test, pty_test);

typedef struct {
    da_pty_test tests;
    int val_errs;
    int fails;
    bool pass;
    char *cmd_str;
    FILE *log_file;
    FILE *log_raw_out;
    FILE *log_raw_in;
    int sh_p[2];
    int val_p[2];
    char **exec_argv;
} pty_harness;

extern bool verbose;
extern bool memcheck;
extern bool script;

extern pty_harness ptyh;
extern pty_test *test;

void test_harness_init(int argc, const char *const *argv);
void test_harness_deinit(void);

void print_val_sum(void);
void test_resize_term(int row, int col);

int test_poll(int tfd);
void test_pump_ms(int ms);

#endif
