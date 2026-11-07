#include <assert.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#include <linc/array.h>
#include "lexer.h"
#include "linc/xsys.h"
#include "utils.h"

static lx_status status = LX_OK;

typedef struct {
    lx_mode mode;
    lx_mode prev_mode;
    lx_tok *cur_tok;
    const char *part_start;
    const char *cur_char;
    const char *cmd;
    bool fd_dup_pending;
} lx_scan;

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

static bool kind_is_double_char_op(lx_kind kind) {
    switch (kind) {
    case LX_TOK_APPEND:
    case LX_TOK_AND_IF:
    case LX_TOK_OR_IF:
    case LX_TOK_FD_OUT:
    case LX_TOK_FD_IN:
        return true;
    default:
        return false;
    }
}

static void ensure_tok(da_tok *toks, lx_kind kind, lx_scan *scan) {
    if (!scan->cur_tok) {
        lx_tok *tok = linc_array_push(toks);
        *tok = (lx_tok){ .kind = kind };
        scan->cur_tok = tok;
    }
}

static void ensure_part(lx_scan *scan, lx_quote quote) {
    if (!scan->part_start) {
        lx_part *part = linc_array_push(&scan->cur_tok->parts);
        *part = (lx_part){ .quote = quote };
        scan->part_start = scan->cur_char;
    }
}

static int lex_io_num(const lx_scan *scan) {
    const lx_tok *tok = scan->cur_tok;
    int io_num;

    for (const char *c = tok->parts.data[0].raw; *c != '\0'; ++c)
        if (*c < 48 || *c > 57)
            return -1;

    if (get_int(tok->parts.data[0].raw, &io_num) == -1 || io_num < 0) {
        LOG_INFO("io-num conversion failed: %s", tok->parts.data[0].raw);
        return -1;
    }

    return io_num;
}

static void end_tok(lx_scan *scan, lx_kind delim) {
    lx_tok *tok = scan->cur_tok;
    char *endptr;

    if (!tok)
        return;

    bool not_number = (
        tok->kind != LX_TOK_WORD ||
        tok->parts.size > 1 ||
        tok->parts.data[0].quote != LX_Q_NONE
    );

    if (not_number) {
        if (scan->cur_tok->kind != LX_WHITE_SPACE) {
            scan->fd_dup_pending = (
                tok->kind == LX_TOK_FD_IN ||
                tok->kind == LX_TOK_FD_OUT
            );
        }

        scan->cur_tok = nullptr;
        return;
    }

    strtol(tok->parts.data[0].raw, &endptr, 10);
    tok->is_num = (*endptr == '\0');

    if (delim == LX_TOK_RDR_OUT || delim == LX_TOK_RDR_IN ||
            delim == LX_TOK_APPEND || delim == LX_TOK_FD_OUT ||
            delim == LX_TOK_FD_IN || scan->fd_dup_pending) {
        int io_num = lex_io_num(scan);
        if (io_num == -1) {
            LOG_INFO("io-num lex failed: %s", tok->parts.data[0].raw);
            goto done;
        }

        lx_free_tok(tok);
        tok->kind = LX_TOK_IO_NUM;
        tok->io_num = io_num;
        LOG_INFO("lexed io-num: %d", io_num);
    }

done:
    scan->fd_dup_pending = false;
    scan->cur_tok = nullptr;
}

static void end_part(lx_scan *scan) {
    ASSERT(scan);
    ASSERT(scan->cur_tok);
    ASSERT(scan->part_start);

    da_part *parts = &scan->cur_tok->parts;
    lx_part *part = &parts->data[parts->size - 1];

    part->start = scan->part_start - scan->cmd;
    part->end = scan->cur_char - scan->cmd;

    size_t len = 0;
    for (const char *c = scan->part_start; c != scan->cur_char; ++c)
        ++len;

    part->raw = xmalloc(len + 1);

    memcpy(part->raw, scan->part_start, len);
    part->raw[len] = '\0';

    scan->part_start = nullptr;
}

static void clear_part(lx_scan *scan) {
    if (scan->cur_tok && scan->part_start)
        end_part(scan);
}

