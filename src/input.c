#include <stdio.h>
#include <stdlib.h>
#include <linux/limits.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "linc_tools.h"
#include "input.h"
#include "shell_state.h"
#include "utils.h"
#include "printty.h"
#include "variable.h"

#define MIN(a, b) ((a) < (b)) ? (a) : (b)
#define MAX(a, b) ((a) > (b)) ? (a) : (b)

#define PROMPT1 "> "

#define DELETE_CURSOR 1
#define DELETE_WORD   2
#define DELETE_END    3
#define DELETE_START  4
#define DELETE_ALL    5

#define INSERT_CHAR  1
#define INSERT_PASTE 2

#define OP_NONE 0
#define OP_DEL  1

#define xprintty_cursor_move(pt, row, col) \
    do { if (printty_cursor_move(pt, row, col) == PRINTTY_ERR) \
            xfatal("printty_cursor_move"); } while (false)

#define xprintty_write(pt, uc) \
    do { if (printty_write(pt, uc) == PRINTTY_ERR) \
            xfatal("printty_write"); } while (false)

#define xprintty_write_name(pt, name) \
    do { if (printty_write_name(pt, name) == PRINTTY_ERR) \
            xfatal("printty_write_name"); } while (false)

#define xprintty_get_cursor_pos(pt, row, col) \
    do { if (printty_get_cursor_pos(pt, row, col) == PRINTTY_ERR) \
            xfatal("printty_get_cursor_pos"); } while (false)

#define xprintty_get_term_size(pt, rows, cols) \
    do { if (printty_get_term_size(pt, rows, cols) == PRINTTY_ERR) \
            xfatal("printty_get_term_size"); } while (false)

#define xprintty_cstyle(pt, style) \
    do { if (printty_cstyle(pt, style) == PRINTTY_ERR) \
            xfatal("printty_cstyle"); } while (false)

input_editor input = { .off = 0 };

static inline int llen(void) {
    return (int)(hda_size(input.line) - 1);
}

static inline int line_len(void) {
    int rv = (int)(hda_size(input.line) - 1) - input.start;
    assert(rv >= 0);
    return rv;
}

static inline int next_non_ws(const char *line, int i) {
    ASSERT(line);
    ASSERT_GE_INT(i, 0);

    ++i;

    for (; i < (int)strlen(line); ++i)
        if (line[i] != ' ')
            return i;

    return -1;
}

static inline int next_ws(const char *line, int i) {
    ASSERT(line);
    ASSERT_GE_INT(i, 0);

    ++i;

    for (; i < (int)strlen(line); ++i)
        if (line[i] == ' ')
            return i;

    return -1;
}

static inline int prev_non_ws(const char *line, int i) {
    ASSERT(line);
    ASSERT_GE_INT(i, 0);

    --i;

    for (; i >= 0; --i)
        if (line[i] != ' ')
            return i;

    return -1;
}

static inline int prev_ws(const char *line, int i) {
    ASSERT(line);
    ASSERT_GE_INT(i, 0);

    --i;

    for (; i >= 0; --i)
        if (line[i] == ' ')
            return i;

    return -1;
}

int next_word(const char *line, int start, input_word *word) {
    ASSERT(line && word);
    ASSERT(start >= 0);
    ASSERT(start < (int)strlen(line) + 1);

    if (start == (int)strlen(line))
        return -1;

    int ws = start;

    if (line[ws] != ' ') {
        ws = next_ws(line, ws);
        if (ws == -1)
            return -1;
    }

    word->start = next_non_ws(line, ws);
    if (word->start == -1)
        return -1;

    word->end = next_ws(line, word->start);
    if (word->end == -1) {
        word->end = strlen(line) - 1;
    } else
        --word->end;

    ASSERT_GE_INT(word->start, 0);
    ASSERT_GE_INT(word->end, 0);
    ASSERT_G_INT((int)strlen(line), word->start);
    ASSERT_G_INT((int)strlen(line), word->end);

    return 0;
}

int prev_word(const char *line, int start, input_word *word) {
    ASSERT(line && word);
    ASSERT(start >= 0);
    ASSERT(start < (int)strlen(line) + 1);

    if (start == (int)strlen(line) + 1)
        return -1;

    int ws = start;

    if (line[ws] != ' ') {
        ws = prev_ws(line, ws);
        if (ws == -1)
            return -1;
    }

    word->end = prev_non_ws(line, ws);
    if (word->end == -1)
        return -1;

    word->start = prev_ws(line, word->end);
    if (word->start == -1) {
        word->start = 0;
    } else
        ++word->start;

    ASSERT_GE_INT(word->start, 0);
    ASSERT_GE_INT(word->end, 0);
    ASSERT_G_INT((int)strlen(line), word->start);
    ASSERT_G_INT((int)strlen(line), word->end);

    return 0;
}

