#ifndef T_VTERM_H
#define T_VTERM_H

#include <vterm.h>

#include <linc/array.h>
#include "t_utils.h"

LINC_ARRAY_TYPE(da_charp, char *);

#define VT_COL_BLACK   0
#define VT_COL_RED     1
#define VT_COL_GREEN   2
#define VT_COL_YELLOW  3
#define VT_COL_BLUE    4
#define VT_COL_MAGENTA 5
#define VT_COL_CYAN    6
#define VT_COL_WHITE   7

#define VT_COL_BRIGHT_BLACK   8
#define VT_COL_BRIGHT_RED     9
#define VT_COL_BRIGHT_GREEN   10
#define VT_COL_BRIGHT_YELLOW  11
#define VT_COL_BRIGHT_BLUE    12
#define VT_COL_BRIGHT_MAGENTA 13
#define VT_COL_BRIGHT_CYAN    14
#define VT_COL_BRIGHT_WHITE   15

typedef struct {
    VTerm *vp;
    VTermScreen *scr;
    VTermState *st;
    int rows;
    int cols;
} vt_vterm;

typedef struct {
    char ch;
    int bold;
    int under;
    int fg, bg;
} vt_cell;

LINC_ARRAY_TYPE(da_vt_cell, vt_cell);

typedef struct {
    int crow, ccol;
    da_charp lines;
    da_vt_cell cells;
    int rows, cols;
} vt_snap;

LINC_ARRAY_TYPE(da_vt_snap, vt_snap);

void vt_take_snap(void);
void vt_write(const char *str);
bool vt_snap_cmp(vt_snap *exp, bool attr);
da_int vt_ch_dirty(vt_snap *exp, vt_snap *act, bool *curs_dirty);
vt_snap vt_get_snap(void);
void vt_free_snap(vt_snap *snap);
void vt_lines_to_snap(char **lines, vt_snap *snap, int crow, int ccol);

void vt_send_str(const char *msg);
void vt_send_ch(char ch, VTermModifier mod);
void vt_send_key(VTermKey key, VTermModifier mod);

vt_vterm vt_new_vterm(int *fd, int rows, int cols);

#endif
