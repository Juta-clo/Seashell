#ifndef T_VTERM_H
#define T_VTERM_H

#include <vterm.h>

#include <linc/array.h>
#include "t_utils.h"

LINC_ARRAY_TYPE(da_charp, char *);

typedef struct {
    VTerm *vp;
    VTermScreen *scr;
    VTermState *st;
    int rows;
    int cols;
} vt_vterm;

typedef struct {
    int crow, ccol;
    da_charp lines;
    int rows, cols;
} vt_snap;

LINC_ARRAY_TYPE(da_vt_snap, vt_snap);

void vt_write(const char *str);
bool vt_snap_cmp(vt_snap *exp);
da_int vt_ch_dirty(vt_snap *exp, vt_snap *act, bool *curs_dirty);
vt_snap vt_get_snap(void);
void vt_free_snap(vt_snap *snap);
void vt_lines_to_snap(char **lines, vt_snap *snap, int crow, int ccol);

void vt_send_str(const char *msg);
void vt_send_ch(char ch, VTermModifier mod);
void vt_send_key(VTermKey key, VTermModifier mod);

vt_vterm vt_new_vterm(int *fd, int rows, int cols);

#endif
