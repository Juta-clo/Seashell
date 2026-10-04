#include <stdio.h>
#include <stdlib.h>
#include <linux/limits.h>
#include <string.h> // IWYU pragma: keep
#include <unistd.h>
#include <fcntl.h>

#include "linc_tools.h"
#include "input.h"
#include "shell_state.h"
#include "sig_funcs.h"
#include "utils.h"
#include "printty.h"

input_editor input = { .offset = 0, .need_line = true };

void input_init(shell_env *env) {
    assert(env);
    if (!(env->pt = printty_create(NULL)))
        xfatal("printty_create");
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

void display_prompt(int mode) {
    dstr prompt;
    dstr_init(&prompt);

    int row, col;
    if (printty_get_cursor_pos(sh_env.pt, &row, &col) == PRINTTY_ERR)
        xfatal("printty_get_cursor_pos");

    if (col != 0) {
        printty_write(sh_env.pt, unibi_enter_reverse_mode);
        printty_write(sh_env.pt, unibi_enter_bold_mode);
        printf("%%\n");
        printty_write(sh_env.pt, unibi_exit_attribute_mode);
    }

    if (mode == PROMPT_SIMPLE) {
        dstrcat(&prompt, "> ");

    } else if (mode == PROMPT_CWD) {
        static char cwd_str[PATH_MAX];

        char *cwd = xgetcwd(cwd_str, PATH_MAX);
        char *cwd_base = basename(cwd);

        dstrcat(&prompt, cwd_base);
        dstrcat(&prompt, "> ");

    } else
        xfatal("unknown prompt mode");

    printf("%s", prompt.c_str);
    dstr_free(&prompt);

    input.prompt_drawn = true;
}

static int llen(void) {
    return (int)(hda_size(input.line) - 1);
}

static void input_update_start_pos(void) {
    int crow, ccol;

    if (printty_get_term_size(sh_env.pt, &input.rows, &input.cols)
            == PRINTTY_ERR)
        xfatal("printty_get_term_size");

    if (printty_get_cursor_pos(sh_env.pt, &crow, &ccol)
            == PRINTTY_ERR)
        xfatal("printty_get_cursor_pos");

    int rows_from_start = (input.offset + input.scol) / input.cols;
    input.srow = crow - rows_from_start;

    if (input.cols <= input.scol) {
        input.scol %= input.cols;
        ++input.srow;
    }

    assert(input.srow >= 0);
    assert(input.srow < input.rows);
    assert(input.scol >= 0);
    assert(input.scol < input.cols);
}

static void input_get_cursor(int *crow, int *ccol) {
    assert(crow && ccol);

    *crow = input.srow;
    *ccol = input.offset + input.scol;

    while (input.cols <= *ccol) {
        *ccol -= input.cols;
        ++*crow;
    }
}

static void input_get_cursor_offset(int *crow, int *ccol, int offset) {
    assert(crow && ccol);

    *crow = input.srow;
    *ccol = offset + input.scol;

    while (input.cols <= *ccol) {
        *ccol -= input.cols;
        ++*crow;
    }
}

static void input_update_cursor(void) {
    int crow, ccol;
    input_get_cursor(&crow, &ccol);
    printty_cursor_move(sh_env.pt, crow, ccol);
}

static void input_redraw(void) {
    if (printty_cursor_move(sh_env.pt, input.srow, input.scol) == PRINTTY_ERR)
        xfatal("printty_cursor_move");

    int offset = 0;
    int crow, ccol;

    for (int i = 0; i < llen(); ++i) {
        input_get_cursor_offset(&crow, &ccol, offset++);

        if (crow == input.rows - 1 && ccol == input.cols - 1) {
            printf("%c\r\n", input.line[i]);
            --input.srow;
        } else
            printf("%c", input.line[i]);
    }

    printf(" ");
    printty_write(sh_env.pt, unibi_clr_eos);
    input_update_cursor();
}

static void input_move(int offset) {
    if (offset < 0)
        return;

    if (offset > llen())
        return;

    input.offset = offset;
    input_update_cursor();
}

static void input_insert(char c) {
    hda_insert_buf(input.line, input.offset++, 1, &c);
    input_redraw();
}

static void input_delete(void) {
    if (input.offset == 0)
        return;

    hda_delete(input.line, --input.offset, 1);
    input_redraw();
}

static void input_left(void) {
    input_move(input.offset - 1);
}

static void input_right(void) {
    input_move(input.offset + 1);
}

static void input_end(void) {
    input_move(llen());
}

static void input_start(void) {
    input_move(0);
}

static void input_clear_start(void) {
    hda_delete(input.line, 0, input.offset);
    input.offset = 0;
    input_redraw();
}

static void input_clear_end(void) {
    hda_delete(input.line, input.offset, llen() - input.offset);
    input_redraw();
}

typedef struct {
    int start, end;
} input_word;

static int next_non_ws(int i) {
    assert(i + 1 >= 0);

    ++i;

    for (; i < llen(); ++i)
        if (input.line[i] != ' ')
            return i;

    return -1;
}

static int next_ws(int i) {
    assert(i + 1 >= 0);

    ++i;

    for (; i < llen(); ++i)
        if (input.line[i] == ' ')
            return i;

    return -1;
}

static int prev_non_ws(int i) {
    assert(i - 1 < llen());

    --i;

    for (; i >= 0; --i)
        if (input.line[i] != ' ')
            return i;

    return -1;
}

static int prev_ws(int i) {
    assert(i - 1 < llen());

    --i;

    for (; i >= 0; --i)
        if (input.line[i] == ' ')
            return i;

    return -1;
}

static int next_word(int start, input_word *word) {
    assert(start >= 0);

    if (start == llen())
        return -1;

    int ws = start;

    if (input.line[ws] != ' ') {
        ws = next_ws(ws);
        if (ws == -1)
            return -1;
    }

    word->start = next_non_ws(ws);
    if (word->start == -1)
        return -1;

    word->end = next_ws(word->start);
    if (word->end == -1) {
        word->end = llen() - 1;
    } else
        --word->end;

    return 0;
}

static int prev_word(int start, input_word *word) {
    int ws = start;

    if (input.line[ws] != ' ') {
        ws = prev_ws(ws);
        if (ws == -1)
            return -1;
    }

    word->end = prev_non_ws(ws);
    if (word->end == -1)
        return -1;

    word->start = prev_ws(word->end);
    if (word->start == -1) {
        word->start = 0;
    } else
        ++word->start;

    return 0;
}

static int cur_word(int start, input_word *word) {
    if (input.line[start] == ' ')
        return -1;

    word->start = prev_ws(start);
    if (word->start == -1) {
        word->start = 0;
    } else
        ++word->start;

    word->end = next_ws(start);
    if (word->end == -1) {
        word->end = llen() - 1;
    } else
        --word->end;

    return 0;
}

static void insert_next_word(void) {
    input_word word;
    if (cur_word(input.offset, &word) == 0) {
        input_move(word.end + 1);
        return;
    }

    if (next_word(input.offset, &word) == -1) {
        input_move(llen());
        return;
    }

    input_move(word.end + 1);
}

static void insert_prev_word(void) {
    input_word word;
    if (prev_word(input.offset, &word) == -1) {
        input_move(0);
        return;
    }

    if (word.end + 1 == input.offset) {
        if (prev_word(word.start, &word) == -1) {
            input_move(0);
            return;
        }
    }

    input_move(word.end + 1);
}

static void normal_next_word(void) {
    input_word word;
    if (next_word(input.offset, &word) == -1) {
        input_move(llen() - 1);
        return;
    }

    input_move(word.start);
}

static void normal_prev_word(void) {
    input_word word;

    int rv = cur_word(input.offset, &word);

    if (rv == -1 || word.start == input.offset) {
        if (prev_word(input.offset, &word) == -1) {
            input_move(0);
        } else
            input_move(word.start);
    } else
        input_move(word.start);
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
        input_delete();

    } else if (key == PRINTTY_LEFT) {
        input_left();

    } else if (key == PRINTTY_RIGHT) {
        input_right();

    } else if (key == PRINTTY_SLEFT) {
        insert_prev_word();

    } else if (key == PRINTTY_SRIGHT) {
        insert_next_word();
    }
}

