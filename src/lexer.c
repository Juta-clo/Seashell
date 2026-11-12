#include <assert.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#include <linc/array.h>
#include "lexer.h"
#include "linc/xsys.h"
#include "utils.h"

typedef struct {
    lx_mode mode;
    lx_mode prev_mode;
    lx_tok *t;
    const char *part_start;
    const char *c;
    const char *cmd;
    bool fd_dup_pending;
    bool esc_pending;
    int status;
} lx_scan;

static void log_toks(da_tok *toks) {
    return;

    LOG_INFO("lexed toks {");

    for (size_t i = 0; i < toks->size; ++i) {
        lx_tok *tok = &toks->data[i];

        if (tok->kind == LX_TOK_WORD) {
            LOG_INFO("    [%s]", tok->parts.data->raw);
        } else {
            LOG_INFO("    %s", tok_str(tok->kind));
        }
    }

    LOG_INFO("}");
}

static lx_kind get_kind(const char *cur_char) {
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
        return 1;
    case LX_TOK_FD_OUT:
    case LX_TOK_FD_IN:
    case LX_TOK_APPEND:
    case LX_TOK_AND_IF:
    case LX_TOK_OR_IF:
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
        return true;
    default:
        return false;
    }
}

static void ensure_tok(da_tok *toks, lx_kind kind, lx_scan *scan) {
    if (!scan->t) {
        lx_tok *tok = linc_array_push(toks);

        *tok = (lx_tok){
            .kind = kind,
            .i = scan->c - scan->cmd
        };

        scan->t = tok;
    }
}

