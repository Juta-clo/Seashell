#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <linc.h>
#include <linc/array.h>
#include "parser.h"
#include "arena.h"
#include "lexer.h"
#include "utils.h"

typedef struct {
    ps_pline *cur_pline;
    ps_cmd *cur_cmd;
    lx_tok *cur_tok;
    ps_redir *queued_redir;
    ps_andor_op andor_op;
    ps_ast *ast;
} ps_scanner;

static void log_tokens(const da_tok *toks) {
    for (size_t i = 0; i < toks->size; ++i) {
        lx_tok *t = &toks->data[i];
        switch (t->kind) {
        case LX_TOK_PIPE:    LOG_INFO("    [|]"); break;
        case LX_TOK_BG:      LOG_INFO("    [&]"); break;
        case LX_TOK_RDR_IN:  LOG_INFO("    [<]"); break;
        case LX_TOK_RDR_OUT: LOG_INFO("    [>]"); break;
        case LX_TOK_RDR_ERR: LOG_INFO("    [2>]"); break;
        case LX_TOK_APPEND:  LOG_INFO("    [>>]"); break;
        case LX_TOK_AND_IF:  LOG_INFO("    [&&]"); break;
        case LX_TOK_OR_IF:   LOG_INFO("    [||]"); break;
        case LX_TOK_UNKNOWN: LOG_INFO("    [Unknown]"); break;
        case LX_TOK_WORD:
            LOG_INFO("    [\"%s\"]", t->parts.data[0].raw);
            break;
        }
    }
}

static void copy_word(ps_word *dst, lx_tok *src, ps_scanner *scanner) {
    assert(dst);
    assert(dst->segments.size == 0);

    for (size_t i = 0; i < src->parts.size; ++i) {
        lx_part *src_part = &src->parts.data[i];
        size_t n = strlen(src_part->raw) + 1;

        ps_segment *dst_segment = linc_array_push(&dst->segments);

        *dst_segment = (ps_segment){
            .raw = scanner->ast->allo.alloc(n, scanner->ast->allo.ctx),
        };

        memcpy(dst_segment->raw, src_part->raw, n);

        switch (src_part->quote) {
            case LX_Q_SINGLE: dst_segment->quote = PS_Q_SINGLE; break;
            case LX_Q_DOUBLE: dst_segment->quote = PS_Q_DOUBLE; break;
            case LX_Q_NONE:   dst_segment->quote = PS_Q_NONE;   break;
        }
    }
}

static void queue_redir(lx_kind kind, int append, ps_scanner *scanner) {
    assert(scanner);

    ps_redir *redir = linc_array_push(&scanner->cur_cmd->redirs);

    *redir = (ps_redir){
        .append = append,
        .target.segments.allo = scanner->ast->allo
    };

    switch (kind) {
    case LX_TOK_RDR_IN:  redir->io_num = 0; break;
    case LX_TOK_RDR_OUT: redir->io_num = 1; break;
    case LX_TOK_RDR_ERR: redir->io_num = 2; break;
    case LX_TOK_APPEND:  redir->io_num = 1; break;
    default:
        xfatal("unexpected paraser state");
    }

    scanner->queued_redir = redir;
}

static void add_cmd(da_cmd *cmds, ps_scanner *scanner) {
    assert(scanner);

    ps_cmd *cmd = linc_array_push(cmds);

    *cmd = (ps_cmd){
        .words.allo = scanner->ast->allo,
        .redirs.allo = scanner->ast->allo
    };

    scanner->cur_cmd = cmd;
}

static void add_andor(da_andor *andors, ps_andor_op op,
        ps_scanner *scanner) {
    assert(andors);

    ps_andor *andor = linc_array_push(andors);

    *andor = (ps_andor){
        .op = op,
        .pline.cmds.allo = scanner->ast->allo
    };

    scanner->cur_pline = &andor->pline;
}