static void insert_mode_feed_unicode(uint32_t codepoint, char utf8[5]) {
    if (codepoint >= 32 && codepoint <= 126) {
        input_insert(utf8[0]);

    } else if (codepoint == 5/*C-e*/) {
        input_end();

    } else if (codepoint == 1/*C-a*/) {
        input_start();

    } else if (codepoint == 11/*C-k*/) {
        input_clear_end();

    } else if (codepoint == 21/*C-u*/) {
        input_clear_start();

    } else if (codepoint == 12/*C-l*/) {
        printty_write(sh_env.pt, unibi_clear_screen);
        display_prompt(sh_env.prompt_style);
        printty_get_cursor_pos(sh_env.pt, &input.srow, &input.scol);
        printf("%s", input.line);
        input_update_cursor();
        input.prompt_drawn = false;

    } else if (codepoint == 27/*ESC*/) {
        input_left();
        enter_mode_normal();
    }
}

static void normal_mode_feed_key(PrinttyInputKey key) {
    if (key == PRINTTY_LEFT) {
        input_left();

    } else if (key == PRINTTY_RIGHT) {
        if (input.offset < llen() - 1)
            input_move(input.offset + 1);
    }
}

static void normal_mode_feed_unicode(uint32_t codepoint, char _[5]) {
    if (codepoint == 'i') {
        enter_mode_insert();

    } else if (codepoint == 'a') {
        input_right();
        enter_mode_insert();

    } else if (codepoint == 'h') {
        input_left();

    } else if (codepoint == 'l') {
        if (input.offset < llen() - 1)
            input_right();

    } else if (codepoint == 'w') {
        normal_next_word();

    } else if (codepoint == 'b') {
        normal_prev_word();

    } else if (codepoint == 'x') {
        input_right();
        input_delete();
        if (input.offset == llen())
            input_left();

    } else if (codepoint == 'A') {
        input_end();
        enter_mode_insert();

    } else if (codepoint == 'I') {
        input_move(0);
        enter_mode_insert();
    }
}