static void ensure_part(lx_scan *scan, lx_quote quote) {
    if (!scan->part_start) {
        lx_part *part = linc_array_push(&scan->t->parts);
        *part = (lx_part){ .quote = quote };
        scan->part_start = scan->c;
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
    if (!(lx_kind_is_redir(delim) || scan->fd_dup_pending))
        return LX_FAIL;

    const lx_tok *tok = scan->t;
    int io_num;

    for (const char *c = tok->parts.data[0].raw; *c != '\0'; ++c)
        if (*c < 48 || *c > 57)
            return LX_FAIL;

    if (get_int(tok->parts.data[0].raw, &io_num) == -1 || io_num < 0) {
        LOG_INFO("io-num conversion failed: %s", tok->parts.data[0].raw);
        return LX_FAIL;
    }

    return io_num;
}

static void try_end_tok(lx_scan *scan, lx_kind delim) {
    if (!scan->t)
        return;

    if (scan->t->kind == LX_TOK_WORD) {
        scan->t->n = (scan->c - scan->cmd) - scan->t->i;
    } else {
        scan->t->i = scan->c - scan->cmd;
        scan->t->n = tok_len(scan->t->kind);
    }

    if (!tok_is_number(scan->t)) {
        scan->fd_dup_pending = (
            scan->t->kind == LX_TOK_FD_IN ||
            scan->t->kind == LX_TOK_FD_OUT
        );
    } else {
        char *endptr;

        strtol(scan->t->parts.data[0].raw, &endptr, 10);
        scan->t->is_num = (*endptr == '\0');

        if (scan->t->is_num) {
            int io_num = lex_io_num(delim, scan);

            if (io_num != LX_FAIL) {
                lx_free_tok(scan->t);
                scan->t->kind = LX_TOK_IO_NUM;
                scan->t->io_num = io_num;
            }
        }

        scan->fd_dup_pending = false;
    }

    scan->t = nullptr;
}

static void try_end_part(lx_scan *scan) {
    ASSERT(scan);

    if (!scan->t || !scan->part_start)
        return;

    da_part *parts = &scan->t->parts;
    lx_part *part = &parts->data[parts->size - 1];

    size_t len = 0;
    for (const char *c = scan->part_start; c != scan->c; ++c)
        ++len;

    part->raw = xmalloc(len + 1);

    memcpy(part->raw, scan->part_start, len);
    part->raw[len] = '\0';

    part->i = scan->part_start - scan->cmd;
    part->n = strlen(part->raw);

    scan->part_start = nullptr;
}

static void handle_single_quote(lx_scan *scan) {
    if (*scan->c == '\'') {
        try_end_part(scan);
        scan->mode = LX_M_NORMAL;
    }
}

static void handle_double_quote(lx_scan *scan) {
    if (scan->esc_pending) {
        scan->esc_pending = false;
    } else if (*scan->c == '\\') {
        scan->esc_pending = true;
    } else if (*scan->c == '"') {
        try_end_part(scan);
        scan->mode = LX_M_NORMAL;
    }
}

int lx_tokenize(const char *cmd, da_tok *toks) {
    if (!cmd || !toks)
        return LX_ERRINPUT;

    size_t cmd_len = strlen(cmd);
    if (cmd_len <= 0)
        return LX_ERRINPUT;

    lx_scan scan = {
        .mode = LX_M_NORMAL,
        .prev_mode = LX_M_NORMAL,
        .c = cmd,
        .cmd = cmd,
        .status = LX_OK
    };

    *toks = (da_tok){0};

    for (; *scan.c != '\0'; ++scan.c) {
        if (scan.mode == LX_M_NORMAL) {
            if (scan.esc_pending) {
                scan.esc_pending = false;
                continue;
            }

            if (*scan.c == '"') {
                scan.mode = LX_M_DOUBLEQ;

                try_end_part(&scan);
                ensure_tok(toks, LX_TOK_WORD, &scan);
                ++scan.c;
                ensure_part(&scan, LX_Q_DOUBLE);

                if (*scan.c == '\0')
                    break;

                handle_double_quote(&scan);
            } else if (*scan.c == '\'') {
                scan.mode = LX_M_SINGLEQ;

                try_end_part(&scan);
                ensure_tok(toks, LX_TOK_WORD, &scan);
                ++scan.c;
                ensure_part(&scan, LX_Q_SINGLE);

                if (*scan.c == '\0')
                    break;

                handle_single_quote(&scan);
            } else if (*scan.c == ' ') {
                try_end_part(&scan);
                try_end_tok(&scan, LX_WHITE_SPACE);
            } else if(*scan.c == '\\') {
                ensure_tok(toks, LX_TOK_WORD, &scan);
                ensure_part(&scan, LX_Q_NONE);
                scan.esc_pending = true;
            } else {
                lx_kind kind = get_kind(scan.c);

                if (kind_is_delim(kind)) {
                    try_end_part(&scan);
                    try_end_tok(&scan, kind);

                    ensure_tok(toks, kind, &scan);
                    try_end_tok(&scan, kind);

                    scan.c += tok_len(kind) - 1;
                }

                else if (kind == LX_TOK_UNKNOWN) {
                    ensure_tok(toks, LX_TOK_WORD, &scan);
                    ensure_part(&scan, LX_Q_NONE);
                }
            }
        } else if (scan.mode == LX_M_DOUBLEQ) {
            handle_double_quote(&scan);
        } else {
            handle_single_quote(&scan);
        }
    }

    if (scan.mode == LX_M_DOUBLEQ || scan.mode == LX_M_SINGLEQ)
        scan.status = LX_ERRNOENDQUOTE;

    if (scan.esc_pending)
        scan.status = LX_ERREMPTYESC;

    try_end_part(&scan);
    try_end_tok(&scan, LX_EOL);
    log_toks(toks);

    return scan.status;
}

char *tok_str(lx_kind kind) {
    switch (kind) {
    case LX_TOK_PIPE:    return "|";
    case LX_TOK_AMPER:      return "&";
    case LX_TOK_RDR_IN:  return "<";
    case LX_TOK_RDR_OUT: return ">";
    case LX_TOK_FD_OUT:  return ">&";
    case LX_TOK_FD_IN:  return "<&";
    case LX_TOK_APPEND:  return ">>";
    case LX_TOK_AND_IF:  return "&&";
    case LX_TOK_OR_IF:   return "||";
    case LX_TOK_LPAREN:  return "(";
    case LX_TOK_RPAREN:  return ")";
    case LX_TOK_UNKNOWN: return "Unknown";
    case LX_TOK_WORD:    return "[word]";
    case LX_TOK_IO_NUM:  return "[io]";
    default: break;
    }

    ASSERT(false && "tok_str: bad tok type");
    fatal(nullptr);
}

bool lx_kind_is_redir(lx_kind kind) {
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

void lx_tok_copy(lx_tok *dest, lx_tok *src) {
    *dest = *src;

    if (src->kind != LX_TOK_WORD)
        return;

   dest->parts = (da_part){0};

   for (size_t i = 0; i < src->parts.size; ++i) {
       lx_part *src_part = &src->parts.data[i];
       lx_part *dest_part = linc_array_push(&dest->parts);

       *dest_part = *src_part;
       dest_part->raw = xstrdup(src_part->raw);
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
    case LX_ERRMEM:        return "malloc failure";
    case LX_ERRNOENDQUOTE: return "unterminated quote";
    case LX_ERREMPTYESC:   return "bad escape";
    case LX_ERRINPUT:      return "empty input";
    case LX_OK:            return "success";
    default:               return "???";
    }
}