int cur_word(const char *line, int start, input_word *word) {
    ASSERT(line && word);
    ASSERT(start >= 0);
    ASSERT(start < (int)strlen(line) + 1);

    if (start == (int)strlen(line) + 1)
        return -1;

    if (line[start] == ' ')
        return -1;

    word->start = prev_ws(line, start);

    if (word->start == -1) {
        word->start = 0;
    } else
        ++word->start;

    word->end = next_ws(line, start);

    if (word->end == -1) {
        word->end = strlen(line) - 1;
    } else
        --word->end;

    ASSERT_GE_INT(word->start, 0);
    ASSERT_GE_INT(word->end, 0);
    ASSERT_G_INT((int)strlen(line), word->start);
    ASSERT_G_INT((int)strlen(line), word->end);

    return 0;
}

static void update_prompt(int style) {
    if (style == PROMPT_SIMPLE) {
        hds_append(input.line, PROMPT1);

    } else if (style == PROMPT_CWD) {
        static char cwd_str[PATH_MAX];

        char *cwd = xgetcwd(cwd_str, PATH_MAX);
        char *cwd_base = basename(cwd);

        hds_append(input.line, cwd_base);
        hds_append(input.line, PROMPT1);

    } else if (style == PROMPT_PS) {
        sh_var *var = lookup_var(&sh_env.vars, "PS");
        if (!var) {
            hds_append(input.line, PROMPT1);

        } else {
            hds_append(input.line, var->value);
        }

    } else
        xfatal("unknown prompt mode");

    input.start = llen();
    LOG_INFO("new prompt: \"%s\"", input.line);
}

static void new_start_pos(void) {
    int row, col;
    xprintty_get_term_size(sh_env.pt, &input.rows, &input.cols);
    xprintty_get_cursor_pos(sh_env.pt, &row, &col);

    if (col != 0) {
        xprintty_write(sh_env.pt, unibi_enter_reverse_mode);
        xprintty_write(sh_env.pt, unibi_enter_bold_mode);
        printf("%%\n");
        xprintty_write(sh_env.pt, unibi_exit_attribute_mode);
    }

    xprintty_get_cursor_pos(sh_env.pt, &row, &col);
    input.start_row = row;

    LOG_INFO("new start pos: [%d,%d]", row, col);
    ASSERT_GE_INT(input.start_row, 0);
    ASSERT_G_INT(input.rows, input.start_row);
}

static void update_start_pos(void) {
    int crow, ccol;
    xprintty_get_term_size(sh_env.pt, &input.rows, &input.cols);
    xprintty_get_cursor_pos(sh_env.pt, &crow, &ccol);

    LOG_INFO("cursor[%d,%d], term[%d,%d]", crow, ccol, input.rows, input.cols);

    int rows_from_start = (input.start + input.off) / input.cols;
    input.start_row = crow - rows_from_start;

    LOG_INFO("rows_from_start=%d, input.start_row=%d",
            rows_from_start, input.start_row);

    if (input.start_row < 0) {
        LOG_WARN("start_row clamped to 0 (was %d)", input.start_row);
        input.start_row = 0;
    }

    LOG_INFO("new start row = %d", input.start_row);
    ASSERT_G_INT(input.rows, input.start_row);
}

static void ncol_cell(int *crow, int *ccol, int col_n) {
    ASSERT_GE_INT(col_n, 0);
    ASSERT_GE_INT(llen(), col_n);
    ASSERT(crow && ccol);

    *crow = input.start_row;
    *ccol = col_n;

    while (input.cols <= *ccol) {
        *ccol -= input.cols;
        ++*crow;
    }
}

static void input_draw(int rows, int start_idx) {
    int scr_i = 0;
    int line_i = start_idx * input.cols;
    int crow, ccol;

    while (true) {
        if (line_i >= llen() || scr_i >= input.cols * rows)
            break;

        ncol_cell(&crow, &ccol, scr_i);
        printf("%c", input.line[line_i]);

        if (ccol == input.cols - 1) {
            if (crow != rows - 1) {
                /* vterm doesn't clear wrap pending on \r or \b */
                printf(" \x1b[1G");

            } else if (input.start_row > 0) {
                --input.start_row;
                scr_i -= input.cols;
                printf(" \x1b[1G");
            }
        }

        ++scr_i;
        ++line_i;
    }
}

