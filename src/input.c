#include "builtins.h"
#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <linux/limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h> /* include before libgen.h! */
#include <libgen.h>

#undef basename

#include "linc/xsys.h"
#include "linc/hda.h"
#include "linc/hds.h"

#include "input.h"
#include "input_internal.h"
#include "expander.h"
#include "linc/string.h"
#include "input.h"
#include "state.h"
#include "utils.h"
#include "printty.h"
#include "variable.h"
#include "path_glob.h"
#include "lexer.h"

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

static Printty *pt = nullptr;

typedef enum {
    COMP_NAME,
    COMP_ARG,
    COMP_REDIR,
    COMP_EMPTY,
} comp_tok_kind;

typedef struct {
    comp_tok_kind kind;
    const char *text;
    int i, n;
    int off;

    ps_word *word;
    ps_ast ast;
} comp_tok;

typedef struct {
    char *data;
    int i;
    int n;
    bool hl;
    bool attr_off;
    int fg;
    int bg;
    bool bold;
    bool under;
} input_cell;

LINC_ARRAY_TYPE(da_cell, input_cell);

typedef struct {
    da_cell cells;
    int prompt_len;
} input_buf;

typedef struct {
    size_t i;
    linc_hds glob;
    linc_hds orig;
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
    int start_i;
    int prompt_len;
    int off;
    int rows, cols;
    int insert_mode;
    input_comp comp;
    input_hist hist;
} input_editor;

static void default_sig_cb(void) {
    _exit(EXIT_FAILURE);
}

void (*input_sig_cb)(void) = default_sig_cb;

input_editor input = {0};

static void log_cells(da_cell cells) {
    return;

    LOG_INFO("collected %zu cells {", cells.size);

    for (size_t i = 0; i < cells.size; ++i) {
        input_cell cell = cells.data[i];
        LOG_INFO("    cell(n=%d) = %s", cell.n, cell.data);
    }

    LOG_INFO("}");
}

static int line_len(void) {
    int rv = (int)(linc_hda_size(input.line) - 1) - input.start_i;
    ASSERT(rv >= 0);
    return rv;
}

static char *lstart(void) {
    return input.line + input.start_i;
}

static void ncol_cell(int *crow, int *ccol, int start_row, int col_n) {
    ASSERT_(GEQ, col_n, 0);
    ASSERT(crow && ccol);

    *crow = start_row;
    *ccol = col_n;

    while (input.cols <= *ccol) {
        *ccol -= input.cols;
        ++*crow;
    }
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
    ASSERT_(GEQ, input.start_row, 0);
    ASSERT_(G, input.rows, input.start_row);
}

static void update_start_pos(void) {
    int crow, ccol;
    xprintty_get_term_size(pt, &input.rows, &input.cols);
    xprintty_get_cursor_pos(pt, &crow, &ccol);

    LOG_INFO("cursor[%d,%d], term[%d,%d]", crow, ccol, input.rows, input.cols);

    int rows_from_start = (input.prompt_len + input.off) / input.cols;
    input.start_row = crow - rows_from_start;

    LOG_INFO("rows_from_start=%d, input.start_row=%d",
            rows_from_start, input.start_row);

    if (input.start_row < 0) {
        LOG_INFO("start_row clamped to 0 (was %d)", input.start_row);
        input.start_row = 0;
    }

    LOG_INFO("new start row = %d", input.start_row);
    ASSERT_(G, input.rows, input.start_row);
}

static void free_index_tok(comp_tok *ctok) {
    ps_free(&ctok->ast);
}

static int input_index_tok(comp_tok *ctok, int ind) {
    ASSERT(ctok);

    *ctok = (comp_tok){0};
    da_tok toks = {0};
    ps_info info = {0};
    ps_scan scan = {
        .print_err = false,
        .alias = false,
        .toks = &toks
    };

    lx_tokenize(lstart(), &toks);
    ps_parse(&scan, &ctok->ast);
    ps_get_info(ind, &ctok->ast, &info);
    lx_free(&toks);

    if (info.kinds & PS_NAME || info.kinds & PS_ARG) {
        if (expand_word(info.word, nullptr, EXPAND_COMP) == -1)
            return -1;

        ctok->kind = (info.kinds & PS_NAME) ? COMP_NAME : COMP_ARG;
        ctok->text = info.word->arg;
        ctok->i = info.word->i;
        ctok->n = info.word->n;
        ctok->word = info.word;
    } else if (info.kinds & PS_REDIR) {
        if (expand_word(&info.redir->target, nullptr, EXPAND_COMP) == -1)
            return -1;

        ctok->kind = COMP_REDIR;
        ctok->text = info.redir->target.arg;
        ctok->i = info.redir->target.i;
        ctok->n = info.redir->target.n;
        ctok->word = &info.redir->target;
    } else {
        ctok->kind = COMP_EMPTY;
        ctok->text = "";
        ctok->i = input.off;
        ctok->n = 0;
    }

    return 0;
}

