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

input_editor input = { .ccol = 0, .need_line = true };

void input_init(shell_env *env) {
    assert(env);
    if (!(env->pt = printty_create(NULL)))
        xfatal("printty_create");
    if (printty_cbreak(env->pt) == PRINTTY_ERR)
        xfatal("printty_cbreak");;
    printty_set_suspend_mask(env->pt, env->og_mask);
}

void input_deinit(shell_env *env) {
    assert(env);
    /* null when called from the exit builtin */
    if (env->pt) {
        printty_write(env->pt, unibi_exit_attribute_mode);
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

static void input_update_cursor(void) {
    printty_cursor_move(sh_env.pt, input.init_row,
            input.ccol + input.init_col);
}

static void input_insert(input_editor *edit, char c) {
    if (llen() + edit->init_col == edit->cols - 1)
        return;

    hda_insert_buf(edit->line, edit->ccol, 1, &c);
    printf("%s", &edit->line[edit->ccol++]);
    input_update_cursor();
}

static void input_delete(void) {
    if (llen() == 0 || input.ccol == 0)
        return;

    hda_delete(input.line, --input.ccol, 1);
    printty_write(sh_env.pt, unibi_cursor_left);
    printf("%s ", &input.line[input.ccol]);
    input_update_cursor();
}

static void input_left(void) {
    if (input.ccol == 0)
        return;

    --input.ccol;
    if (printty_write(sh_env.pt, unibi_cursor_left) == PRINTTY_ERR)
        xfatal("printty_write_cap");
}

static void input_right(void) {
    if (input.ccol == input.cols - 1 || input.ccol == llen())
        return;

    ++input.ccol;
    if (printty_write(sh_env.pt, unibi_cursor_right) == PRINTTY_ERR)
        xfatal("printty_write_cap");
}

char *get_line(void) {
    /* MAYBE CHECK IF INIT_COL CHANGED: CCOL COULD BE OFF */
    printty_get_term_size(sh_env.pt, &input.rows, &input.cols);

    if (input.need_line) {
        input.need_line = false;
        input.line = NULL;
        hda_push(input.line, '\0');
    }

    if (input.prompt_drawn) {
        input.prompt_drawn = false;
        printty_get_cursor_pos(sh_env.pt, &input.init_row, &input.init_col);
        LOG_INFO("cached curs[%d,%d]", input.init_row, input.init_col);

        /* warning for tests: if a background process prints between
         * the cursor position request and the subsequent cursor update
         * request (here), output can get wonky. */

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
                input.ccol = 0;
            }

            if (sigchld_caught)
                LOG_INFO("woke up for sigchld");
            return NULL;
        }

        if (pev.type == PRINTTY_UNICODE) {
            if (pev.codepoint >= 32 && pev.codepoint <= 126)
                input_insert(&input, pev.utf8[0]);

        } else if (pev.type == PRINTTY_KEY) {
            if (pev.key == PRINTTY_ENTER) {
                LOG_INFO("parsed line: %s", input.line);
                printf("\n");
                break;

            } else if (pev.key == PRINTTY_BACKSPACE) {
                input_delete();

            } else if (pev.key == PRINTTY_LEFT) {
                input_left();

            } else if (pev.key == PRINTTY_RIGHT) {
                input_right();
            }
        }
    }

    input.need_line = true;
    input.ccol = 0;

    return input.line;
}