static void input_update(void) {
    ASSERT_NEQ_INT(input.rows, 0);
    ASSERT_NEQ_INT(input.cols, 0);

    int crow, ccol;
    int req_idx = (input.start + input.off) / input.cols;
    int rows = input.rows;
    int start_idx = 0;
    bool paging = req_idx >= input.rows;

    if (paging) {
        rows -= 1;

        if (rows != 0)
            start_idx = (rows * ((req_idx - input.rows) / rows)) + input.rows;
    }

    ASSERT_GE_INT(req_idx, start_idx);
    xprintty_cursor_move(sh_env.pt, input.start_row, 0);
    xprintty_write(sh_env.pt, unibi_clr_eos);

    const char *pager_str = ">...";
    bool wrap_pending = input.cols <= (int)strlen(pager_str);

    if (paging) {
        printf("%.*s", MIN(input.cols, (int)strlen(pager_str)), pager_str);

        if (input.rows > 1) {
            if (wrap_pending) {
                printf(" \x1b[1G");
            } else
                printf("\n");
        } else {
            return;
        }
    }

    input_draw(rows, start_idx);
    ncol_cell(&crow, &ccol, (input.off + input.start) - start_idx * input.cols);

    if (paging)
        crow += 1;

    xprintty_cursor_move(sh_env.pt, crow, ccol);
}

static void input_move(int offset) {
    if (input.insert_mode) {
        if (offset < 0)
            return;
        if (offset > line_len())
            return;

        input.off = offset;
        input_update();

    } else {
        if (offset < 0)
            return;
        if (offset >= line_len())
            return;

        input.off = offset;
        input_update();
    }
}

static void input_delete(int type) {
    ASSERT_GE_INT(input.off, 0);
    ASSERT_GE_INT(line_len(), input.off);
    assert(input.insert_mode || input.off < line_len() || line_len() == 0);

    if (line_len() == 0)
        return;

    if (input.insert_mode) {
        if (type == DELETE_CURSOR) {
            if (input.off == 0)
                return;

            hda_delete(input.line, input.start + input.off - 1, 1);
            input.off -= 1;

        } else if (type == DELETE_END) {
            if (input.off == line_len())
                return;

            hda_delete(input.line, input.start + input.off,
                    line_len() - input.off);

        } else if (type == DELETE_START) {
            if (input.off == 0)
                return;

            hda_delete(input.line, input.start, input.off);
            input.off = 0;

        } else if (type == DELETE_ALL) {
            hda_delete(input.line, input.start, line_len());
            input.off = 0;
        }

    } else {
        if (type == DELETE_CURSOR) {
            hda_delete(input.line, input.start + input.off, 1);
            if (input.off == line_len() && input.off != 0)
                input.off -= 1;

        } else if (type == DELETE_ALL) {
            hda_delete(input.line, input.start, line_len());
            input.off = 0;

        } else if (type == DELETE_WORD) {
            input_word word;

            if (cur_word(input.line + input.start, input.off, &word) == -1) {
                hda_delete(input.line, input.start + input.off, 1);
                if (input.off == line_len() && input.off != 0)
                    input.off -= 1;

            } else {
                hda_delete(input.line, word.start + input.start,
                        (word.end - word.start) + 1);

                input.off = word.start;

                if (input.off > 0)
                    --input.off;
            }
        }
    }

    input_update();
}

static void input_insert(int type, const char *s) {
    assert(input.off >= 0);
    assert(input.off <= line_len());
    assert(input.insert_mode || input.off < line_len());

    if (input.insert_mode) {
        if (type == INSERT_CHAR) {
            hda_insert_buf(input.line, input.start + input.off, 1, s);
            input.off += 1;
        }
    }

    input_update();
}

static void enter_mode_normal() {
    xprintty_cstyle(sh_env.pt, PRINTTY_CBLOCK(false));
    input.insert_mode = false;
}

static void enter_mode_insert() {
    xprintty_cstyle(sh_env.pt, PRINTTY_CBEAM(false));
    input.insert_mode = true;
}

static void insert_mode_feed_key(PrinttyInputKey key) {
    if (key == PRINTTY_BACKSPACE) {
        input_delete(DELETE_CURSOR);

    } else if (key == PRINTTY_LEFT) {
        input_move(input.off - 1);

    } else if (key == PRINTTY_RIGHT) {
        input_move(input.off + 1);
    }
}

static void insert_mode_feed_unicode(uint32_t codepoint, char utf8[5]) {
    if (codepoint >= 32 && codepoint <= 126) {
        input_insert(INSERT_CHAR, &utf8[0]);

    } else if (codepoint == 5/*C-e*/) {
        input_move(line_len());

    } else if (codepoint == 1/*C-a*/) {
        input_move(0);

    } else if (codepoint == 11/*C-k*/) {
        input_delete(DELETE_END);

    } else if (codepoint == 21/*C-u*/) {
        input_delete(DELETE_START);

    } else if (codepoint == 12/*C-l*/) {
        xprintty_write(sh_env.pt, unibi_clear_screen);
        new_start_pos();
        input_update();

    } else if (codepoint == 27/*ESC*/) {
        input_move(input.off - 1);
        enter_mode_normal();

    }
}