static void get_path_entries(da_charp *pents) {
    ASSERT(pents);
    *pents = (da_charp){0};

    sh_var *var = lookup_var(&env.vars, "PATH");
    if (!var)
        return;

    char *path_str = xstrdup(var->value);

    char *tok = strtok(path_str, ":");
    if (!tok)
        fatal("unexpected strtok return value");

    do {
        *linc_array_push(pents) = xstrdup(tok);
    } while ((tok = strtok(nullptr, ":")));

    free(path_str);
}

static da_charp build_path_candidates(const char *name) {
    da_charp pents = {0};
    get_path_entries(&pents);

    for (size_t i = 0; i < pents.size; ++i) {
        linc_string candidate = LINC_STRING_INIT;
        linc_string_append(&candidate, pents.data[i]);
        linc_string_append(&candidate, "/");
        linc_string_append(&candidate, name);

        free(pents.data[i]);
        pents.data[i] = candidate.cstr;
    }

    return pents;
}

static void input_complete_command(const char *cmd, da_charp *hits) {
    ASSERT(hits);

    *hits = (da_charp){0};

    da_charp candidates = build_path_candidates(cmd);

    for (size_t i = 0; i < candidates.size; ++i) {
        char *glob = candidates.data[i];
        da_charp path_hits;

        glob_pattern(glob, strlen(glob), &path_hits);

        memcpy(
            linc_array_insert(hits, hits->size, path_hits.size),
            path_hits.data, sizeof(char *) * path_hits.size
        );

        linc_array_free(&path_hits);
    }

    argv_free_free(&candidates);

    for (size_t i = 0; i < hits->size; ++i) {
        char *match = hits->data[i];
        hits->data[i] = xstrdup(basename(match));
        free(match);
    }
}

static void escape_spaces(linc_string *str) {
    linc_string out = LINC_STRING_INIT;

    for (char *c = str->cstr; *c != '\0'; ++c) {
        if (*c == ' ') {
            if (c == str->cstr || c[-1] != '\\') {
                linc_string_append(&out, "\\ ");
            } else {
                linc_string_append(&out, " ");
            }
        } else
            linc_string_append_n(&out, c, 1);
    }

    linc_string_free(str);
    *str = out;
}

static void input_complete_file(const char *glob, const char *cli,
        da_charp *hits) {
    glob_pattern(glob, strlen(glob), hits);

    for (size_t i = 0; i < hits->size; ++i) {
        const char *orig = cli;
        linc_string result = LINC_STRING_INIT;
        char *orig_copy = xstrdup(cli);
        char *hit_copy = hits->data[i];

        if (strstr(orig, "*")) {
            linc_string_append(&result, hit_copy);
        } else {
            char *dir_prefix = dirname(orig_copy);

            if (orig[strlen(orig) - 1] == '/') {
                linc_string_appendf(&result, "%s", orig);
            } else if (!STREQ(dir_prefix, ".")) {
                linc_string_append(&result, dir_prefix);
                linc_string_append(&result, "/");
            } else if (strncmp(orig, "./", 2) == 0) {
                linc_string_append(&result, "./");
            }

            linc_string_append(&result, basename(hit_copy));
        }

        if (is_dir(hits->data[i]))
            linc_string_append(&result, "/");

        escape_spaces(&result);

        hits->data[i] = result.cstr;

        free(hit_copy);
        free(orig_copy);
    }
}

static void input_draw_ch_cell(char c, input_cell *hl) {
    if (c >= 32 && c < 127) {
        printf("%c", c);
    } else {
        xprintty_write(pt, unibi_exit_attribute_mode);

        xprintty_set_color(pt, PRINTTY_BLACK, PRINTTY_RED);
        xprintty_write(pt, unibi_enter_bold_mode);
        printf("%c", '?');
        xprintty_write(pt, unibi_exit_attribute_mode);

        if (hl && !hl->attr_off) {
            xprintty_set_color(pt, hl->fg, hl->bg);

            if (hl->bold)
                xprintty_write(pt, unibi_enter_bold_mode);
            if (hl->under)
                xprintty_write(pt, unibi_enter_underline_mode);
        }
    }
}

