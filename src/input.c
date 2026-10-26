#include "linc/string.h"
#include <stdio.h>
#include <stdlib.h>
#include <linux/limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h> /* include before libgen.h! */
#include <libgen.h>

#undef basename

#include "input.h"
#include "state.h"
#include "utils.h"
#include "printty.h"
#include "variable.h"
#include "path_glob.h"
#include "lexer.h"

#include <linc/xsys.h>
#include <linc/hda.h>
#include <linc/hds.h>

#define MIN(a, b) ((a) < (b)) ? (a) : (b)
#define MAX(a, b) ((a) > (b)) ? (a) : (b)

#define DELETE_CURSOR 1
#define DELETE_WORD   2
#define DELETE_END    3
#define DELETE_START  4
#define DELETE_ALL    5
#define DELETE_RANGE  6

#define INSERT_CHAR  1
#define INSERT_STR   2
#define INSERT_PASTE 3

#define OP_NONE 0
#define OP_DEL  1

typedef char * linc_hds;

static Printty *pt;

typedef enum {
    COMP_CMD,
    COMP_ARG,
    COMP_REDIR
} comp_tok;

typedef struct {
    size_t i;
    size_t start_i;
    linc_hds glob;
} input_comp;

typedef struct {
    char **lines;
    char *saved;
    size_t i;
} input_hist;

typedef struct {
    size_t start_i;
    size_t n;
} input_del_ctx;

typedef struct {
    char *line;
    int start_row;
    int off;
    int rows, cols;
    int insert_mode;
    int start;
    input_comp comp;
    input_hist hist;
} input_editor;

static void default_sig_cb(void) {
    _exit(EXIT_FAILURE);
}

void (*input_sig_cb)(void) = default_sig_cb;

input_editor input = {0};

static int llen(void) {
    return (int)(linc_hda_size(input.line) - 1);
}

static int line_len(void) {
    int rv = (int)(linc_hda_size(input.line) - 1) - input.start;
    assert(rv >= 0);
    return rv;
}

static char *lstart(void) {
    return input.line + input.start;
}

static int next_non_ws(const char *line, int i) {
    ASSERT(line);
    ASSERT_GE_INT(i, 0);

    ++i;

    for (; i < (int)strlen(line); ++i)
        if (line[i] != ' ')
            return i;

    return -1;
}

static int next_ws(const char *line, int i) {
    ASSERT(line);
    ASSERT_GE_INT(i, 0);

    ++i;

    for (; i < (int)strlen(line); ++i)
        if (line[i] == ' ')
            return i;

    return -1;
}

