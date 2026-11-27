#include <assert.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#include <linc/array.h>
#include "lexer.h"
#include "linc/xsys.h"
#include "state.h" // IWYU pragma: keep
#include "utils.h"

typedef struct {
    lx_mode mode;
    lx_mode prev_mode;
    lx_tok *cur_tok;
    const char *part_start;
    const char *cur_ch;
    size_t i;
    const char *cmd;
    size_t n;
    bool fd_dup_pending;
    bool esc_pending;
    int status;
    lx_kind kind;
} lx_scan;

void log_toks(da_tok *toks) {
    if (toks->size == 0) {
        LOG_INFO("tokens = { NONE }");
        return;
    }

    LOG_INFO("tokens = {");

    for (size_t i = 0; i < toks->size; ++i) {
        lx_tok *tok = &toks->data[i];

        if (tok->kind == LX_TOK_WORD) {
            for (size_t i = 0; i < tok->parts.size; ++i) {
                lx_part *part = &tok->parts.data[i];

                if (part->quote == LX_Q_DOUBLE) {
                    LOG_INFO("    [\"%s\"], i=%d, n=%d", tok->parts.data->raw,
                            tok->i, tok->n);
                } else if (part->quote == LX_Q_SINGLE) {
                    LOG_INFO("    ['%s'], i=%d, n=%d", tok->parts.data->raw,
                            tok->i, tok->n);
                } else {
                    LOG_INFO("    [%s], i=%d, n=%d", tok->parts.data->raw,
                            tok->i, tok->n);
                }
            }
        } else {
            LOG_INFO("    %s, i=%d, n=%d", tok_str(tok->kind), tok->i, tok->n);
        }
    }

    LOG_INFO("}");
}

static lx_kind get_kind(const char *cur_char, bool last) {
    if (!last) {
        if (strncmp(">>", cur_char, 2) == 0)
            return LX_TOK_APPEND;
        if (strncmp("&&", cur_char, 2) == 0)
            return LX_TOK_AND_IF;
        if (strncmp("||", cur_char, 2) == 0)
            return LX_TOK_OR_IF;
        if (strncmp(">&", cur_char, 2) == 0)
            return LX_TOK_FD_OUT;
        if (strncmp("<&", cur_char, 2) == 0)
            return LX_TOK_FD_IN;
        if (strncmp("$(", cur_char, 2) == 0)
            return LX_CMD_SUB_START;
        if (strncmp("<(", cur_char, 2) == 0)
            return LX_PROC_SUB_START;
    }

    if (strncmp("|", cur_char, 1) == 0)
        return LX_TOK_PIPE;
    if (strncmp("&", cur_char, 1) == 0)
        return LX_TOK_AMPER;
    if (strncmp("<", cur_char, 1) == 0)
        return LX_TOK_RDR_IN;
    if (strncmp(">", cur_char, 1) == 0)
        return LX_TOK_RDR_OUT;
    if (strncmp("(", cur_char, 1) == 0)
        return LX_TOK_LPAREN;
    if (strncmp(")", cur_char, 1) == 0)
        return LX_TOK_RPAREN;
    if (strncmp(";", cur_char, 1) == 0)
        return LX_TOK_SEMI;
    if (strncmp("{", cur_char, 1) == 0)
        return LX_TOK_LBRACE;
    if (strncmp("}", cur_char, 1) == 0)
        return LX_TOK_RBRACE;

    return LX_TOK_UNKNOWN;
}

static size_t tok_len(lx_kind kind) {
    switch (kind) {
    case LX_TOK_PIPE:
    case LX_TOK_AMPER:
    case LX_TOK_RDR_IN:
    case LX_TOK_RDR_OUT:
    case LX_TOK_LPAREN:
    case LX_TOK_RPAREN:
    case LX_TOK_SEMI:
    case LX_TOK_LBRACE:
    case LX_TOK_RBRACE:
        return 1;
    case LX_TOK_FD_OUT:
    case LX_TOK_FD_IN:
    case LX_TOK_APPEND:
    case LX_TOK_AND_IF:
    case LX_TOK_OR_IF:
    case LX_CMD_SUB_START:
    case LX_PROC_SUB_START:
        return 2;
    default:
        return 0;
    }
}

static bool kind_is_delim(lx_kind kind) {
    switch (kind) {
    case LX_TOK_PIPE:
    case LX_TOK_AMPER:
    case LX_TOK_RDR_IN:
    case LX_TOK_RDR_OUT:
    case LX_TOK_FD_OUT:
    case LX_TOK_FD_IN:
    case LX_TOK_APPEND:
    case LX_TOK_AND_IF:
    case LX_TOK_OR_IF:
    case LX_TOK_LPAREN:
    case LX_TOK_RPAREN:
    case LX_TOK_SEMI:
    case LX_TOK_LBRACE:
    case LX_TOK_RBRACE:
        return true;
    default:
        return false;
    }
}