static void free_cells(input_buf *ibuf) {
    for (size_t i = 0; i < ibuf->cells.size; ++i)
        free(ibuf->cells.data[i].data);

    linc_array_free(&ibuf->cells);
}

static void push_cell(char *data, size_t n, size_t i, da_cell *cells) {
    input_cell *cell = linc_array_push(cells);

    *cell = (input_cell){
        .n = n,
        .i = i,
        .data = xstrndup(data, n)
    };
}

static input_buf get_cells(void) {
    input_buf ibuf = {0};
    bool pending_csi = false;
    bool pending_esc = false;
    int i = 0;
    int n = 0;

    for (char *b = input.line; *b != '\0'; ++b, ++i) {
        if (i >= input.start_i) {
            /* Push unfinished prompt csi characters literally */
            for (; n > 0; --n)
                push_cell(&input.line[i - n], 1, i - n, &ibuf.cells);

            push_cell(&input.line[i], 1, i, &ibuf.cells);
        } else if (pending_csi) {
            ++n;

            if (*b >= '@' && *b <= '~') {
                pending_csi = false;
                int csi_i = i - (n - 1);
                push_cell(&input.line[csi_i], n, csi_i, &ibuf.cells);
                n = 0;
            }
        } else if (pending_esc) {
            pending_esc = false;

            if (*b == '[') {
                pending_csi = true;
                n = 2;
            } else {
                push_cell(&input.line[i - 1], 1, i - 1, &ibuf.cells);
                push_cell(&input.line[i], 1, i, &ibuf.cells);
            }
        } else if (*b == '\x1b') {
            pending_esc = true;
        } else {
            ++ibuf.prompt_len;
            push_cell(&input.line[i], 1, i, &ibuf.cells);
        }
    }

    log_cells(ibuf.cells);
    return ibuf;
}

static void input_draw(int rows, int start_row, da_cell cells) {
    int scr_i = 0;
    size_t cell_content_start_i = start_row * input.cols;
    int crow, ccol;
    input_cell *last_hl = nullptr;

    for (size_t i = 0; i < cells.size; ++i) {
        input_cell cell = cells.data[i];

        if (cell.hl) {
            if (cell.attr_off) {
                xprintty_write(pt, unibi_exit_attribute_mode);
            } else {
                xprintty_set_color(pt, cell.fg, cell.bg);

                if (cell.bold)
                    xprintty_write(pt, unibi_enter_bold_mode);
                if (cell.under)
                    xprintty_write(pt, unibi_enter_underline_mode);
            }

            last_hl = &cells.data[i];
            ++cell_content_start_i;
        } else if (cell.n > 1) {
            printf("%s", cell.data);
            ++cell_content_start_i;
        } else if (i >= cell_content_start_i && scr_i < input.cols * rows) {
            ncol_cell(&crow, &ccol, input.start_row, scr_i++);
            input_draw_ch_cell(*cell.data, last_hl);

            if (ccol == input.cols - 1) {
                if (crow != rows - 1) {
                    printf(" \x1b[1G");
                } else if (input.start_row > 0) {
                    --input.start_row;
                    printf(" \x1b[1G");
                }
            }
        }
    }
}

static int cell_index(int *start, int *end, da_cell *cells) {
    ASSERT(start);
    ASSERT(end);
    ASSERT(cells);

    int tok_start = *start + input.start_i;
    int tok_end = *end + input.start_i;

    for (size_t i = 0; i < cells->size; ++i) {
        input_cell *cell = &cells->data[i];

        if (tok_start == cell->i)
            *start = i;

        if (tok_end == cell->i) {
            *end = i + 1;
            return 0;
        }
    }

    return -1;
}

static void input_add_highlight(int start, int end, int fg, int bg, bool bold,
        bool under, input_buf *ibuf) {
    ASSERT(ibuf);

    input_cell *cell = linc_array_insert(&ibuf->cells, start, 1);
    *cell = (input_cell){
        .hl = true,
        .fg = fg,
        .bg = bg,
        .bold = bold,
        .under = under
    };

    cell = linc_array_insert(&ibuf->cells, end + 1, 1);
    *cell = (input_cell){
        .hl = true,
        .attr_off = true
    };
}