static void clear_empty_part(lx_scan *scan) {
    da_part *parts = &scan->cur_tok->parts;
    lx_part *part = &parts->data[parts->size - 1];

    part->start = scan->part_start - scan->cmd;
    part->end = scan->cur_char - scan->cmd;

    part->raw = xmalloc(1);
    part->raw[0] = '\0';

    scan->part_start = nullptr;
}

static void handle_single_quote(lx_scan *scan) {
    if (*scan->cur_char == '\'') {
        clear_part(scan);

        scan->mode = LX_M_NORMAL;
    }
}

static void handle_double_quote(lx_scan *scan) {
    if (*scan->cur_char == '"') {
        clear_part(scan);

        scan->mode = LX_M_NORMAL;
    }
}

static int handle_normal(da_tok *toks, lx_scan *scan) {
    if (*scan->cur_char == '"') {
        if (scan->cur_char[1] == '\0') {
            status = LX_ERRNOENDQUOTE;
            return -1;
        }

        clear_part(scan);
        ++scan->cur_char;

        ensure_tok(toks, LX_TOK_WORD, scan);
        ensure_part(scan, LX_Q_DOUBLE);

        if (*scan->cur_char == '"') {
            clear_empty_part(scan);
        } else {
            if (*scan->cur_char == '\\') {
                if (*++scan->cur_char == '\0') {
                    status = LX_ERREMPTYESC;
                    return -1;
                }
            }
            scan->mode = LX_M_DOUBLEQ;
        }
    } else if (*scan->cur_char == '\'') {
        if (scan->cur_char[1] == '\0') {
            status = LX_ERRNOENDQUOTE;
            return -1;
        }

        clear_part(scan);
        ++scan->cur_char;

        ensure_tok(toks, LX_TOK_WORD, scan);
        ensure_part(scan, LX_Q_SINGLE);

        if (*scan->cur_char == '\'') {
            clear_empty_part(scan);
        } else {
            scan->mode = LX_M_SINGLEQ;
        }
    } else if (*scan->cur_char == ' ') {
        clear_part(scan);
        end_tok(scan, LX_WHITE_SPACE);
    } else {
        lx_kind kind = get_kind(scan->cur_char);

        if (kind_is_delim(kind)) {
            clear_part(scan);
            end_tok(scan, kind);

            ensure_tok(toks, kind, scan);
            end_tok(scan, kind);

            if (kind_is_double_char_op(kind))
                ++scan->cur_char;
        }

        else if (kind == LX_TOK_UNKNOWN) {
            ensure_tok(toks, LX_TOK_WORD, scan);
            ensure_part(scan, LX_Q_NONE);

            if (*scan->cur_char == '\\') {
                if (*++scan->cur_char == '\0') {
                    status = LX_ERREMPTYESC;
                    return -1;
                }
            }
        }
    }

    return 0;
}

void lx_tok_copy(lx_tok *dest, lx_tok *src) {
    if (src->kind != LX_TOK_WORD) {
        *dest = *src;
        return;
    }

   *dest = (lx_tok){ .kind = src->kind, .is_num = src->is_num };

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

int lx_tokenize(const char *cmd, da_tok *toks) {
    status = LX_OK;

    if (!cmd || !toks)
        return LX_ERRINPUT;

    size_t cmd_len = strlen(cmd);
    if (cmd_len <= 0)
        return LX_ERRINPUT;

    lx_scan scan = {
        .mode = LX_M_NORMAL,
        .prev_mode = LX_M_NORMAL,
        .cur_char = cmd,
        .cmd = cmd
    };

    *toks = (da_tok){0};

    for (; *scan.cur_char != '\0'; ++scan.cur_char) {
        switch (scan.mode) {
        case LX_M_NORMAL:
            if (handle_normal(toks, &scan) == -1)
                goto fail;
            break;
        case LX_M_DOUBLEQ:
            handle_double_quote(&scan);
            break;
        case LX_M_SINGLEQ:
            handle_single_quote(&scan);
            break;
        }
    }

    if (scan.mode == LX_M_DOUBLEQ || scan.mode == LX_M_SINGLEQ) {
        status = LX_ERRNOENDQUOTE;
        goto fail;
    }

    clear_part(&scan);
    end_tok(&scan, LX_EOL);

    return status;

fail:
    lx_free(toks);
    return status;
}