char *get_line(void) {
    enter_mode_insert();

    if (printty_get_term_size(sh_env.pt, &input.rows, &input.cols)
            == PRINTTY_ERR)
        xfatal("printty_get_sterm_size");

    if (input.need_line) {
        input.need_line = false;
        input.line = NULL;
        hda_push(input.line, '\0');
    }

    if (input.prompt_drawn) {
        if (printty_get_cursor_pos(sh_env.pt, &input.srow, &input.scol)
                == PRINTTY_ERR)
            xfatal("printty_get_cursor_pos");

        input.prompt_drawn = false;
        printf("%s", input.line);
        input_update_cursor();
    }

    while (true) {
        PrinttyEvent pev;
        int rv = printty_get_event(sh_env.pt, &pev);
        if (rv == PRINTTY_EOF) {
            LOG_INFO("eof, exiting");
            exit(EXIT_SUCCESS);

        } else if (rv == PRINTTY_ERR) {
            xfatal("failed parsing line");

        } else if (rv == PRINTTY_SIG) {
            if (sigint_caught) {
                hda_free(input.line);
                input.need_line = true;
                input.offset = 0;
            }

            return NULL;
        }

        if (pev.type == PRINTTY_UNICODE) {
            if (input.insert_mode) {
                insert_mode_feed_unicode(pev.codepoint, pev.utf8);
            } else {
                normal_mode_feed_unicode(pev.codepoint, pev.utf8);
            }

        } else if (pev.type == PRINTTY_KEY) {
            if (pev.key == PRINTTY_ENTER)
                break;

            if (input.insert_mode) {
                insert_mode_feed_key(pev.key);
            } else
                normal_mode_feed_key(pev.key);

        } else if (pev.type == PRINTTY_RESIZE) {
            input_update_start_pos();
            input_update_cursor();
        }
    }

    LOG_INFO("parsed line: %s", input.line);
    printf("\n");
    input.need_line = true;
    input.offset = 0;

    return input.line;
}