static void try_highlight_word(lx_tok *word, input_buf *ibuf) {
    ASSERT(word);
    ASSERT(ibuf);

    comp_tok ctok = {0};
    da_charp hits = {0};
    bool name_found = false;
    bool path_found = false;

    int rv = input_index_tok(&ctok, word->i);
    if (rv == -1 || ctok.kind == COMP_EMPTY) {
        free_index_tok(&ctok);
        return;
    }

    if (*ctok.text != '\0') {
        linc_string glob = LINC_STRING_INIT;
        linc_string_appendf(&glob, "%s*", ctok.text);
        glob_pattern(glob.cstr, glob.len, &hits);

        for (size_t i = 0; i < hits.size; ++i) {
            if (is_dir(hits.data[i]) || is_file(hits.data[i]))
                path_found = true;
        }

        argv_free_free(&hits);
        linc_string_free(&glob);
    }

    if (ctok.kind == COMP_NAME) {
        if (strstr(ctok.text, "/")) {
            if (is_bin(ctok.text))
                name_found = true;
        } else if (get_builtin(ctok.text)) {
            name_found = true;
        } else if (mp_str_lookup(&env.aliases, ctok.text, NULL) != -1) {
            name_found = true;
        } else {
            da_charp candidates = build_path_candidates(ctok.text);

            for (size_t i = 0; i < candidates.size; ++i) {
                if (is_bin(candidates.data[i])) {
                    name_found = true;
                    break;
                }
            }

            argv_free_free(&candidates);
        }

        if (!name_found) {
            int start = word->i;
            int end = word->i + (word->n - 1);
            int fg = PRINTTY_RED;
            bool bold = true;
            bool under = false;

            if (path_found && is_bin(ctok.text)) {
                fg = -1;
                under = true;
                bold = false;
            }

            if (cell_index(&start, &end, &ibuf->cells) != -1)
                input_add_highlight(start, end, fg, -1, bold, under, ibuf);

            free_index_tok(&ctok);
            return;
        }
    }

    for (size_t i = 0; i < word->parts.size; ++i) {
        lx_part *part = &word->parts.data[i];

        int start = part->i;
        int end = part->i + (part->n - 1);
        int fg = -1;
        bool under = false;
        bool bold = false;

        if (part->quote == LX_Q_NONE) {

            if (ctok.kind == COMP_NAME) {
                if (name_found) {
                    fg = PRINTTY_GREEN;
                    bold = false;
                    under = false;
                } else if (path_found) {
                    fg = -1;
                    bold = false;
                    under = true;
                }
            } else if (ctok.kind == COMP_ARG) {
                if (part->raw[0] == '-' && part->raw[1] != '-')
                    fg = PRINTTY_CYAN;

                if (part->raw[0] == '-' && part->raw[1] == '-')
                    fg = PRINTTY_BLUE;

                bold = false;

                if (path_found)
                    under = true;
            } else if (ctok.kind == COMP_REDIR) {
                fg = -1;
                bold = false;

                if (path_found)
                    under = true;
            }
        } else {
            start = part->i - 1;
            end = part->i + part->n;
            fg = PRINTTY_GREEN + 8;
            bold = false;

            if (path_found)
                under = true;

            /* Is the quote terminated? */
            if (lstart()[part->i + part->n] == '\0')
                --end;
        }

        if (cell_index(&start, &end, &ibuf->cells) != -1)
            input_add_highlight(start, end, fg, -1, bold, under, ibuf);
    }

    free_index_tok(&ctok);
}

static void try_highlight_tok(lx_tok *tok, input_buf *ibuf) {
    int start = tok->i;
    int end = tok->i + (tok->n - 1);

    if (cell_index(&start, &end, &ibuf->cells) == -1)
        return;

    if (tok->kind == LX_TOK_PIPE || tok->kind == LX_TOK_AND_IF ||
        tok->kind == LX_TOK_OR_IF) {
        input_add_highlight(start, end, PRINTTY_YELLOW, -1, false, false, ibuf);
    } else if (tok->kind == LX_TOK_LPAREN || tok->kind == LX_TOK_RPAREN) {
        input_add_highlight(start, end, PRINTTY_MAGENTA, -1, false, false, ibuf);
    } else if (lx_kind_is_redir(tok->kind) || tok->kind == LX_TOK_IO_NUM) {
        input_add_highlight(start, end, PRINTTY_CYAN, -1, false, false, ibuf);
    } else if (tok->kind == LX_TOK_AMPER) {
        input_add_highlight(start, end, PRINTTY_CYAN, -1, true, false, ibuf);
    }
}