static void ensure_tok(da_tok *toks, lx_kind kind, lx_scan *scan) {
    if (!scan->cur_tok) {
        lx_tok *tok = linc_array_push(toks);

        *tok = (lx_tok){
            .kind = kind,
            .i = scan->cur_ch - scan->cmd
        };

        scan->cur_tok = tok;
    }
}

static void ensure_part(lx_scan *scan, lx_quote quote) {
    if (!scan->part_start) {
        lx_part *part = linc_array_push(&scan->cur_tok->parts);
        *part = (lx_part){ .quote = quote };
        scan->part_start = scan->cur_ch;
    }
}

static bool tok_is_number(lx_tok *tok) {
    bool is_number = (
        tok->kind == LX_TOK_WORD &&
        tok->parts.size == 1 &&
        tok->parts.data[0].quote == LX_Q_NONE
    );

    return is_number;
}

static int lex_io_num(lx_kind delim, const lx_scan *scan) {
    if (!(lx_is_redir(delim) || scan->fd_dup_pending))
        return LX_FAIL;

    const lx_tok *tok = scan->cur_tok;
    int io_num;

    for (const char *c = tok->parts.data[0].raw; *c != '\0'; ++c)
        if (*c < 48 || *c > 57)
            return LX_FAIL;

    if (get_int(tok->parts.data[0].raw, &io_num, GET_INT_NON_NEG) == -1) {
        LOG_INFO("io-num conversion failed: %s", tok->parts.data[0].raw);
        return LX_FAIL;
    }

    return io_num;
}

static void classify_word(lx_tok *word) {
    ASSERT(word && word->kind == LX_TOK_WORD);

    lx_part *part = &word->parts.data[0];

    if (word->parts.size > 1 || part->quote != LX_Q_NONE)
        return;

    if (STREQ("if", part->raw)) {
        word->spec = LX_SP_IF;
    } else if (STREQ("else", part->raw)) {
        word->spec = LX_SP_ELSE;
    } else if (STREQ("while", part->raw)) {
        word->spec = LX_SP_WHILE;
    } else if (STREQ("for", part->raw)) {
        word->spec = LX_SP_FOR;
    } else {
        word->spec = LX_SP_SIMPLE;
    }
}

static void try_end_tok(lx_scan *scan, lx_kind delim) {
    if (!scan->cur_tok)
        return;

    if (scan->cur_tok->kind == LX_TOK_WORD) {
        classify_word(scan->cur_tok);
        scan->cur_tok->n = (scan->cur_ch - scan->cmd) - scan->cur_tok->i;
    } else {
        scan->cur_tok->i = scan->cur_ch - scan->cmd;
        scan->cur_tok->n = tok_len(scan->cur_tok->kind);
    }

    if (!tok_is_number(scan->cur_tok)) {
        scan->fd_dup_pending = (
            scan->cur_tok->kind == LX_TOK_FD_IN ||
            scan->cur_tok->kind == LX_TOK_FD_OUT
        );
    } else {
        char *endptr;

        strtol(scan->cur_tok->parts.data[0].raw, &endptr, 10);
        scan->cur_tok->is_num = (*endptr == '\0');

        if (scan->cur_tok->is_num) {
            int io_num = lex_io_num(delim, scan);

            if (io_num != LX_FAIL) {
                lx_free_tok(scan->cur_tok);
                scan->cur_tok->kind = LX_TOK_IO_NUM;
                scan->cur_tok->io_num = io_num;
            }
        }

        scan->fd_dup_pending = false;
    }

    scan->cur_tok = nullptr;
}

static void try_end_part(lx_scan *scan) {
    ASSERT(scan);

    if (!scan->cur_tok || !scan->part_start)
        return;

    da_part *parts = &scan->cur_tok->parts;
    lx_part *part = &parts->data[parts->size - 1];

    size_t len = 0;
    for (const char *c = scan->part_start; c != scan->cur_ch; ++c)
        ++len;

    part->raw = xmalloc(len + 1);

    memcpy(part->raw, scan->part_start, len);
    part->raw[len] = '\0';

    part->i = scan->part_start - scan->cmd;
    part->n = strlen(part->raw);

    scan->part_start = nullptr;
}

static void scan_advance(lx_scan *scan, size_t n) {
    ASSERT_(LEQ, n, scan->n - scan->i);

    scan->i += n;
    scan->cur_ch += n;
    scan->kind = get_kind(scan->cur_ch, (scan->i == scan->n - 1));
}

static void handle_single_quote(lx_scan *scan) {
    if (*scan->cur_ch == '\'') {
        try_end_part(scan);
        scan->mode = LX_M_NORMAL;
    }
}