static int expand_alias(ps_scanner *scanner, da_tok *toks, size_t i) {
    /* Is the word a potential alias expansion candidate? */
    ASSERT(scanner->cur_tok->kind == LX_TOK_WORD);
    bool expandable = scanner->cur_tok->parts.size == 1 &&
                      scanner->cur_tok->parts.data[0].quote == LX_Q_NONE;
    if (!expandable)
        return 0;

    /* Is there an actual defined alias match? */
    char *alias = scanner->cur_tok->parts.data[0].raw;
    void *value;
    mp_str_lookup(&sh_env.aliases, alias, &value);
    if (!value)
        return 0;

    /* Is the alias value lexically valid? */
    da_tok alias_toks;
    int rv = lx_tokenize((char *)value, &alias_toks);
    if (rv != LX_OK) {
        err_msg("alias error: %s", lx_errstr(rv));
        return -1;
    }

    /* Is the result syntactically valid given the context? */
    if (alias_toks.size == 0 || alias_toks.data[0].kind != LX_TOK_WORD) {
        err_msg("alias error: %s -> '%s'", alias, (char *)value);
        lx_free(&alias_toks);
        return -1;
    }

    lx_free_tok(scanner->cur_tok);
    linc_array_delete(toks, i, 1);

    memcpy(
        linc_array_insert(toks, i, alias_toks.size),
        alias_toks.data,
        alias_toks.size * sizeof(lx_tok)
    );

    linc_array_free(&alias_toks);
    scanner->cur_tok = &toks->data[i];

    LOG_INFO("expanded alias {");
    log_tokens(&alias_toks);
    LOG_INFO("}");

    return 0;
}

void ps_free(ps_ast *ast) {
    if (!ast || !ast->allo.ctx)
        return;

    arena_destroy(ast->allo.ctx);

    *ast = (ps_ast){0};
}

int ps_parse(da_tok *toks, ps_ast *ast) {
    ASSERT(toks && ast);

    if (!toks || !ast)
        return -1;

    if (toks->size == 0)
        return -1;

    linc_allo allo = {
        .alloc = arena_alloc,
        .free = arena_free,
        .ctx = arena_create()
    };

    *ast = (ps_ast){ .allo = allo, .andors.allo = allo };

    ps_scanner scanner = {
        .cur_tok = &toks->data[0],
        .andor_op = PS_NO_IF,
        .ast = ast
    };

    for (size_t i = 0; i < toks->size; ++i, ++scanner.cur_tok) {
        if (!scanner.cur_pline || !scanner.cur_cmd || scanner.queued_redir) {
            if (scanner.cur_tok->kind != LX_TOK_WORD) {
                err_msg("syntax error: unexpected token");
                goto fail;
            }
        }

        switch (scanner.cur_tok->kind) {
        case LX_TOK_WORD: {
            if (scanner.queued_redir) {
                copy_word(
                    &scanner.queued_redir->target,
                    scanner.cur_tok,
                    &scanner
                );

                scanner.queued_redir = NULL;
                continue;
            }

            if (!scanner.cur_pline)
                add_andor(&ast->andors, scanner.andor_op, &scanner);

            if (!scanner.cur_cmd) {
                add_cmd(&scanner.cur_pline->cmds, &scanner);

                int expanded = expand_alias(&scanner, toks, i);
                if (expanded == -1)
                    goto fail;
            }

            scanner.cur_cmd->words.allo = scanner.ast->allo;
            ps_word *word = linc_array_push(&scanner.cur_cmd->words);
            *word = (ps_word){ .segments.allo = scanner.ast->allo };

            copy_word(word, scanner.cur_tok, &scanner);

            break;
        }
        case LX_TOK_PIPE:
            scanner.cur_cmd = NULL;
            break;
        case LX_TOK_AND_IF:
            scanner.cur_cmd = NULL;
            scanner.cur_pline = NULL;
            scanner.andor_op = PS_AND_IF;
            break;
        case LX_TOK_OR_IF:
            scanner.cur_cmd = NULL;
            scanner.cur_pline = NULL;
            scanner.andor_op = PS_OR_IF;
            break;
        case LX_TOK_BG: {
            if (i != toks->size - 1)
                goto fail;

            ast->bg = true;
            break;
        }
        case LX_TOK_RDR_IN:
        case LX_TOK_RDR_OUT:
        case LX_TOK_RDR_ERR:
        case LX_TOK_APPEND: {
            if (!scanner.cur_pline || !scanner.cur_cmd) {
                err_msg("syntax error: unexpected redirect");
                goto fail;
            }

            const lx_kind kind = scanner.cur_tok->kind;
            const int append = kind == LX_TOK_APPEND;

            queue_redir(kind, append, &scanner);
            break;
        }
        default:
            break;
        }
    }

    if (scanner.queued_redir || !scanner.cur_cmd) {
        err_msg("syntax error: bad terminating token");
        goto fail;
    }

    return 0;

fail:
    ps_free(ast);
    return -1;
}