static char *history_get_shadow(void) {
    char *shadow = nullptr;

    if (!input.hist.saved && line_len() > 0) {
        for (size_t i = 0; i < linc_hda_size(input.hist.lines); ++i) {
            char *hist_line = input.hist.lines[i];

            if (strncmp(hist_line, lstart(), line_len()) == 0)
                return &hist_line[line_len()];
        }
    }

    return shadow;
}

static void input_highlight(input_buf *ibuf) {
    da_tok toks = {0};

    lx_tokenize(lstart(), &toks);

    for (size_t i = 0; i < toks.size; ++i) {
        lx_tok *tok = &toks.data[i];

        if (tok->kind == LX_TOK_WORD) {
            try_highlight_word(tok, ibuf);
        } else {
            try_highlight_tok(tok, ibuf);
        }
    }

    char *shadow = history_get_shadow();

    if (shadow) {
        size_t shadow_len = strlen(shadow);

        *linc_array_push(&ibuf->cells) = (input_cell){
            .hl = true, .fg = PRINTTY_BLACK + 8, .bg = -1,
        };

        for (size_t i = 0; i < shadow_len; ++i)
            push_cell(&shadow[i], 1, 0, &ibuf->cells);

        *linc_array_push(&ibuf->cells) = (input_cell){
            .hl = true, .attr_off = true
        };
    }

    lx_free(&toks);
}

static void input_update(void) {
    ASSERT_(NEQ, input.rows, 0);
    ASSERT_(NEQ, input.cols, 0);

    int crow, ccol;
    int rows = input.rows;
    input_buf ibuf = get_cells();

    input_highlight(&ibuf);

    input.prompt_len = ibuf.prompt_len;

    xprintty_cursor_move(pt, input.start_row, 0);
    xprintty_write(pt, unibi_clr_eos);

    int start_row = 0;
    int req_row = (input.prompt_len + input.off) / input.cols;
    bool paging = req_row >= input.rows;

    if (paging) {
        if (--rows != 0)
            start_row = (rows * ((req_row - input.rows) / rows)) + input.rows;

        ASSERT_(GEQ, req_row, start_row);

        while (input.start_row > 0) {
            xprintty_cursor_move(pt, input.rows - 1, 0);
            xprintty_write(pt, unibi_scroll_forward);
            --input.start_row;
        }

        xprintty_write(pt, unibi_cursor_home);

        xprintty_set_color(pt, PRINTTY_CYAN, -1);
        printf("%.*s", MIN(input.cols, 4), ">...");
        xprintty_write(pt, unibi_exit_attribute_mode);

        bool wrap_pending = input.cols <= 4;

        if (input.rows > 1) {
            if (wrap_pending) {
                printf(" \x1b[1G");
            } else {
                printf("\n");
            }
        } else {
            free_cells(&ibuf);
            return;
        }
    }

    input_draw(rows, start_row, ibuf.cells);
    free_cells(&ibuf);

    if (paging) {
        ncol_cell(&crow, &ccol, input.start_row,
                (input.off + input.prompt_len) % (input.cols * start_row));

        crow += 1;
    } else {
        ncol_cell(&crow, &ccol, input.start_row, input.off + input.prompt_len);
    }

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
    ASSERT_(GEQ, input.off, 0);
    ASSERT_(GEQ, line_len(), input.off);
    ASSERT(input.insert_mode || input.off < line_len() || line_len() == 0);

    if (line_len() == 0)
        return;

    if (type == DELETE_RANGE) {
        ASSERT(ctx);
        ASSERT_(GEQ, (size_t)line_len() - input.off, ctx->n);
        ASSERT_(GEQ, (int)ctx->start_i, 0);

        linc_hda_delete(input.line, input.start_i + ctx->start_i, ctx->n);
    } else if (input.insert_mode) {
        if (type == DELETE_CURSOR) {
            if (input.off == 0)
                return;

            linc_hda_delete(input.line, input.start_i + input.off - 1, 1);
            input.off -= 1;
        } else if (type == DELETE_END) {
            if (input.off == line_len())
                return;

            linc_hda_delete(input.line, input.start_i + input.off,
                    line_len() - input.off);
        } else if (type == DELETE_START) {
            if (input.off == 0)
                return;

            linc_hda_delete(input.line, input.start_i, input.off);
            input.off = 0;
        } else if (type == DELETE_ALL) {
            linc_hda_delete(input.line, input.start_i, line_len());
            input.off = 0;
        } else if (type == DELETE_WORD) {
            input_word word;

            if (cur_word(lstart(), input.off, &word) == -1)
                return;

            linc_hda_delete(input.line, word.start + input.start_i,
                    (word.end - word.start) + 1);

            input.off = word.start;
        }

    } else {
        if (type == DELETE_CURSOR) {
            linc_hda_delete(input.line, input.start_i + input.off, 1);
            if (input.off == line_len() && input.off != 0)
                input.off -= 1;
        } else if (type == DELETE_ALL) {
            linc_hda_delete(input.line, input.start_i, line_len());
            input.off = 0;
        } else if (type == DELETE_WORD) {
            input_word word;

            if (cur_word(lstart(), input.off, &word) == -1)
                return;

            linc_hda_delete(input.line, word.start + input.start_i,
                    (word.end - word.start) + 1);

            input.off = word.start;

            if (input.off > 0)
                --input.off;
        }
    }

    input_update();
}

