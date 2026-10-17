#include <vterm.h>

#include "vterm_utils.h"
#include "pty_test.h"
#include "unity_fixture.h" // IWYU pragma: keep

static void vt_output_cb(const char *bytes, size_t len, void *user) {
    int mfd = *(int *)user;
    if ((size_t) write(mfd, bytes, len) != len)
        TEST_FAIL_MESSAGE("vterm_output_cb: write: failed/partial");
}

bool vt_snap_cmp(vt_snap *exp) {
    assert(exp);

    vt_snap snap = vt_get_snap();

    for (int row = 0; row < test->vt.rows; ++row) {
        if (strcmp(snap.lines.data[row], exp->lines.data[row]) != 0) {
            vt_free_snap(&snap);
            return false;
        }
    }

    if (exp->crow != snap.crow || exp->ccol != snap.ccol) {
        vt_free_snap(&snap);
        return false;
    }

    vt_free_snap(&snap);
    return true;
}


void vt_free_snap(vt_snap *snap) {
    assert(snap);

    for (size_t i = 0; i < snap->lines.size; ++i)
        free(snap->lines.data[i]);

    da_free(&snap->lines);
    *snap = (vt_snap){0};
}

vt_snap vt_get_snap(void) {
    vt_snap snap = {0};

    VTermPos curs_pos;
    vterm_state_get_cursorpos(test->vt.st, &curs_pos);

    snap.rows = test->vt.rows;
    snap.cols = test->vt.cols;
    snap.crow = curs_pos.row;
    snap.ccol = curs_pos.col;

    for (int row = 0; row < test->vt.rows; ++row) {
        char **line = da_push(&snap.lines);
        *line = calloc(1, test->vt.cols + 1);
        if (!line)
            xtest_fatal("calloc: %m");

        for (int col = 0; col < test->vt.cols; ++col) {
            VTermScreenCell cell;
            VTermPos pos = { .row = row, .col = col };

            if (!vterm_screen_get_cell(test->vt.scr, pos, &cell))
                xtest_fatal("vterm_screen_get_cell");

            if (cell.chars[0] > 127)
                PTY_TEST_FAIL("non ascii in vterm");

            char ch;

            if (cell.chars[0] == 0) {
                ch = '`';
            } else {
                ch = cell.chars[0];
            }

            (*line)[col] = ch;
        }
    }

    return snap;
}

void vt_lines_to_snap(char **lines, vt_snap *snap, int crow, int ccol) {
#define VTERM_ROWS_MAX 4096
    assert(lines);
    assert(test->vt.rows <= 4096);
    assert(snap);

    if (crow < 0 || crow >= test->vt.rows)
        xtest_fatal("lines_to_vterm_snap: out of bounds start row");
    if (ccol < 0 || ccol >= test->vt.cols)
        xtest_fatal("lines_to_vterm_snap: out of bounds start col");

    *snap = (vt_snap){
        .rows = test->vt.rows, .cols = test->vt.cols,
        .crow = crow, .ccol = ccol
    };

    for (int i = 0; i < test->vt.rows && lines[i]; ++i)
        if (i == test->vt.rows - 1 && lines[i + 1])
            xtest_fatal("lines_to_term: Too many terminal lines");

    bool lines_done = false;
    char spacing[VTERM_ROWS_MAX];
    memset(spacing, '`', VTERM_ROWS_MAX);

    for (int i = 0; i < test->vt.rows; ++i) {
        dstr line;
        dstr_init(&line);

        if (!lines_done && lines[i] == NULL)
            lines_done = true;

        int gap = test->vt.cols;

        if (!lines_done) {
            if (test->vt.cols < (int)strlen(lines[i])) {
                xtest_fatal("bad line request, len=%d, cols=%d: [%s]",
                    (int)hds_len(lines[i]), test->vt.cols, lines[i]);
            }

            dstrcat(&line, lines[i]);
            gap -= (int)strlen(lines[i]);
        }

        dstrncat(&line, spacing, gap);
        *da_push(&snap->lines) = line.c_str;
    }
}

void vt_send_str(const char *msg) {
    VTerm *vtty = test->vt.vp;

    for (const char *c = msg; *c != '\0'; ++c) {
        uint32_t key = *c;

        if (*c == '\n') {
            vterm_keyboard_key(vtty, VTERM_KEY_ENTER, VTERM_MOD_NONE);
        } else if (*c == '\b') {
            vterm_keyboard_key(vtty, VTERM_KEY_BACKSPACE, VTERM_MOD_NONE);
        } else {
            vterm_keyboard_unichar(vtty, key, VTERM_MOD_NONE);
        }
    }
}

void vt_send_ch(char ch, VTermModifier mod) {
    vterm_keyboard_unichar(test->vt.vp, ch, mod);
}

void vt_send_key(VTermKey key, VTermModifier mod) {
    vterm_keyboard_key(test->vt.vp, key, mod);
}

vt_vterm vt_new_vterm(int *fd, int rows, int cols) {
    vt_vterm vt;

    vt.vp = vterm_new(rows, cols);
    if (!vt.vp)
        xtest_fatal("vterm_new");

    vt.rows = rows;
    vt.cols = cols;

    vterm_set_utf8(vt.vp, 1);

    vt.scr = vterm_obtain_screen(vt.vp);
    if (!vt.scr)
        xtest_fatal("vterm_obtain_screen");

    vt.st = vterm_obtain_state(vt.vp);
    if (!vt.st)
        xtest_fatal("vterm_obtain_state");

    vterm_screen_reset(vt.scr, 1);
    vterm_screen_enable_reflow(vt.scr, true);
    vterm_output_set_callback(vt.vp, vt_output_cb, fd);

    return vt;
}
