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

#define DELETE_CURSOR 1
#define DELETE_WORD   2
#define DELETE_END    3
#define DELETE_START  4

#define INSERT_CHAR  1
#define INSERT_PASTE 2

input_editor input = { .offset = 0 };

static void update_prompt(int style) {
    if (style == PROMPT_SIMPLE) {
        hds_append(input.line, "> ");

    } else if (style == PROMPT_CWD) {
        static char cwd_str[PATH_MAX];

        char *cwd = xgetcwd(cwd_str, PATH_MAX);
        char *cwd_base = basename(cwd);

        hds_append(input.line, cwd_base);
        hds_append(input.line, "> ");

    } else
        xfatal("unknown prompt mode");

    input.start = llen();
    LOG_INFO("new prompt: \"%s\"", input.line);
}

static void new_start_pos(void) {
    if (printty_get_term_size(sh_env.pt, &input.rows, &input.cols)
            == PRINTTY_ERR)
        xfatal("printty_get_sterm_size");

    int row, col;
    if (printty_get_cursor_pos(sh_env.pt, &row, &col) == PRINTTY_ERR)
        xfatal("printty_get_cursor_pos");

    if (col != 0) {
        printty_write(sh_env.pt, unibi_enter_reverse_mode);
        printty_write(sh_env.pt, unibi_enter_bold_mode);
        printf("%%\n"); /* need bottom left check */
        printty_write(sh_env.pt, unibi_exit_attribute_mode);
    }

    if (printty_get_cursor_pos(sh_env.pt, &row, &col) == PRINTTY_ERR)
        xfatal("printty_get_cursor_pos");

    input.start_row = row;

    LOG_INFO("new start pos: [%d,%d]", row, col);
}

static void update_start_pos(void) {
    int crow, ccol;

    if (printty_get_term_size(sh_env.pt, &input.rows, &input.cols)
            == PRINTTY_ERR)
        xfatal("printty_get_term_size");

    if (printty_get_cursor_pos(sh_env.pt, &crow, &ccol)
            == PRINTTY_ERR)
        xfatal("printty_get_cursor_pos");

    int rows_from_start = llen() / input.cols;
    input.start_row = crow - rows_from_start;

    assert(input.start_row >= 0);
    assert(input.start_row < input.rows);
}

static void get_line_cell(int *crow, int *ccol, size_t i) {
    assert(i >= 0);
    assert(i <= hda_size(input.line) - 1);
    assert(crow && ccol);

    *crow = input.start_row;
    *ccol = i;

    while (input.cols <= *ccol) {
        *ccol -= input.cols;
        ++*crow;
    }
}

static void input_update_cursor(int offset) {
    assert(offset >= 0);
    assert(offset <= line_len());

    offset += input.start;

    int crow, ccol;
    get_line_cell(&crow, &ccol, offset);
    printty_cursor_move(sh_env.pt, crow, ccol);
}

static void input_redraw(void) {
    if (printty_cursor_move(sh_env.pt, input.start_row, 0) == PRINTTY_ERR)
        xfatal("printty_cursor_move");

    int crow, ccol;

    for (size_t i = 0; i < hda_size(input.line) - 1; ++i) {
        get_line_cell(&crow, &ccol, i);

        if (crow == input.rows - 1 && ccol == input.cols - 1) {
            printf("%c\r\n", input.line[i]);
            --input.start_row;
        } else
            printf("%c", input.line[i]);
    }

    printf(" "); /* needs bottom left check */
    printty_write(sh_env.pt, unibi_clr_eos);
    input_update_cursor(input.offset);
}

static void input_move(int offset) {
    if (input.insert_mode) {
        if (offset < 0)
            return;
        if (offset > line_len())
            return;

        input_update_cursor(offset);
        input.offset = offset;

    } else {
        if (offset < 0)
            return;
        if (offset >= line_len())
            return;

        input_update_cursor(offset);
        input.offset = offset;
    }
}

