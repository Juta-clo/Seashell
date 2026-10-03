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
        xfatal("printty_cbreak");
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
    printty_cursor_move(sh_env.pt, input.srow,
            input.ccol + input.scol);
}

static void input_move(int n) {
    if (n < 0)
        return;

    if (n + input.scol >= input.cols - 1 || n > llen())
        return;

    input.ccol = n;
    input_update_cursor();
}

static void input_insert(input_editor *edit, char c) {
    if (llen() + edit->scol == edit->cols - 1)
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
    input_move(input.ccol - 1);
}

static void input_right(void) {
    input_move(input.ccol + 1);
}

static void input_end(void) {
    input_move(llen());
}

static void input_start(void) {
    input_move(0);
}

static void input_clear_start(void) {
    int n = input.ccol;
    for (int i = 0; i < n; ++i)
        input_delete();
}

static void input_clear_end(void) {
    int n = llen() - input.ccol;
    input_end();

    for (int i = 0; i < n; ++i)
        input_delete();
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
    if (cur_word(input.ccol, &word) == 0) {
        input_move(word.end + 1);
        return;
    }

    if (next_word(input.ccol, &word) == -1) {
        input_move(llen());
        return;
    }

    input_move(word.end + 1);
}

static void insert_prev_word(void) {
    input_word word;
    if (prev_word(input.ccol, &word) == -1) {
        input_move(0);
        return;
    }

    if (word.end + 1 == input.ccol) {
        if (prev_word(word.start, &word) == -1) {
            input_move(0);
            return;
        }
    }

    input_move(word.end + 1);
}

static void normal_next_word(void) {
    input_word word;
    if (next_word(input.ccol, &word) == -1) {
        input_move(llen());
        return;
    }

    input_move(word.start);
}

static void normal_prev_word(void) {
    input_word word;

    int rv = cur_word(input.ccol, &word);

    if (rv == -1 || word.start == input.ccol) {
        if (prev_word(input.ccol, &word) == -1) {
            input_move(0);
        } else
            input_move(word.start);
    } else
        input_move(word.start);
}

char *get_line(void) {
    input.insert_mode = true;

    printty_get_term_size(sh_env.pt, &input.rows, &input.cols);

    if (input.need_line) {
        input.need_line = false;
        input.line = NULL;
        hda_push(input.line, '\0');
    }

    if (input.prompt_drawn) {
        input.prompt_drawn = false;
        printty_get_cursor_pos(sh_env.pt, &input.srow, &input.scol);

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

            return NULL;
        }

        if (pev.type == PRINTTY_UNICODE) {

            if (input.insert_mode) {
                if (pev.codepoint >= 32 && pev.codepoint <= 126) {
                    input_insert(&input, pev.utf8[0]);
                    continue;
                }

                switch (pev.codepoint) {
                case 5 /*C-e*/: input_end();               break;
                case 1 /*C-a*/: input_start();             break;
                case 11/*C-k*/: input_clear_end();         break;
                case 21/*C-u*/: input_clear_start();       break;
                case 27/*ESC*/: input.insert_mode = false; break;
                case 12/*C-l*/:
                    printty_write(sh_env.pt, unibi_clear_screen);
                    display_prompt(sh_env.prompt_style);
                    printty_get_cursor_pos(sh_env.pt, &input.srow, &input.scol);
                    printf("%s", input.line);
                    input_update_cursor();
                    input.prompt_drawn = false;
                    break;
                default:
                    break;
                }
            } else /* if (NORMAL MODE) */ {
                if (pev.codepoint == 'i')
                    input.insert_mode = true;
                else if (pev.codepoint == 'h')
                    input_left();
                else if (pev.codepoint == 'l')
                    input_right();
                else if (pev.codepoint == 'w')
                    normal_next_word();
                else if (pev.codepoint == 'b')
                    normal_prev_word();
            }

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

            } else if (pev.key == PRINTTY_SLEFT) {
                insert_prev_word();

            } else if (pev.key == PRINTTY_SRIGHT) {
                insert_next_word();
            }
        }
    }

    input.need_line = true;
    input.ccol = 0;

    return input.line;
}