static void normal_mode_feed_key(PrinttyInputKey key) {
    if (key == PRINTTY_LEFT) {
        input_move(input.off - 1);

    } else if (key == PRINTTY_RIGHT) {
        input_move(input.off + 1);
    }
}

static void operator_pending_feed_unicode(uint32_t codepoint, int op) {
    if (op == OP_DEL) {
        if (codepoint == 'd')
            input_delete(DELETE_ALL);
        else if (codepoint == 'w')
            input_delete(DELETE_WORD);
    }
}

static void normal_mode_feed_unicode(uint32_t codepoint, char _[5]) {
    static int op = OP_NONE;
    input_word word;

    if (op != OP_NONE) {
        operator_pending_feed_unicode(codepoint, op);
        op = OP_NONE;
        return;
    }

    if (codepoint == 'i') {
        enter_mode_insert();

    } else if (codepoint == 'a') {
        enter_mode_insert();
        input_move(input.off + 1);

    } else if (codepoint == 'h') {
        input_move(input.off - 1);

    } else if (codepoint == 'l') {
        input_move(input.off + 1);

    } else if (codepoint == 'x') {
        input_delete(DELETE_CURSOR);

    } else if (codepoint == 'A') {
        enter_mode_insert();
        input_move(line_len());

    } else if (codepoint == 'I') {
        enter_mode_insert();
        input_move(0);

    } else if (codepoint == 'b') {
        if (cur_word(input.line + input.start, input.off, &word) != -1 &&
                input.off != word.start) {
            input_move(word.start);

        } else if (prev_word(input.line + input.start, input.off, &word) == -1) {
            input_move(0);
        } else
            input_move(word.start);

    } else if (codepoint == 'w') {
        if (next_word(input.line + input.start, input.off, &word) == -1) {
            input_move(line_len() - 1);
        } else
            input_move(word.start);

    } else if (codepoint == 'e') {
        if (cur_word(input.line + input.start, input.off, &word) != -1 &&
                input.off != word.end) {
            input_move(word.end);

        } else if (next_word(input.line + input.start, input.off, &word) == -1) {
            input_move(line_len() - 1);
        } else
            input_move(word.end);

    } else if (codepoint == 'd') {
        op = OP_DEL;
    }
}

void input_init(shell_env *env) {
    assert(env);
    if (!(env->pt = printty_create(NULL)))
        xfatal("printty_create");
    if (printty_start(env->pt) == PRINTTY_ERR)
        xfatal("printty_resume");
    if (printty_cbreak(env->pt) == PRINTTY_ERR)
        xfatal("printty_cbreak");
    printty_set_suspend_mask(env->pt, env->og_mask);
    printty_set_esc_delay(env->pt, 25);
}

void input_deinit(shell_env *env) {
    assert(env);
    /* null when called from the exit builtin */
    if (env->pt) {
        xprintty_write(env->pt, unibi_exit_attribute_mode);
        xprintty_write_name(env->pt, "Se");
        printty_destroy(env->pt);
    }
    env->pt = NULL;
}

void display_job_msg(const char *msg) {
    int crow, ccol;
    xprintty_get_cursor_pos(sh_env.pt, &crow, &ccol);

    if (ccol != 0)
        printf("\n");

    printf("%s\n", msg); /* needs bottom left check */
}

void drop_line(void) {
    if (input.line) {
        hda_free(input.line);
        input.line = NULL;
    }
}

char *get_line(void) {
    if (!input.line) {
        input.off = 0;
        update_prompt(sh_env.prompt_style);
    }

    if (sh_env.need_prompt) {
        sh_env.need_prompt = false;
        new_start_pos();
    }

    input_update();
    enter_mode_insert();

    while (true) {
        PrinttyEvent pev;
        int rv = printty_get_event(sh_env.pt, &pev);
        if (rv == PRINTTY_EOF) {
            LOG_INFO("eof, exiting");
            exit(EXIT_SUCCESS);

        } else if (rv == PRINTTY_ERR) {
            xfatal("failed parsing line");

        } else if (rv == PRINTTY_SIG) {
            return NULL;
        }

        if (pev.type == PRINTTY_UNICODE) {
            if (input.insert_mode) {
                insert_mode_feed_unicode(pev.codepoint, pev.utf8);
            } else {
                normal_mode_feed_unicode(pev.codepoint, pev.utf8);
            }

        } else if (pev.type == PRINTTY_KEY) {
            if (pev.key == PRINTTY_ENTER) {
                printf("\n");
                break;
            }

            if (input.insert_mode) {
                insert_mode_feed_key(pev.key);

            } else {
                normal_mode_feed_key(pev.key);
            }

        } else if (pev.type == PRINTTY_RESIZE) {
            LOG_INFO("resize detected");
            update_start_pos();
            input_update();
        }
    }

    LOG_INFO("parsed line: %s", input.line);
    return input.line + input.start;
}
