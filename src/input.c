#include <stdio.h>
#include <stdlib.h>
#include <linux/limits.h>
#include <string.h> // IWYU pragma: keep
#include <unistd.h>
#include <fcntl.h>

#include "linc_tools.h"
#include "input.h"
#include "shell_state.h"
#include "utils.h"
#include "printty.h"

typedef struct {
    char *line;
    int init_col;
    int crow;
    int ccol;
    int rows, cols;
} input_editor;

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
}

static int llen(input_editor *edit) {
    return (int)(hds_len(edit->line));
}

static void input_update_cursor(input_editor *edit) {
    printty_cursor_move(sh_env.pt, edit->crow, edit->ccol + edit->init_col);
}

static void input_insert(input_editor *edit, char c) {
    if (llen(edit) + edit->init_col == edit->cols - 1)
        return;

    hda_insert_buf(edit->line, edit->ccol, 1, &c);
    printf("%s", &edit->line[edit->ccol++]);
    input_update_cursor(edit);
}

static void input_delete(input_editor *edit) {
    if (llen(edit) == 0 || edit->ccol == 0)
        return;

    hda_delete(edit->line, --edit->ccol, 1);
    printty_write(sh_env.pt, unibi_cursor_left);
    printf("%s ", &edit->line[edit->ccol]);
    input_update_cursor(edit);
}

static void input_left(input_editor *edit) {
    if (edit->ccol == 0)
        return;

    --edit->ccol;
    if (printty_write(sh_env.pt, unibi_cursor_left) == PRINTTY_ERR)
        xfatal("printty_write_cap");
}

static void input_right(input_editor *edit) {
    if (edit->ccol == edit->cols - 1 || edit->ccol == llen(edit))
        return;

    ++edit->ccol;
    if (printty_write(sh_env.pt, unibi_cursor_right) == PRINTTY_ERR)
        xfatal("printty_write_cap");
}

char *get_line(void) {
    PrinttyEvent pev;
    input_editor edit = {0};
    hda_push(edit.line, '\0');

    printty_get_cursor_pos(sh_env.pt, &edit.crow, &edit.init_col);
    printty_get_term_size(sh_env.pt, &edit.rows, &edit.cols);

    while (true) {
        int rv = printty_get_event(sh_env.pt, &pev);
        if (rv == PRINTTY_EOF) {
            LOG_INFO("eof, exiting");
            exit(EXIT_SUCCESS);

        } else if (rv == PRINTTY_ERR) {
            xfatal("failed parsing line");

        } else if (rv == PRINTTY_SIG) {
            hda_free(edit.line);
            return NULL;
        }

        if (pev.type == PRINTTY_UNICODE) {
            if (pev.codepoint >= 32 && pev.codepoint <= 126)
                input_insert(&edit, pev.utf8[0]);

        } else if (pev.type == PRINTTY_KEY) {
            if (pev.key == PRINTTY_ENTER) {
                break;

            } else if (pev.key == PRINTTY_BACKSPACE) {
                input_delete(&edit);

            } else if (pev.key == PRINTTY_LEFT) {
                input_left(&edit);

            } else if (pev.key == PRINTTY_RIGHT) {
                input_right(&edit);
            }
        }
    }

    LOG_INFO("parsed line: %s", edit.line);

    printf("\n");
    return edit.line;
}
