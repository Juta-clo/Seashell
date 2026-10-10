#include <assert.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#include "linc_tools.h"
#include "lexer.h"

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

static int init_part(lx_part *part) {
    assert(part);

    *part = (lx_part){0};

    return 0;
}

static void free_part(lx_part *part) {
    free(part->raw);
    *part = (lx_part){0};
}

static int init_tok(lx_tok *tok) {
    assert(tok);

    *tok = (lx_tok){0};
    da_init(&tok->parts);

    return 0;
}

static void free_tok(lx_tok *tok) {
    for (size_t i = 0; i < tok->parts.size; ++i) {
        lx_part *part = &tok->parts.data[i];
        free_part(part);
    }
    da_free(&tok->parts);
    *tok = (lx_tok){0};
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

static int add_tok(da_tok *tokens, lx_kind kind, lx_scanner *scanner) {
    assert(scanner);
    assert(!scanner->cur_tok);

    lx_tok *tok = da_push_init(tokens, init_tok);

    tok->kind = kind;

    scanner->cur_tok = tok;
    return 0;
}

static int ensure_tok(da_tok *tokens, lx_kind kind, lx_scanner *scanner) {
    if (!scanner->cur_tok)
        if (add_tok(tokens, kind, scanner) == -1)
            return -1;
    return 0;
}

static void end_tok(lx_scanner *scanner) {
    assert(scanner);
    scanner->cur_tok = NULL;
}

static int add_part(lx_scanner *scanner, lx_quote quote) {
    assert(scanner);
    assert(scanner->cur_tok);
    assert(!scanner->part_start);

    lx_part *part = da_push_init(&scanner->cur_tok->parts, init_part);

    part->quote = quote;

    scanner->part_start = scanner->cur_char;
    return 0;
}

static int ensure_part(lx_scanner *scanner, lx_quote quote) {
    if (!scanner->part_start)
        if (add_part(scanner, quote) == -1)
            return -1;
    return 0;
}

/* TODO: maybe refactor alloc from setting scanner->part_start */
static int end_part(lx_scanner *scanner) {
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

    part->raw = malloc(len + 1);
    if (!part->raw) {
        status = LX_ERRMEM;
        return -1;
    }

    memcpy(part->raw, scanner->part_start, len);
    part->raw[len] = '\0';

    scanner->part_start = NULL;
    return 0;
}

static int clear_part(lx_scanner *scanner) {
    if (scanner->cur_tok && scanner->part_start)
        if (end_part(scanner) == -1)
            return -1;
    return 0;
}

static int clear_empty_part(lx_scanner *scanner) {
    da_part *parts = &scanner->cur_tok->parts;
    lx_part *part = &parts->data[parts->size - 1];

    part->raw = malloc(1);
    if (!part->raw) {
        status = LX_ERRMEM;
        return -1;
    }
    part->raw[0] = '\0';

    scanner->part_start = NULL;
    return 0;
}

static int handle_single_quote(lx_scanner *scanner) {
    if (*scanner->cur_char == '\'') {
        if (clear_part(scanner) == -1)
            return -1;
        scanner->mode = LX_M_NORMAL;
    }

    return 0;
}

static int handle_double_quote(lx_scanner *scanner) {
    if (*scanner->cur_char == '"') {
        if (clear_part(scanner) == -1)
            return -1;
        scanner->mode = LX_M_NORMAL;
    }

    return 0;
}

static int handle_normal(da_tok *tokens, lx_scanner *scanner) {
    if (*scanner->cur_char == '"') {
        if (scanner->cur_char[1] == '\0') {
            status = LX_ERRNOENDQUOTE;
            return -1;
        }

        clear_part(scanner);
        ++scanner->cur_char;

        if (ensure_tok(tokens, LX_TOK_WORD, scanner) == -1)
            return -1;
        if (ensure_part(scanner, LX_Q_DOUBLE) == -1)
                return -1;

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
    }

    else if (*scanner->cur_char == '\'') {
        if (scanner->cur_char[1] == '\0') {
            status = LX_ERRNOENDQUOTE;
            return -1;
        }

        clear_part(scanner);
        ++scanner->cur_char;

        if (ensure_tok(tokens, LX_TOK_WORD, scanner) == -1)
            return -1;
        if (ensure_part(scanner, LX_Q_SINGLE) == -1)
                return -1;

        if (*scanner->cur_char == '\'') {
            clear_empty_part(scanner);
        } else {
            scanner->mode = LX_M_SINGLEQ;
        }
    }

    else if (*scanner->cur_char == ' ') {
        if (clear_part(scanner) == -1)
            return -1;
        end_tok(scanner);
    } else {
        lx_kind kind = get_kind(scanner->cur_char);

        if (kind_is_delim(kind)) {
            if (clear_part(scanner) == -1)
                return -1;
            end_tok(scanner);

            if (ensure_tok(tokens, kind, scanner) == -1)
                return -1;
            end_tok(scanner);

            if (kind_is_double_char_op(kind))
                ++scanner->cur_char;
        }

        else if (kind == LX_TOK_UNKNOWN) {
            if (ensure_tok(tokens, LX_TOK_WORD, scanner) == -1)
                return -1;
            if (ensure_part(scanner, LX_Q_NONE) == -1)
                return -1;
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
    for (size_t i = 0; i < tokens->size; ++i) {
        lx_tok *tok = &tokens->data[i];
        free_tok(tok);
    }
    da_free(tokens);
}

int lx_tokenize(const char *cmd, da_tok *tokens) {
    status = LX_OK;

    if (!cmd || !tokens)
        return LX_ERRINPUT;

    size_t cmd_len = strlen(cmd);
    if (cmd_len <= 0)
        return LX_ERRINPUT;

    da_init(tokens);

    lx_scanner scanner = { LX_M_NORMAL, LX_M_NORMAL, NULL, NULL, cmd, cmd };

    for (; *scanner.cur_char != '\0'; ++scanner.cur_char) {
        switch (scanner.mode) {
        case LX_M_NORMAL:
            if (handle_normal(tokens, &scanner) == -1)
                goto fail;
            break;
        case LX_M_DOUBLEQ:
            if (handle_double_quote(&scanner) == -1)
                goto fail;
            break;
        case LX_M_SINGLEQ:
            if (handle_single_quote(&scanner) == -1)
                goto fail;
            break;
        }
    }

    if (scanner.mode == LX_M_DOUBLEQ || scanner.mode == LX_M_SINGLEQ) {
        status = LX_ERRNOENDQUOTE;
        goto fail;
    }

    if (tokens->size == 0)
        da_free(tokens);

    if (clear_part(&scanner) == -1)
        goto fail;
    end_tok(&scanner);

    return status;

fail:
    lx_free(tokens);
    return status;
}
