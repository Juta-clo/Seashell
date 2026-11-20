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
#include "arena.h"

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

typedef struct {
    size_t i;
    da_charp hits;
    ps_info info;
    bool saved;
} input_comp;

typedef struct {
    char **lines;
    char *saved;
    size_t i;
} input_hist;

typedef struct {
    size_t start_i;
    size_t n;
} del_ctx;

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

static Printty *pt = nullptr;
static bool completion_shadow = true;

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

static void complete_command(const char *cmd, da_charp *hits) {
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

static void complete_file(const char *glob, const char *cli, da_charp *hits) {
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
                if (!STREQ(result.cstr, "/"))
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

void input_draw_cell(input_cell *cell, input_cell *hl) {
    if (cell->hl) {
        if (cell->attr_off) {
            xprintty_write(pt, unibi_exit_attribute_mode);
        } else {
            xprintty_set_color(pt, cell->fg, cell->bg);

            if (cell->bold)
                xprintty_write(pt, unibi_enter_bold_mode);
            if (cell->under)
                xprintty_write(pt, unibi_enter_underline_mode);
        }
    } else if (cell->n > 1) {
        printf("%s", cell->data);
    } else if (*cell->data >= 32 && *cell->data < 127) {
        printf("%c", *cell->data);
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

void input_free_cells(input_buf *ibuf) {
    for (size_t i = 0; i < ibuf->cells.size; ++i)
        free(ibuf->cells.data[i].data);

    linc_array_free(&ibuf->cells);
}

static void push_cell(const char *data, size_t n, size_t i, da_cell *cells) {
    input_cell *cell = linc_array_push(cells);

    *cell = (input_cell){
        .n = n,
        .i = i,
        .data = xstrndup(data, n)
    };
}

input_buf input_get_cells(const char *line, int start_i) {
    input_buf ibuf = {0};
    bool pending_csi = false;
    bool pending_esc = false;
    int i = 0;
    int n = 0;

    for (const char *b = line; *b != '\0'; ++b, ++i) {
        if (i >= start_i) {
            /* Push unfinished prompt csi characters literally */
            for (; n > 0; --n)
                push_cell(&line[i - n], 1, i - n, &ibuf.cells);

            push_cell(&line[i], 1, i, &ibuf.cells);
        } else if (pending_csi) {
            ++n;

            if (*b >= '@' && *b <= '~') {
                pending_csi = false;
                int csi_i = i - (n - 1);
                push_cell(&line[csi_i], n, csi_i, &ibuf.cells);
                n = 0;
            }
        } else if (pending_esc) {
            pending_esc = false;

            if (*b == '[') {
                pending_csi = true;
                n = 2;
            } else {
                push_cell(&line[i - 1], 1, i - 1, &ibuf.cells);
                push_cell(&line[i], 1, i, &ibuf.cells);
            }
        } else if (*b == '\x1b') {
            pending_esc = true;
        } else {
            ++ibuf.prompt_len;
            push_cell(&line[i], 1, i, &ibuf.cells);
        }
    }

    log_cells(ibuf.cells);
    return ibuf;
}

static void draw(int rows, int start_row, da_cell cells) {
    int scr_i = 0;
    size_t cell_content_start_i = start_row * input.cols;
    int crow, ccol;
    input_cell *last_hl = nullptr;

    for (size_t i = 0; i < cells.size; ++i) {
        input_cell cell = cells.data[i];

        if (cell.hl || cell.n > 1) {
            if (cell.hl)
                last_hl = &cells.data[i];

            input_draw_cell(&cell, last_hl);
            ++cell_content_start_i;
        } else if (i >= cell_content_start_i && scr_i < input.cols * rows) {
            ncol_cell(&crow, &ccol, input.start_row, scr_i++);
            input_draw_cell(&cell, last_hl);

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

static int cell_index(int *start, int *end, da_cell *cells, int start_i) {
    ASSERT(start);
    ASSERT(end);
    ASSERT(cells);

    int tok_start = *start + start_i;
    int tok_end = *end + start_i;

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

static void add_highlight(int start, int end, int fg, int bg, bool bold,
        bool under, input_buf *ibuf, int start_i) {
    ASSERT(ibuf);

    if (cell_index(&start, &end, &ibuf->cells, start_i) == -1)
        return;

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

static bool is_path(const char *text) {
    if (*text == '\0')
        return false;

    da_charp hits = {0};
    linc_string glob = LINC_STRING_INIT;
    bool path_found = false;

    linc_string_appendf(&glob, "%s*", text);
    glob_pattern(glob.cstr, glob.len, &hits);

    for (size_t i = 0; i < hits.size; ++i) {
        if (is_dir(hits.data[i]) || is_file(hits.data[i])) {
            path_found = true;
            break;
        }
    }

    argv_free_free(&hits);
    linc_string_free(&glob);

    return path_found;
}

static bool is_name(const char *text) {
    bool name_found = false;

    if (strstr(text, "/")) {
        if (is_bin(text))
            name_found = true;
    } else if (get_builtin(text)) {
        name_found = true;
    } else {
        da_charp candidates = build_path_candidates(text);

        for (size_t i = 0; i < candidates.size; ++i) {
            if (is_bin(candidates.data[i])) {
                name_found = true;
                break;
            }
        }

        argv_free_free(&candidates);
    }

    if (mp_str_lookup(&env.aliases, text, NULL) != -1)
        name_found = true;

    return name_found;
}

void input_highlight_tok(int start, int end, lx_kind kind, input_buf *ibuf,
        int start_i) {
    int fg = -1;
    int bg = -1;
    int bold = false;
    bool under = false;

    if (kind == LX_TOK_PIPE || kind == LX_TOK_AND_IF ||
        kind == LX_TOK_OR_IF) {
        fg = PRINTTY_YELLOW;
    } else if (kind == LX_TOK_LPAREN || kind == LX_TOK_RPAREN) {
        fg = PRINTTY_MAGENTA;
    } else if (lx_kind_is_redir(kind) || kind == LX_TOK_IO_NUM) {
        fg = PRINTTY_CYAN;
    } else if (kind == LX_TOK_AMPER) {
        fg = PRINTTY_CYAN;
        bold = true;
    }

    add_highlight(
        start, end,
        fg, bg, bold, under,
        ibuf, start_i
    );
}

static void highlight_err(int start, int end, input_buf *ibuf, int start_i) {
    int fg = PRINTTY_RED;
    int bg = -1;
    int bold = true;
    int under = false;

    add_highlight(start, end, fg, bg, bold, under, ibuf, start_i);
}

static void highlight_command(ps_word *word, input_buf *ibuf, int start_i) {
    ASSERT(word && ibuf);
    ASSERT(word->arg);

    bool name_found = is_name(word->arg);
    bool path_found = is_path(word->arg);

    int start = word->i;
    int end = word->i + (word->n - 1);
    int fg = -1;
    bool bold = false;
    bool under = false;

    if (name_found) {
        fg = PRINTTY_GREEN;
    } else if (path_found) {
        under = true;
    } else {
        highlight_err(start, end, ibuf, start_i);
        return;
    }

    add_highlight(start, end, fg, -1, bold, under, ibuf, start_i);
}

static void highlight_arg(ps_word *word, bool redir, input_buf *ibuf,
        int start_i, const char *str) {
    bool path_found = is_path(word->arg);

    for (size_t i = 0; i < word->segments.size; ++i) {
        ps_segment *seg = &word->segments.data[i];

        int start;
        int end;
        int fg = -1;
        bool under = (path_found);
        bool bold = false;

        if (seg->quote == PS_Q_NONE) {
            start = seg->i;
            end = seg->i + (seg->n - 1);

            if (!redir) {
                if (seg->raw[0] == '-' && seg->raw[1] != '-')
                    fg = PRINTTY_CYAN;

                if (seg->raw[0] == '-' && seg->raw[1] == '-')
                    fg = PRINTTY_BLUE;
            }
        } else {
            start = seg->i - 1;
            end = seg->i + seg->n;
            fg = PRINTTY_GREEN + 8;

            /* Is the quote terminated? */
            if (str[seg->i + seg->n] == '\0')
                --end;
        }

        add_highlight(start, end, fg, -1, bold, under, ibuf, start_i);
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

static bool is_err_tok(lx_tok *tok, ps_scan *scan) {
    for (size_t i = 0; i < scan->err_toks.size; ++i)
        if (tok->i == scan->err_toks.data[i].i)
            return true;

    return false;
}

void input_highlight(input_buf *ibuf, char *line, size_t n, int start_i) {
    bool hl_complete = false;
    size_t err_tok_i;
    da_tok toks = {0};

    lx_tokenize(line, n, &toks);

    while (!hl_complete) {
        ps_ast ast = {0};
        ps_scan scan = { .toks = &toks };
        ps_parse(&scan, &ast);

        for (size_t i = 0; i < toks.size; ++i) {
            lx_tok *tok = &toks.data[i];

            if (is_err_tok(tok, &scan)) {
                highlight_err(tok->i, tok->i + (tok->n - 1), ibuf, start_i);
                err_tok_i = i + 1;
            } else if (tok->kind != LX_TOK_WORD) {
                input_highlight_tok(tok->i, tok->i + (tok->n - 1), tok->kind,
                        ibuf, start_i);
            } else {
                ps_info pinfo = {0};
                ps_get_info(tok->i, &ast, &pinfo);

                if (pinfo.kinds & PS_INFO_NAME) {
                    if (expand_word(pinfo.word, NULL, EXPAND_COMP) != -1)
                        highlight_command(pinfo.word, ibuf, start_i);
                } else if (pinfo.kinds & PS_INFO_REDIR) {
                    if (expand_word(pinfo.word, NULL, EXPAND_COMP) != -1)
                        highlight_arg(pinfo.word, true, ibuf, start_i, line);
                } else if (pinfo.kinds & PS_INFO_ARG) {
                    if (expand_word(pinfo.word, NULL, EXPAND_COMP) != -1)
                        highlight_arg(pinfo.word, false, ibuf, start_i, line);
                }
            }
        }

        if (scan.err_toks.size != 0) {
            da_tok next_toks = {0};

            for (size_t i = err_tok_i; i < toks.size; ++i)
                lx_tok_copy(linc_array_push(&next_toks), &toks.data[i]);

            lx_free(&toks);
            toks = next_toks;
        } else {
            hl_complete = true;
        }

        ps_free(&ast);
    }

    lx_free(&toks);
}

static void input_shadow(char *shadow, size_t n, input_buf *ibuf) {
    if (shadow) {
        *linc_array_push(&ibuf->cells) = (input_cell){
            .hl = true, .fg = PRINTTY_BLACK + 8, .bg = -1,
        };

        for (size_t i = 0; i < n; ++i)
            push_cell(&shadow[i], 1, 0, &ibuf->cells);

        *linc_array_push(&ibuf->cells) = (input_cell){
            .hl = true, .attr_off = true
        };
    }
}

static bool on_last_word(void) {
    da_tok toks = {0};
    lx_tokenize(lstart(), line_len(), &toks);

    bool out;
    int ind = input.off;
    lx_tok *last = nullptr;
    size_t last_i;

    for (size_t i = 0; i < toks.size; ++i) {
        lx_tok *tok = &toks.data[i];
        if (tok->kind == LX_TOK_WORD) {
            last = tok;
            last_i = i;
        }
    }

    if (!last || last_i < toks.size - 1) {
        out =  false;
        lx_free(&toks);
        return out;
    }

    out = (ind >= last->i && ind <= last->i + last->n);
    lx_free(&toks);
    return out;
}

static int get_completion_info(ps_info *info) {
    int status = -1;
    da_tok toks = {0};

    lx_tokenize(lstart(), line_len(), &toks);

    ps_scan scan = { .toks = &toks};
    ps_ast ast = {0};

    ps_parse(&scan, &ast);

    for (size_t tok_i = 0; tok_i < toks.size; ++tok_i) {
        lx_tok *tok = &toks.data[tok_i];

        if (input.off < tok->i)
            continue;

        if (is_err_tok(tok, &scan)) {
            da_tok next_toks = {0};

            for (size_t i = tok_i + 1; i < toks.size; ++i)
                lx_tok_copy(linc_array_push(&next_toks), &toks.data[i]);

            lx_free(&toks);
            ps_free(&ast);

            toks = next_toks;
            tok_i = SIZE_MAX;

            scan = (ps_scan){ .toks = &toks };
            ps_parse(&scan, &ast);

            continue;
        }

        if (input.off > tok->i + tok->n)
            continue;

        if (tok->kind == LX_TOK_WORD) {
            ps_get_info(tok->i, &ast, info);

            if (info->word)
                status = 0;
        }

        break;
    }

    lx_free(&toks);
    return status;
}

static void input_update(void);

static void input_reset_comp(void) {
    if (!input.comp.saved)
        return;

    argv_free_free(&input.comp.hits);

    if (input.comp.info.allo.ctx)
        arena_destroy(input.comp.info.allo.ctx);

    input.comp = (input_comp){0};
}

static void input_complete_hits(ps_info *info, da_charp *hits, bool log) {
    *info = (ps_info){0};
    *hits = (da_charp){0};

    if (get_completion_info(info) == -1)
        return;

    if (expand_word(info->word, nullptr, EXPAND_COMP) == -1)
        return;

    char *orig = make_word_str(info->word);
    const char *arg = info->word->arg;

    linc_string glob = LINC_STRING_INIT;
    linc_string_appendf(&glob, "%s*", arg);

    if (log) {
        LOG_INFO("Attempting Completion");
        LOG_INFO("    glob = %s", glob.cstr);
        LOG_INFO("    orig = %s", orig);
    }

    if (info->kinds & PS_INFO_NAME)
        complete_command(glob.cstr, hits);

    if (info->kinds & PS_INFO_ARG || info->kinds & PS_INFO_REDIR)
        complete_file(glob.cstr, orig, hits);

    if (hits->size == 0)
        complete_file(glob.cstr, orig, hits);

    free(orig);
    linc_string_free(&glob);

    if (log) {
        if (hits->size == 0) {
            LOG_INFO("    No Matches");
        } else {
            LOG_INFO("    %zu Matches", hits->size);
        }
    }
}

static bool input_completion_shadow(input_buf *ibuf) {
    ASSERT(ibuf);

    if (input.comp.saved)
        return false;

    if (input.off != line_len() || !on_last_word())
        return false;

    ps_info *info = &input.comp.info;
    da_charp *hits = &input.comp.hits;

    input_complete_hits(info, hits, false);
    input.comp.saved = true;

    if (hits->size == 0)
        goto fail;

    char *match = hits->data[0];
    ps_word *word = info->word;
    int shadow_len = strlen(match) - word->n;

    input_shadow(&match[word->n], shadow_len, ibuf);
    return true;

fail:
    input_reset_comp();
    return false;
}

static void input_update(void) {
    ASSERT_(NEQ, input.rows, 0);
    ASSERT_(NEQ, input.cols, 0);

    int crow, ccol;
    int rows = input.rows;
    input_buf ibuf = input_get_cells(input.line, input.start_i);
    input_highlight(&ibuf, lstart(), line_len(), input.start_i);

    char *shadow = history_get_shadow();

    if (shadow) {
        input_shadow(shadow, strlen(shadow), &ibuf);
    } else if (completion_shadow) {
        input_completion_shadow(&ibuf);
    }

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
            input_free_cells(&ibuf);
            return;
        }
    }

    draw(rows, start_row, ibuf.cells);
    input_free_cells(&ibuf);

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

static void input_delete(int type, del_ctx *ctx) {
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
        input_delete(DELETE_CURSOR, nullptr);
    } else if (key == PRINTTY_LEFT) {
        input_move(input.off - 1);
    } else if (key == PRINTTY_RIGHT) {
        input_move(input.off + 1);
    }
}

/* Caller must free the arena stored within the info out param */

static void input_complete(void) {
    ps_info *info = &input.comp.info;
    da_charp *hits = &input.comp.hits;

    if (!input.comp.saved) {
        input.comp.saved = true;
        input_complete_hits(info, hits, true);
        if (hits->size == 0)
            goto reset;
    }

    LOG_INFO("    Match = %s", hits->data[input.comp.i % hits->size]);

    input_move(info->word->i);
    input_delete(DELETE_RANGE, &(del_ctx){ info->word->i, info->word->n });
    input_insert(INSERT_STR, hits->data[input.comp.i % hits->size]);

    info->word->n = strlen(hits->data[input.comp.i % hits->size]);
    ++input.comp.i;

    return;

reset:
    input_reset_comp();
}

static void insert_mode_feed_unicode(uint32_t codepoint, char utf8[5]) {
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

void input_init(int input_fd) {
    if (!(pt = printty_create(&input_fd)))
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

void input_opt_completion_shadow(bool set) {
    completion_shadow = !!set;
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
            if (input.comp.saved) {
                if (pev.cp == '/' || pev.cp != '\t')
                    input_reset_comp();
                if (pev.cp == '/')
                    continue;
            }


            if (input.insert_mode) {
                insert_mode_feed_unicode(pev.cp, pev.utf8);
            } else {
                normal_mode_feed_unicode(pev.cp, pev.utf8);
            }
        } else if (pev.type == PRINTTY_KEY) {
            input_reset_comp();

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