static void input_insert(int type, const char *s) {
    ASSERT(input.off >= 0);
    ASSERT(input.off <= line_len());

    if (input.insert_mode) {
        if (type == INSERT_CHAR) {
            linc_hda_insert_buf(input.line, input.start_i + input.off, 1, s);
            input.off += 1;
        } else if (type == INSERT_STR) {
            linc_hda_insert_buf(input.line,
                    input.start_i + input.off, strlen(s), s);
            input.off += strlen(s);
        }
    } else {
        linc_hda_insert_buf(input.line,
                input.start_i + input.off, strlen(s), s);

        input.off += strlen(s);

        if (input.off == line_len() && input.off != 0)
            input.off -= 1;
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
        linc_hds_free(input.comp.glob);
        input.comp.glob = nullptr;
    }

    if (input.comp.orig) {
        free(input.comp.orig);
        input.comp.orig = nullptr;
    }

    input.comp.i = 0;
    input_update();
}

static void history_push(void) {
    linc_hda_insert(input.hist.lines, 0, 1, { xstrdup(lstart()) });
    input.hist.i = 0;

    if (input.hist.saved) {
        free(input.hist.saved);
        input.hist.saved = nullptr;
    }
}

static void history_prev(void) {
    if (input.hist.i == 0) {
        if (input.hist.saved) {
            input_delete(DELETE_ALL, nullptr);
            input_insert(INSERT_STR, input.hist.saved);

            free(input.hist.saved);
            input.hist.saved = nullptr;
        }

        return;
    }

    char *line = xstrdup(input.hist.lines[--input.hist.i]);

    input_delete(DELETE_ALL, nullptr);
    input_insert(INSERT_STR, line);

    free(line);
}

static void history_next(void) {
    if (linc_hda_size(input.hist.lines) == 0)
        return;
    if (input.hist.i == linc_hda_size(input.hist.lines) - 1 && input.hist.saved)
        return;

    if (!input.hist.saved) {
        input.hist.saved = xstrdup(lstart());
    } else {
        ++input.hist.i;
    }

    char *line = xstrdup(input.hist.lines[input.hist.i]);

    input_delete(DELETE_ALL, nullptr);
    input_insert(INSERT_STR, line);

    free(line);
}

static void insert_mode_feed_key(PrinttyInputKey key) {
    if (key == PRINTTY_BACKSPACE) {
        if (input.comp.glob)
            input_reset_comp();

        input_delete(DELETE_CURSOR, nullptr);
    } else if (key == PRINTTY_LEFT) {
        input_move(input.off - 1);
    } else if (key == PRINTTY_RIGHT) {
        input_move(input.off + 1);
    }
}