static void input_delete(int type) {
    assert(input.offset >= 0);
    assert(input.offset <= line_len());
    assert(input.insert_mode || input.offset < line_len());

    if (input.insert_mode) {
        if (type == DELETE_CURSOR) {
            if (input.offset == 0)
                return;

            hda_delete(input.line, input.start + input.offset - 1, 1);
            input.offset -= 1;

        } else if (type == DELETE_END) {
            if (input.offset == line_len())
                return;

            hda_delete(input.line, input.start + input.offset,
                    line_len() - input.offset);

        } else if (type == DELETE_START) {
            if (input.offset == 0)
                return;

            hda_delete(input.line, input.start, input.offset);
            input.offset = 0;
        }

    } else {
        if (type == DELETE_CURSOR) {
            hda_delete(input.line, input.start + input.offset, 1);
            if (input.offset == line_len() - 1)
                input.offset -= 1;
        }
    }

    input_redraw();
}

static void input_insert(int type, const char *s) {
    assert(input.offset >= 0);
    assert(input.offset <= line_len());
    assert(input.insert_mode || input.offset < line_len());

    if (input.insert_mode) {
        if (type == INSERT_CHAR) {
            hda_insert_buf(input.line, input.start + input.offset, 1, s);
            input.offset += 1;
        }
    }

    input_redraw();
}

static void enter_mode_normal() {
    if (printty_cstyle(sh_env.pt, PRINTTY_CBLOCK(false)) == PRINTTY_ERR)
        xfatal("printty_cstyle");

    input.insert_mode = false;
}

static void enter_mode_insert() {
    if (printty_cstyle(sh_env.pt, PRINTTY_CBEAM(false)) == PRINTTY_ERR)
        xfatal("printty_cstyle");

    input.insert_mode = true;
}

static void insert_mode_feed_key(PrinttyInputKey key) {
    if (key == PRINTTY_BACKSPACE) {
        input_delete(DELETE_CURSOR);

    } else if (key == PRINTTY_LEFT) {
        input_move(input.offset - 1);

    } else if (key == PRINTTY_RIGHT) {
        input_move(input.offset + 1);
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
        printty_write(sh_env.pt, unibi_clear_screen);
        new_start_pos();
        input_redraw();

    } else if (codepoint == 27/*ESC*/) {
        input_move(input.offset - 1);
        enter_mode_normal();
    }
}

static void normal_mode_feed_key(PrinttyInputKey key) {
    if (key == PRINTTY_LEFT) {
        input_move(input.offset - 1);

    } else if (key == PRINTTY_RIGHT) {
        input_move(input.offset + 1);
    }
}

static void normal_mode_feed_unicode(uint32_t codepoint, char _[5]) {
    if (codepoint == 'i') {
        enter_mode_insert();

    } else if (codepoint == 'a') {
        enter_mode_insert();
        input_move(input.offset + 1);

    } else if (codepoint == 'h') {
        input_move(input.offset - 1);

    } else if (codepoint == 'l') {
        input_move(input.offset + 1);

    } else if (codepoint == 'x') {
        input_delete(DELETE_CURSOR);

    } else if (codepoint == 'A') {
        enter_mode_insert();
        input_move(line_len());

    } else if (codepoint == 'I') {
        enter_mode_insert();
        input_move(0);
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
        if (printty_write(env->pt, unibi_exit_attribute_mode) == PRINTTY_ERR)
            LOG_WARN("printty_write failed");
        if (printty_write_name(env->pt, "Se") == PRINTTY_ERR)
            LOG_WARN("printty_write_name failed");
        printty_destroy(env->pt);
    }
    env->pt = NULL;
}

void display_job_msg(const char *msg) {
    int crow, ccol;
    if (printty_get_cursor_pos(sh_env.pt, &crow, &ccol) == PRINTTY_ERR)
        xfatal("printty_get_cursor_pos");

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
        input.offset = 0;
        update_prompt(sh_env.prompt_style);
    }

    if (sh_env.need_prompt) {
        sh_env.need_prompt = false;
        new_start_pos();
    }

    input_redraw();
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
            update_start_pos();
            input_update_cursor(input.offset);
        }
    }

    LOG_INFO("parsed line: %s", input.line);
    return input.line + input.start;
}