static void handle_double_quote(lx_scan *scan, lx_kind kind) {
    if (scan->esc_pending) {
        scan->esc_pending = false;
    } else if (*scan->cur_ch == '\\') {
        scan->esc_pending = true;
    } else if (*scan->cur_ch == '"') {
        try_end_part(scan);
        scan->mode = LX_M_NORMAL;
    } else if (kind == LX_CMD_SUB_START || kind == LX_PROC_SUB_START) {
        int rp = lx_get_rparen(
            &scan->cur_ch[1],
            scan->n - (&scan->cur_ch[1] - scan->cmd)
        );

        if (rp == -1) {
            scan->cur_ch = &scan->cmd[scan->n - 1];
            scan->i = scan->n - 1;
            scan->status = LX_ERRMISSINGPARAEN;
        } else {
            scan_advance(scan, rp + 1);
        }
    }
}

int lx_get_rparen(const char *str, size_t n) {
    ASSERT(str);

    if (n == 0 || str[0] != '(')
        return -1;

    da_tok toks = {0};
    int rv = lx_tokenize(str, n, &toks);

    if (toks.data[0].kind != LX_TOK_LPAREN || rv == LX_ERRMISSINGPARAEN) {
        lx_free(&toks);
        return -1;
    }

    lx_tok *tok;
    int paren_count = 0;

    for (size_t i = 0; i < toks.size; ++i) {
        tok = &toks.data[i];

        if (tok->kind == LX_TOK_LPAREN)
            ++paren_count;

        if (tok->kind == LX_TOK_RPAREN)
            --paren_count;

        if (paren_count == 0)
            break;
    }

    if (paren_count != 0) {
        lx_free(&toks);
        return -1;
    }

    size_t out = tok->i;
    lx_free(&toks);

    return out;
}

int lx_tokenize(const char *cmd, size_t n, da_tok *toks) {
    ASSERT(cmd && toks);

    *toks = (da_tok){0};

    if (n == 0)
        return LX_OK;

    lx_scan scan = {
        .mode = LX_M_NORMAL,
        .prev_mode = LX_M_NORMAL,
        .cur_ch = cmd,
        .cmd = cmd,
        .n = n,
        .status = LX_OK,
        .kind = get_kind(cmd, n == 1)
    };

    for (; scan.i < scan.n; scan_advance(&scan, 1)) {
        if (scan.mode == LX_M_NORMAL) {
            if (scan.esc_pending) {
                scan.esc_pending = false;
                continue;
            }

            if (*scan.cur_ch == '"') {
                scan.mode = LX_M_DOUBLEQ;

                try_end_part(&scan);
                ensure_tok(toks, LX_TOK_WORD, &scan);
                scan_advance(&scan, 1);
                ensure_part(&scan, LX_Q_DOUBLE);

                if (scan.i >= scan.n)
                    break;

                handle_double_quote(&scan, scan.kind);
            } else if (*scan.cur_ch == '\'') {
                scan.mode = LX_M_SINGLEQ;

                try_end_part(&scan);
                ensure_tok(toks, LX_TOK_WORD, &scan);
                scan_advance(&scan, 1);
                ensure_part(&scan, LX_Q_SINGLE);

                if (scan.i >= scan.n)
                    break;

                handle_single_quote(&scan);
            } else if (*scan.cur_ch == ' ' || *scan.cur_ch == '\n') {
                try_end_part(&scan);
                try_end_tok(&scan, LX_WHITE_SPACE);
            } else if(*scan.cur_ch == '\\') {
                ensure_tok(toks, LX_TOK_WORD, &scan);
                ensure_part(&scan, LX_Q_NONE);
                scan.esc_pending = true;
            } else if (scan.kind == LX_CMD_SUB_START ||
                       scan.kind == LX_PROC_SUB_START) {
                ensure_tok(toks, LX_TOK_WORD, &scan);
                ensure_part(&scan, LX_Q_NONE);

                int rp = lx_get_rparen(
                    &scan.cur_ch[1],
                    n - (&scan.cur_ch[1] - cmd)
                );

                if (rp == -1) {
                    scan_advance(&scan, scan.n - 1 - scan.i);
                    scan.status = LX_ERRMISSINGPARAEN;
                } else {
                    scan_advance(&scan, rp + 1);
                }
            } else if (kind_is_delim(scan.kind)) {
                    try_end_part(&scan);
                    try_end_tok(&scan, scan.kind);

                    ensure_tok(toks, scan.kind, &scan);
                    try_end_tok(&scan, scan.kind);

                    scan_advance(&scan, tok_len(scan.kind) - 1);
            } else if (scan.kind == LX_TOK_UNKNOWN) {
                ensure_tok(toks, LX_TOK_WORD, &scan);
                ensure_part(&scan, LX_Q_NONE);
            }
        } else if (scan.mode == LX_M_DOUBLEQ) {
            handle_double_quote(&scan, scan.kind);
        } else {
            handle_single_quote(&scan);
        }
    }

    if (scan.mode == LX_M_DOUBLEQ || scan.mode == LX_M_SINGLEQ)
        scan.status = LX_ERRNOENDQUOTE;

    if (scan.esc_pending)
        scan.status = LX_ERREMPTYESC;

    try_end_part(&scan);
    try_end_tok(&scan, LX_EOF);

    if (false)
        log_toks(toks);

    return scan.status;
}