static int prev_non_ws(const char *line, int i) {
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

    if (start == (int)strlen(line))
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

    if (start == (int)strlen(line))
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

static int input_current_tok(comp_tok *comp, int *start, int *end) {
    assert(comp && start && end);

    *comp = COMP_CMD;

    da_tok toks;
    if (lx_tokenize(lstart(), &toks) != LX_OK)
        return -1;

    for (size_t i = 0; i < toks.size; ++i) {
        lx_tok *tok = &toks.data[i];
        if (tok->kind != LX_TOK_WORD)
            continue;

        if (input.off >= tok->parts.data[0].start &&
            input.off <= tok->parts.data[tok->parts.size - 1].end)
        {
            *start = tok->parts.data[0].start;
            *end = tok->parts.data[tok->parts.size - 1].end;

            if (i == 0) {
                *comp = COMP_CMD;
            } else if (toks.data[i - 1].kind == LX_TOK_WORD) {
                *comp = COMP_ARG;
            } else if (toks.data[i - 1].kind == LX_TOK_RDR_OUT ||
                       toks.data[i - 1].kind == LX_TOK_RDR_IN ||
                       toks.data[i - 1].kind == LX_TOK_RDR_ERR ||
                       toks.data[i - 1].kind == LX_TOK_APPEND) {
                *comp = COMP_REDIR;
            } else {
                *comp = COMP_CMD;
            }

            lx_free(&toks);
            return 0;
        }
    }

    lx_free(&toks);
    return -1;
}

static void new_start_pos(void) {
    int row, col;
    xprintty_get_term_size(pt, &input.rows, &input.cols);
    xprintty_get_cursor_pos(pt, &row, &col);

    if (col != 0) {
        xprintty_write(pt, unibi_enter_reverse_mode);
        xprintty_write(pt, unibi_enter_bold_mode);
        printf("%%\n");
        xprintty_write(pt, unibi_exit_attribute_mode);
        /* we might have scrolled */
        xprintty_get_cursor_pos(pt, &row, &col);
    }

    input.start_row = row;

    LOG_INFO("new start pos: [%d,%d]", row, col);
    ASSERT_GE_INT(input.start_row, 0);
    ASSERT_G_INT(input.rows, input.start_row);
}

static void update_start_pos(void) {
    int crow, ccol;
    xprintty_get_term_size(pt, &input.rows, &input.cols);
    xprintty_get_cursor_pos(pt, &crow, &ccol);

    LOG_INFO("cursor[%d,%d], term[%d,%d]", crow, ccol, input.rows, input.cols);

    int rows_from_start = (input.start + input.off) / input.cols;
    input.start_row = crow - rows_from_start;

    LOG_INFO("rows_from_start=%d, input.start_row=%d",
            rows_from_start, input.start_row);

    if (input.start_row < 0) {
        LOG_INFO("start_row clamped to 0 (was %d)", input.start_row);
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

        if (line_i < input.start)
            xprintty_set_color(pt, PRINTTY_CYAN, -1);

        if (input.line[line_i] < 32 || input.line[line_i] > 126) {
            xprintty_set_color(pt, PRINTTY_BLACK, PRINTTY_RED);
            xprintty_write(pt, unibi_enter_bold_mode);
            printf("%c", '?');
            xprintty_write(pt, unibi_exit_attribute_mode);
        } else {
            printf("%c", input.line[line_i]);
        }

        if (line_i < input.start)
            xprintty_write(pt, unibi_exit_attribute_mode);

        if (ccol == input.cols - 1) {
            if (crow != rows - 1) {
                printf(" \x1b[1G");
            } else if (input.start_row > 0) {
                --input.start_row;
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
    xprintty_cursor_move(pt, input.start_row, 0);
    xprintty_write(pt, unibi_clr_eos);

    const char *pager_str = ">...";
    bool wrap_pending = input.cols <= (int)strlen(pager_str);

    if (paging) {
        while (input.start_row > 0) {
            xprintty_cursor_move(pt, input.rows - 1, 0);
            xprintty_write(pt, unibi_scroll_forward);
            --input.start_row;
        }

        xprintty_write(pt, unibi_cursor_home);

        xprintty_set_color(pt, PRINTTY_CYAN, -1);
        printf("%.*s", MIN(input.cols, (int)strlen(pager_str)), pager_str);
        xprintty_write(pt, unibi_exit_attribute_mode);

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

    xprintty_cursor_move(pt, crow, ccol);
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

static void input_delete(int type, input_del_ctx *ctx) {
    ASSERT_GE_INT(input.off, 0);
    ASSERT_GE_INT(line_len(), input.off);
    ASSERT(input.insert_mode || input.off < line_len() || line_len() == 0);

    if (line_len() == 0)
        return;

    if (type == DELETE_RANGE) {
        ASSERT(ctx);
        ASSERT_GE_INT(line_len() - input.off, ctx->n);
        ASSERT_GE_INT(ctx->start_i, 0);

        linc_hda_delete(input.line, input.start + ctx->start_i, ctx->n);
    } else if (input.insert_mode) {
        if (type == DELETE_CURSOR) {
            if (input.off == 0)
                return;

            linc_hda_delete(input.line, input.start + input.off - 1, 1);
            input.off -= 1;
        } else if (type == DELETE_END) {
            if (input.off == line_len())
                return;

            linc_hda_delete(input.line, input.start + input.off,
                    line_len() - input.off);
        } else if (type == DELETE_START) {
            if (input.off == 0)
                return;

            linc_hda_delete(input.line, input.start, input.off);
            input.off = 0;
        } else if (type == DELETE_ALL) {
            linc_hda_delete(input.line, input.start, line_len());
            input.off = 0;
        } else if (type == DELETE_WORD) {
            input_word word;

            if (cur_word(lstart(), input.off, &word) == -1)
                return;

            linc_hda_delete(input.line, word.start + input.start,
                    (word.end - word.start) + 1);

            input.off = word.start;
        }

    } else {
        if (type == DELETE_CURSOR) {
            linc_hda_delete(input.line, input.start + input.off, 1);
            if (input.off == line_len() && input.off != 0)
                input.off -= 1;
        } else if (type == DELETE_ALL) {
            linc_hda_delete(input.line, input.start, line_len());
            input.off = 0;
        } else if (type == DELETE_WORD) {
            input_word word;

            if (cur_word(lstart(), input.off, &word) == -1)
                return;

            linc_hda_delete(input.line, word.start + input.start,
                    (word.end - word.start) + 1);

            input.off = word.start;

            if (input.off > 0)
                --input.off;
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
            linc_hda_insert_buf(input.line, input.start + input.off, 1, s);
            input.off += 1;
        } else if (type == INSERT_STR) {
            linc_hda_insert_buf(input.line, input.start + input.off, strlen(s), s);
            input.off += strlen(s);
        }
    }

    input_update();
}

static void enter_mode_normal() {
    xprintty_cstyle(pt, PRINTTY_CBLOCK(false));
    input.insert_mode = false;
}

static void enter_mode_insert() {
    xprintty_cstyle(pt, PRINTTY_CBEAM(false));
    input.insert_mode = true;
}

static void input_reset_comp(void) {
    if (input.comp.glob) {
        input.comp.i = 0;
        input.comp.start_i = 0;
        linc_hds_free(input.comp.glob);
        input.comp.glob = nullptr;
        input_update();
    }
}

static void get_path_entries(da_charp *pents) {
    assert(pents);
    *pents = (da_charp){0};

    sh_var *var = lookup_var(&sh_env.vars, "PATH");
    char *path_str = xstrdup(var->value);

    char *tok = strtok(path_str, ":");
    if (!tok)
        xfatal("unexpected strtok return value");

    do {
        *linc_array_push(pents) = xstrdup(tok);
    } while ((tok = strtok(NULL, ":")));

    free(path_str);
}

static void escape_spaces(linc_string *str) {
    linc_string out = LINC_STRING_INIT;

    for (char *c = str->cstr; *c != '\0'; ++c) {
        if (*c == ' ') {
            linc_string_append(&out, "\\ ");
        } else
            linc_string_append_n(&out, c, 1);
    }

    linc_string_free(str);
    *str = out;
}

static void insert_mode_feed_key(PrinttyInputKey key) {
    if (key == PRINTTY_BACKSPACE) {
        if (input.comp.glob)
            input_reset_comp();

        input_delete(DELETE_CURSOR, NULL);
    } else if (key == PRINTTY_LEFT) {
        input_move(input.off - 1);
    } else if (key == PRINTTY_RIGHT) {
        input_move(input.off + 1);
    }
}

static void insert_mode_feed_unicode(uint32_t codepoint, char utf8[5]) {
    if (input.comp.glob && codepoint == '/') {
        input_reset_comp();
        return;
    }

    if (codepoint != 9/*TAB*/)
        input_reset_comp();
    if (codepoint == 5/*C-e*/) {
        input_move(line_len());
    } else if (codepoint == 1/*C-a*/) {
        input_move(0);
    } else if (codepoint == 11/*C-k*/) {
        input_delete(DELETE_END, NULL);
    } else if (codepoint == 21/*C-u*/) {
        input_delete(DELETE_START, NULL);
    } else if (codepoint == 12/*C-l*/) {
        xprintty_write(pt, unibi_clear_screen);
        new_start_pos();
        input_update();
    } else if (codepoint == 27/*ESC*/) {
        input_move(input.off - 1);
        enter_mode_normal();
    } else if (codepoint == 9/*TAB*/) {
        int tstart, tend;
        comp_tok tok;

        if (input_current_tok(&tok, &tstart, &tend) == -1) {
            input_reset_comp();
            return;
        }

        if (!input.comp.glob) {
            linc_hds_append_n(input.comp.glob, lstart() + tstart, tend - tstart);
            linc_hds_append_n(input.comp.glob, "*", 1);
            input.comp.start_i = tstart;

            LOG_INFO("new comp tok = \"%.*s\", [%d,%d]",
                    tend - tstart, lstart() + tstart, tstart, tend);
        }

        da_charp matches = {0};
        da_charp pents = {0};

        if (tok == COMP_CMD) {
            get_path_entries(&pents);

            for (size_t i = 0; i < pents.size; ++i) {
                char *glob = nullptr;
                linc_hds_append(glob, pents.data[i]);
                linc_hds_append(glob, "/");
                linc_hds_append(glob, input.comp.glob);
                LOG_INFO("PATH glob: %s", glob);

                glob_pattern(glob, linc_hds_len(glob), &matches);
                linc_hds_free(glob);

                if (matches.size != 0)
                    break;
            }

            free_charp_arr(&pents);
        } else if (tok == COMP_ARG || tok == COMP_REDIR) {
            if (matches.size == 0)
                glob_pattern(input.comp.glob,
                        linc_hds_len(input.comp.glob), &matches);

            LOG_INFO("File glob: %s", input.comp.glob);
        }

        if (matches.size == 0)
            return;

        for (size_t i = 0; i < matches.size; ++i) {
            char *glob_copy = xstrdup(input.comp.glob);
            char *glob_dirname = dirname(glob_copy);
            bool path_pre = (strcmp(glob_dirname, ".") != 0);

            bool append_slash = is_dir(matches.data[i]);
            char *bn = basename(matches.data[i]);

            linc_string result = LINC_STRING_INIT;

            if (path_pre)
                linc_string_appendf(&result, "%s/", glob_dirname);

            linc_string_append(&result, bn);

            if (append_slash)
                linc_string_append(&result, "/");

            free(matches.data[i]);
            free(glob_copy);

            escape_spaces(&result);
            matches.data[i] = result.cstr;
        }

        input_move(input.comp.start_i);
        input_delete(DELETE_RANGE, &(input_del_ctx){ tstart, tend - tstart });
        input_insert(INSERT_STR, matches.data[input.comp.i++ % matches.size]);
        free_charp_arr(&matches);
    } else {
        input_insert(INSERT_CHAR, &utf8[0]);
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
            input_delete(DELETE_ALL, NULL);
        else if (codepoint == 'w')
            input_delete(DELETE_WORD, NULL);
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
        input_delete(DELETE_CURSOR, NULL);
    } else if (codepoint == 'A') {
        enter_mode_insert();
        input_move(line_len());
    } else if (codepoint == 'I') {
        enter_mode_insert();
        input_move(0);
    } else if (codepoint == 'b') {
        if (cur_word(lstart(), input.off, &word) != -1 &&
                input.off != word.start) {
            input_move(word.start);

        } else if (prev_word(lstart(), input.off, &word) == -1) {
            input_move(0);
        } else
            input_move(word.start);
    } else if (codepoint == 'w') {
        if (next_word(lstart(), input.off, &word) == -1) {
            input_move(line_len() - 1);
        } else
            input_move(word.start);
    } else if (codepoint == 'e') {
        if (cur_word(lstart(), input.off, &word) != -1 &&
                input.off != word.end) {
            input_move(word.end);

        } else if (next_word(lstart(), input.off, &word) == -1) {
            input_move(line_len() - 1);
        } else
            input_move(word.end);
    } else if (codepoint == 'd') {
        op = OP_DEL;
    } else if (codepoint == 'y') {
        printty_clipboard_copy(pt, input.line + input.start);
    }
}

static void history_push(const char *line) {
    linc_hda_insert(input.hist.lines, 0, 1, { linc_hda_dup(line) });
    input.hist.i = 0;

    if (input.hist.saved) {
        linc_hda_free(input.hist.saved);
        input.hist.saved = nullptr;
    }

    LOG_INFO("history push: \"%s\"", line);
}

static void history_prev(void) {
    if (input.hist.i == 0) {
        if (input.hist.saved) {
            linc_hda_free(input.line);
            input.line = input.hist.saved;
            input.hist.saved = nullptr;
            input.off = line_len();
        }

        return;
    }

    linc_hda_free(input.line);
    input.line = linc_hda_dup(input.hist.lines[--input.hist.i]);
    input.off = line_len();
}

static void history_next(void) {
    if (linc_hda_size(input.hist.lines) == 0)
        return;
    if (input.hist.i == linc_hda_size(input.hist.lines) - 1 && input.hist.saved)
        return;

    if (!input.hist.saved) {
        input.hist.saved = input.line;
    } else {
        linc_hda_free(input.line);
        ++input.hist.i;
    }

    input.line = linc_hda_dup(input.hist.lines[input.hist.i]);
    input.off = line_len();
    LOG_INFO("history_next: input.line = \"%s\"", input.line);
}

void input_deinit(void) {
    if (pt) {
        xprintty_write(pt, unibi_exit_attribute_mode);
        xprintty_write_name(pt, "Se");
        printty_destroy(pt);
    }

    pt = NULL;
}

void input_init(void) {
    if (!(pt = printty_create(NULL)))
        xfatal("printty_create");
    if (printty_cbreak(pt) == PRINTTY_ERR)
        xfatal("printty_cbreak");

    printty_set_esc_delay(pt, 25);
}

void input_set_suspend_mask(sigset_t suspend_mask) {
    printty_set_suspend_mask(pt, suspend_mask);
}

void input_on_new_line(void) {
    if (!input.line)
        return;

    new_start_pos();
    input_update();
    enter_mode_insert();
}

void input_reset_line_state(void) {
    if (!input.line)
        return;

    linc_hda_delete(input.line, input.start, line_len());
    input.off = 0;

    new_start_pos();
    input_update();
    enter_mode_insert();
}

void input_clear_history(void) {
    size_t n = linc_hda_size(input.hist.lines);

    for (size_t i = 0; i < n; ++i)
        linc_hda_free(input.hist.lines[i]);

    linc_hda_delete(input.hist.lines, 0, n);
}

char *input_get_line(const char *prompt) {
    if (printty_start(pt) != PRINTTY_OK)
        return NULL;

    linc_hds_append(input.line, prompt);
    input.start = linc_hda_size(input.line) - 1;

    input_on_new_line();

    LOG_INFO("input: new prompt: \"%s\"", input.line);

    while (true) {
        PrinttyEvent pev;
        int rv = printty_get_event(pt, &pev);

        if (rv == PRINTTY_EOF || rv == PRINTTY_ERR)
            return NULL;

        else if (rv == PRINTTY_SIG)
            input_sig_cb();

        else if (pev.type == PRINTTY_UNICODE) {
            if (input.insert_mode) {
                insert_mode_feed_unicode(pev.cp, pev.utf8);
            } else {
                normal_mode_feed_unicode(pev.cp, pev.utf8);
            }
        } else if (pev.type == PRINTTY_KEY) {
            if (pev.key == PRINTTY_ENTER) {
                if (line_len() > 0)
                    history_push(input.line);

                input_reset_comp();
                printf("\n");
                break;
            } else if (pev.key == PRINTTY_UP) {
                history_next();
                input_update();
                continue;
            } else if (pev.key == PRINTTY_DOWN) {
                history_prev();
                input_update();
                continue;
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
        } else if (pev.type == PRINTTY_PASTE_BEGIN) {
            char *paste_data;
            if (!(paste_data = printty_res_paste(pt))) {
                LOG_WARN("timed out waiting for paste");
                continue;
            }

            input_insert(INSERT_STR, paste_data);
            linc_hda_free(paste_data);
        }
    }

    LOG_INFO("parsed line: %s", input.line);

    if (printty_stop(pt) != PRINTTY_OK)
        return NULL;

    char *out = xstrdup(lstart());

    linc_hda_free(input.line);
    input.line = nullptr;
    input.off = 0;

    return out;
}
