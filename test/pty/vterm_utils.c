#include <vterm.h>
#include <regex.h>

#include "vterm_utils.h"
#include "pty_test.h"
#include "unity_fixture.h" // IWYU pragma: keep

static char reg_chars[] = {
    /* leave ']', '-', '}' out cause only valid contexually */
    '[', '.', '*', '+', '?', '(', ')', '{', '|', '^', '$', '\\'
};

static char *reg_convert(char *line) {
    dstr out;
    dstr_init(&out);
    dstr_push(&out, '^');

    if (line) {
        for (char *c = line; *c != '\0'; ++c) {
            if (*c == '%') {
                switch (c[1]) {
                case 'd':
                    dstrcat(&out, "[0-9]+");
                    ++c;
                    continue;
                case '%':
                    dstr_push(&out, '%');
                    ++c;
                    continue;
                default:
                    xtest_fatal("%s: Bad regex %s", test->test_name, line);
                }
            }

            for (size_t i = 0; i < sizeof(reg_chars); ++i) {
                if (reg_chars[i] == *c) {
                    dstr_push(&out, '\\');
                    break;
                }
            }

            dstr_push(&out, *c);
        }
    }

    dstrcat(&out, "`*$");
    return out.c_str;
}

static void vt_output_cb(const char *bytes, size_t len, void *user) {
    if (len == 0 || !bytes || !user)
        return;

    dstrncat(&test->pty_in_data, bytes, len);

    int mfd = *(int *)user;
    if ((size_t) write(mfd, bytes, len) != len)
        xtest_fatal("vterm_output_cb: write: failed/partial");
}

bool vt_snap_cmp(vt_snap *exp) {
    assert(exp);

    int rv;
    char errbuf[BUFSIZ];
    vt_snap snap = vt_get_snap();
    regex_t reg;

    for (int row = 0; row < test->vt.rows; ++row) {
        rv = regcomp(&reg, exp->lines.data[row], REG_EXTENDED | REG_NOSUB);

        if (rv != 0) {
            regerror(rv, &reg, errbuf, BUFSIZ);
            xtest_fatal("regcomp: %s", errbuf);
        }

        rv = regexec(&reg, snap.lines.data[row], 0, NULL, 0);

        if (rv == REG_NOMATCH) {
            vt_free_snap(&snap);
            regfree(&reg);
            return false;
        } else if (rv != 0) {
            regerror(rv, &reg, errbuf, BUFSIZ);
            xtest_fatal("regexec: %s", errbuf);
        }

        regfree(&reg);
    }

    if (exp->crow != snap.crow || exp->ccol != snap.ccol) {
        vt_free_snap(&snap);
        return false;
    }

    vt_free_snap(&snap);
    return true;
}

da_int vt_ch_dirty(vt_snap *exp, vt_snap *act, bool *curs_dirty) {
    regex_t reg;
    char reg_errbuf[BUFSIZ];
    da_int out = {0};

    *curs_dirty = exp->crow != act->crow || exp->ccol != act->ccol;

    for (int row = 0; row < test->vt.rows; ++row) {
        int rv = regcomp(&reg, exp->lines.data[row],
                REG_EXTENDED | REG_NOSUB);

        if (rv != 0) {
            regerror(rv, &reg, reg_errbuf, BUFSIZ);
            xtest_fatal("regcomp: %s", reg_errbuf);
        }

        rv = regexec(&reg, act->lines.data[row], 0, NULL, 0);

        if (rv == REG_NOMATCH) {
            regfree(&reg);
            *da_push(&out) = row;
        } else if (rv != 0) {
            regerror(rv, &reg, reg_errbuf, BUFSIZ);
            xtest_fatal("regexec: %s", reg_errbuf);
        }

        regfree(&reg);
    }

    return out;
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

    for (int i = 0; i < test->vt.rows; ++i) {
        if (!lines_done && lines[i] == NULL)
            lines_done = true;

        char *s;

        if (!lines_done) {
            s = reg_convert(lines[i]);
        } else {
            s = reg_convert(NULL);
        }

        *da_push(&snap->lines) = s;
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
