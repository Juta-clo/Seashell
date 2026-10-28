#include <assert.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#include <linc/array.h>
#include "lexer.h"
#include "linc/xsys.h"

static lx_status status = LX_OK;

typedef struct {
    lx_mode mode;
    lx_mode prev_mode;
    lx_tok *cur_tok;
    const char *part_start;
    const char *cur_char;
    const char *cmd;
} lx_scanner;

char *lx_errstr(lx_status stat) {
    switch (stat) {
    case LX_ERRMEM:        return "malloc failure";
    case LX_ERRNOENDQUOTE: return "unterminated quote";
    case LX_ERREMPTYESC:   return "bad escape";
    case LX_ERRINPUT:      return "bad input";
    case LX_OK:            return "success";
    default:               return "???";
    }
}

static lx_kind get_kind(const char *cur_char) {
    if (strncmp("2>", cur_char, 2) == 0)
        return LX_TOK_RDR_ERR;
    if (strncmp(">>", cur_char, 2) == 0)
        return LX_TOK_APPEND;
    if (strncmp("&&", cur_char, 2) == 0)
        return LX_TOK_AND_IF;
    if (strncmp("||", cur_char, 2) == 0)
        return LX_TOK_OR_IF;

    if (strncmp("|", cur_char, 1) == 0)
        return LX_TOK_PIPE;
    if (strncmp("&", cur_char, 1) == 0)
        return LX_TOK_BG;
    if (strncmp("<", cur_char, 1) == 0)
        return LX_TOK_RDR_IN;
    if (strncmp(">", cur_char, 1) == 0)
        return LX_TOK_RDR_OUT;

    return LX_TOK_UNKNOWN;
}

static bool kind_is_delim(lx_kind kind) {
    switch (kind) {
    case LX_TOK_PIPE:
    case LX_TOK_BG:
    case LX_TOK_RDR_IN:
    case LX_TOK_RDR_OUT:
    case LX_TOK_RDR_ERR:
    case LX_TOK_APPEND:
    case LX_TOK_AND_IF:
    case LX_TOK_OR_IF:
        return true;
    default:
        return false;
    }
}

static bool kind_is_double_char_op(lx_kind kind) {
    switch (kind) {
    case LX_TOK_RDR_ERR:
    case LX_TOK_APPEND:
    case LX_TOK_AND_IF:
    case LX_TOK_OR_IF:
        return true;
    default:
        return false;
    }
}

static void ensure_tok(da_tok *tokens, lx_kind kind, lx_scanner *scanner) {
    if (!scanner->cur_tok) {
        lx_tok *tok = linc_array_push(tokens);
        *tok = (lx_tok){ .kind = kind };
        scanner->cur_tok = tok;
    }
}

static void ensure_part(lx_scanner *scanner, lx_quote quote) {
    if (!scanner->part_start) {
        lx_part *part = linc_array_push(&scanner->cur_tok->parts);
        *part = (lx_part){ .quote = quote };
        scanner->part_start = scanner->cur_char;
    }
}

static void end_part(lx_scanner *scanner) {
    assert(scanner);
    assert(scanner->cur_tok);
    assert(scanner->part_start);

    da_part *parts = &scanner->cur_tok->parts;
    lx_part *part = &parts->data[parts->size - 1];

    part->start = scanner->part_start - scanner->cmd;
    part->end = scanner->cur_char - scanner->cmd;

    size_t len = 0;
    for (const char *c = scanner->part_start; c != scanner->cur_char; ++c)
        ++len;

    part->raw = xmalloc(len + 1);

    memcpy(part->raw, scanner->part_start, len);
    part->raw[len] = '\0';

    scanner->part_start = NULL;
}

static void clear_part(lx_scanner *scanner) {
    if (scanner->cur_tok && scanner->part_start)
        end_part(scanner);
}

static void clear_empty_part(lx_scanner *scanner) {
    da_part *parts = &scanner->cur_tok->parts;
    lx_part *part = &parts->data[parts->size - 1];

    part->start = scanner->part_start - scanner->cmd;
    part->end = scanner->cur_char - scanner->cmd;

    part->raw = xmalloc(1);
    part->raw[0] = '\0';

    scanner->part_start = NULL;
}

