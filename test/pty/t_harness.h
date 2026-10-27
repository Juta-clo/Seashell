#ifndef T_HARNESS_H
#define T_HARNESS_H

#include <linux/limits.h>
#include <ncurses.h>
#include <vterm.h>

#include <linc/array.h>
#include <linc/string.h>
#include "t_vterm.h"

typedef struct {
    int mfd;
    int cpid;
    int fail_line;
    double duration;
    bool test_ok;
    bool fail_exec;
    bool val_err;
    bool diff_snaps;
    char **argv;

    linc_string pty_out_data;
    linc_string pty_in_data;
    linc_string shell_data;
    linc_string val_data;

    linc_string fail_msg;
    linc_string exit_msg;
    linc_string diff_msg;

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

LINC_ARRAY_TYPE(da_pty_test, pty_test);

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
} pty_harness;

extern bool verbose;
extern bool memcheck;
extern bool script;

extern pty_harness ptyh;
extern pty_test *test;

extern char **(*static_args_cb)(void);

void test_harness_init(int argc, const char *const *argv);
void test_harness_deinit(void);
void print_val_sum(void);
char **create_exec_argv(char **args);

void test_resize_term(int row, int col);
// void test_config_file(const char *content);
int test_poll(int tfd);
void test_pump_ms(int ms);

#endif