char *tok_word_str(lx_tok *tok) {
    static char out[BUFSIZ];

    for (size_t part_i = 0; part_i < tok->parts.size; ++part_i) {
        lx_part *part = &tok->parts.data[part_i];

        if (part->quote == LX_Q_NONE) {
            snprintf(out, BUFSIZ - 1, "%s", part->raw);
        } else if (part->quote == LX_Q_DOUBLE) {
            snprintf(out, BUFSIZ - 1, "\"%s\"", part->raw);
        } else if (part->quote == LX_Q_SINGLE) {
            snprintf(out, BUFSIZ - 1, "'%s'", part->raw);
        }
    }

    return out;
}

char *tok_str(lx_kind kind) {
    switch (kind) {
    case LX_TOK_PIPE:    return "|";
    case LX_TOK_AMPER:   return "&";
    case LX_TOK_RDR_IN:  return "<";
    case LX_TOK_RDR_OUT: return ">";
    case LX_TOK_FD_OUT:  return ">&";
    case LX_TOK_FD_IN:   return "<&";
    case LX_TOK_APPEND:  return ">>";
    case LX_TOK_AND_IF:  return "&&";
    case LX_TOK_OR_IF:   return "||";
    case LX_TOK_LPAREN:  return "(";
    case LX_TOK_RPAREN:  return ")";
    case LX_TOK_SEMI:    return ";";
    case LX_TOK_LBRACE:  return "{";
    case LX_TOK_RBRACE:  return "}";
    case LX_TOK_WORD:    return "[word]";
    case LX_TOK_UNKNOWN: return "Unknown";
    case LX_TOK_IO_NUM:  return "[io]";
    case LX_EOF:         return "EOF";
    default: break;
    }

    ASSERT(false && "tok_str: bad tok type");
    fatal(nullptr);
}

bool lx_is_redir(lx_kind kind) {
    bool is_redir = (
        kind == LX_TOK_RDR_OUT ||
        kind == LX_TOK_RDR_IN ||
        kind == LX_TOK_APPEND ||
        kind == LX_TOK_FD_IN ||
        kind == LX_TOK_FD_OUT
    );

    return is_redir;
}

void lx_shift_toks(int shift, size_t shift_n, lx_tok *toks) {
    if (shift == 0)
        return;

    for (size_t i = 0; i < shift_n; ++i) {
        toks[i].i += shift;

        if (toks[i].kind == LX_TOK_WORD)
            for (size_t y = 0; y < toks[i].parts.size; ++y)
                toks[i].parts.data[y].i += shift;
    }
}

void lx_tkcpy(lx_tok *dest, lx_tok *src, linc_allo *allo) {
    *dest = *src;

    if (src->kind == LX_TOK_WORD) {
        dest->parts = (da_part){0};

        if (allo)
            dest->parts.allo = *allo;

        for (size_t i = 0; i < src->parts.size; ++i) {
            lx_part *src_part = &src->parts.data[i];
            lx_part *dest_part = linc_array_push(&dest->parts);

            *dest_part = *src_part;

            if (allo) {
                dest_part->raw = strdup_alloc(src_part->raw, allo);
            } else {
                dest_part->raw = xstrdup(src_part->raw);
            }
        }
    }
}

void lx_free_tok(lx_tok *tok) {
    if (tok->kind == LX_TOK_WORD) {
        for (size_t i = 0; i < tok->parts.size; ++i)
            free(tok->parts.data[i].raw);

        linc_array_free(&tok->parts);
    }
}

void lx_free(da_tok *toks) {
    if (!toks)
        return;

    for (size_t i = 0; i < toks->size; ++i)
        lx_free_tok(&toks->data[i]);

    linc_array_free(toks);
    *toks = (da_tok){0};
}

char *lx_errstr(lx_status stat) {
    switch (stat) {
    case LX_ERRNOENDQUOTE:    return "lex error finding matching '\"'";
    case LX_ERREMPTYESC:      return "lex error on '\\'";
    case LX_ERRMISSINGPARAEN: return "lex error finding matching ')'";
    case LX_OK:               return "success";
    default:                  return "???";
    }
}