static void handle_single_quote(lx_scanner *scanner) {
    if (*scanner->cur_char == '\'') {
        clear_part(scanner);

        scanner->mode = LX_M_NORMAL;
    }
}

static void handle_double_quote(lx_scanner *scanner) {
    if (*scanner->cur_char == '"') {
        clear_part(scanner);

        scanner->mode = LX_M_NORMAL;
    }
}

static int handle_normal(da_tok *tokens, lx_scanner *scanner) {
    if (*scanner->cur_char == '"') {
        if (scanner->cur_char[1] == '\0') {
            status = LX_ERRNOENDQUOTE;
            return -1;
        }

        clear_part(scanner);
        ++scanner->cur_char;

        ensure_tok(tokens, LX_TOK_WORD, scanner);
        ensure_part(scanner, LX_Q_DOUBLE);

        if (*scanner->cur_char == '"') {
            clear_empty_part(scanner);
        } else {
            if (*scanner->cur_char == '\\') {
                if (*++scanner->cur_char == '\0') {
                    status = LX_ERREMPTYESC;
                    return -1;
                }
            }
            scanner->mode = LX_M_DOUBLEQ;
        }
    } else if (*scanner->cur_char == '\'') {
        if (scanner->cur_char[1] == '\0') {
            status = LX_ERRNOENDQUOTE;
            return -1;
        }

        clear_part(scanner);
        ++scanner->cur_char;

        ensure_tok(tokens, LX_TOK_WORD, scanner);
        ensure_part(scanner, LX_Q_SINGLE);

        if (*scanner->cur_char == '\'') {
            clear_empty_part(scanner);
        } else {
            scanner->mode = LX_M_SINGLEQ;
        }
    } else if (*scanner->cur_char == ' ') {
        clear_part(scanner);
        scanner->cur_tok = NULL;
    } else {
        lx_kind kind = get_kind(scanner->cur_char);

        if (kind_is_delim(kind)) {
            clear_part(scanner);
            scanner->cur_tok = NULL;

            ensure_tok(tokens, kind, scanner);
            scanner->cur_tok = NULL;

            if (kind_is_double_char_op(kind))
                ++scanner->cur_char;
        }

        else if (kind == LX_TOK_UNKNOWN) {
            ensure_tok(tokens, LX_TOK_WORD, scanner);
            ensure_part(scanner, LX_Q_NONE);

            if (*scanner->cur_char == '\\') {
                if (*++scanner->cur_char == '\0') {
                    status = LX_ERREMPTYESC;
                    return -1;
                }
            }
        }
    }

    return 0;
}

void lx_free(da_tok *tokens) {
    if (!tokens)
        return;

    for (size_t i = 0; i < tokens->size; ++i) {
        lx_tok *tok = &tokens->data[i];

        for (size_t y = 0; y < tok->parts.size; ++y)
            free(tok->parts.data[y].raw);

        linc_array_free(&tok->parts);
    }

    linc_array_free(tokens);
    *tokens = (da_tok){0};
}

int lx_tokenize(const char *cmd, da_tok *tokens) {
    status = LX_OK;

    if (!cmd || !tokens)
        return LX_ERRINPUT;

    size_t cmd_len = strlen(cmd);
    if (cmd_len <= 0)
        return LX_ERRINPUT;

    lx_scanner scanner = {
        .mode = LX_M_NORMAL,
        .prev_mode = LX_M_NORMAL,
        .cur_char = cmd,
        .cmd = cmd
    };

    *tokens = (da_tok){0};

    for (; *scanner.cur_char != '\0'; ++scanner.cur_char) {
        switch (scanner.mode) {
        case LX_M_NORMAL:
            if (handle_normal(tokens, &scanner) == -1)
                goto fail;
            break;
        case LX_M_DOUBLEQ:
            handle_double_quote(&scanner);
            break;
        case LX_M_SINGLEQ:
            handle_single_quote(&scanner);
            break;
        }
    }

    if (scanner.mode == LX_M_DOUBLEQ || scanner.mode == LX_M_SINGLEQ) {
        status = LX_ERRNOENDQUOTE;
        goto fail;
    }

    clear_part(&scanner);
    scanner.cur_tok = NULL;

    return status;

fail:
    lx_free(tokens);
    return status;
}