static void input_complete(void) {
    da_charp hits = {0};
    comp_tok ctok = {0};

    if (input_index_tok(&ctok, input.off) == -1) {
        input_reset_comp();
        free_index_tok(&ctok);
        return;
    }

    if (ctok.kind == COMP_EMPTY) {
        free_index_tok(&ctok);
        return;
    }

    da_segment *segs = &ctok.word->segments;
    ps_segment *last = &segs->data[segs->size - 1];

    if (input.off != last->i + last->n) {
        free_index_tok(&ctok);
        return;
    }

    if (!input.comp.glob) {
        linc_hds_append_n(input.comp.glob, ctok.text, strlen(ctok.text));
        linc_hds_append_n(input.comp.glob, "*", 1);
    }

    if (!input.comp.orig)
        input.comp.orig = make_word_str(ctok.word);

    LOG_INFO("Attempting Completion");
    LOG_INFO("    glob = %s", input.comp.glob);
    LOG_INFO("    orig = %s", input.comp.orig);

    if (ctok.kind == COMP_NAME)
        input_complete_command(input.comp.glob, &hits);

    if (ctok.kind == COMP_ARG || ctok.kind == COMP_REDIR || hits.size == 0)
        input_complete_file(input.comp.glob, input.comp.orig, &hits);

    if (hits.size == 0) {
        LOG_INFO("    Fail!");
        free_index_tok(&ctok);
        return;
    }

    LOG_INFO("    Match = %s", hits.data[input.comp.i % hits.size]);

    input_move(ctok.i);
    input_delete(DELETE_RANGE, &(input_del_ctx){ ctok.i, ctok.n });
    input_insert(INSERT_STR, hits.data[input.comp.i++ % hits.size]);
    argv_free_free(&hits);
    free_index_tok(&ctok);
}

static void insert_mode_feed_unicode(uint32_t codepoint, char utf8[5]) {
    if (input.comp.glob && codepoint == '/') {
        input_reset_comp();
        return;
    }

    if (codepoint != '\t')
        input_reset_comp();

    switch (codepoint) {
    case 5: /*C-e*/
        input_move(line_len());

        char *shadow = history_get_shadow();
        if (shadow)
            input_insert(INSERT_STR, shadow);

        break;
    case 1: /*C-a*/
        input_move(0);
        break;
    case 11: /*C-k*/
        input_delete(DELETE_END, nullptr);
        break;
    case 21: /*C-u*/
        input_delete(DELETE_START, nullptr);
        break;
    case 12: /*C-l*/
        xprintty_write(pt, unibi_clear_screen);
        new_start_pos();
        input_update();
        break;
    case '\x1b':
        input_move(input.off - 1);
        enter_mode_normal();
        break;
    case '\t':
        input_complete();
        break;
    default:
        input_insert(INSERT_CHAR, &utf8[0]);
        break;
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
            input_delete(DELETE_ALL, nullptr);
        else if (codepoint == 'w')
            input_delete(DELETE_WORD, nullptr);
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
        input_delete(DELETE_CURSOR, nullptr);
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
        printty_clipboard_copy(pt, input.line + input.start_i);
    } else if (codepoint == 'k') {
        history_next();
        input_update();
    } else if (codepoint == 'j') {
        history_prev();
        input_update();
    }
}

void input_deinit(void) {
    if (pt) {
        xprintty_write(pt, unibi_exit_attribute_mode);
        xprintty_write_name(pt, "Se");
        printty_destroy(pt);
    }

    pt = nullptr;
}

void input_init(void) {
    if (!(pt = printty_create(nullptr)))
        fatal("printty_create");
    if (printty_cbreak(pt) == PRINTTY_ERR)
        fatal("printty_cbreak");

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

    linc_hda_delete(input.line, input.start_i, line_len());
    input.off = 0;

    new_start_pos();
    input_update();
    enter_mode_insert();
}

void input_clear_history(void) {
    size_t n = linc_hda_size(input.hist.lines);

    for (size_t i = 0; i < n; ++i)
        free(input.hist.lines[i]);

    linc_hda_delete(input.hist.lines, 0, n);
}

char *input_get_line(const char *prompt) {
    if (printty_start(pt) != PRINTTY_OK)
        return nullptr;

    linc_hds_append(input.line, prompt);
    input.start_i = linc_hda_size(input.line) - 1;

    input_on_new_line();

    while (true) {
        PrinttyEvent pev;
        int rv = printty_get_event(pt, &pev);

        if (rv == PRINTTY_EOF || rv == PRINTTY_ERR)
            return nullptr;

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
                    history_push();

                input_reset_comp();
                input_move(line_len());
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

    LOG_INFO("input line: %s", lstart());

    if (printty_stop(pt) != PRINTTY_OK)
        return nullptr;

    char *out = xstrdup(lstart());

    linc_hda_free(input.line);
    input.line = nullptr;
    input.off = 0;

    return out;
}
