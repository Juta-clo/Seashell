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

static int queue_redir(lx_kind kind, int append, ps_scanner *scanner) {
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
    default:             return -1;
    }

    scanner->queued_redir = redir;
    return 0;
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

void ps_free(ps_ast *ast) {
    if (!ast || !ast->allo.ctx)
        return;

    arena_destroy(ast->allo.ctx);

    *ast = (ps_ast){0};
}

int ps_parse(da_tok *tokens, ps_ast *ast) {
    ASSERT(tokens && ast);

    if (!tokens || !ast)
        return -1;

    if (tokens->size == 0)
        return -1;

    linc_allo allo = {
        .alloc = arena_alloc,
        .free = arena_free,
        .ctx = arena_create()
    };

    *ast = (ps_ast){ .allo = allo, .andors.allo = allo };

    ps_scanner scanner = {
        .cur_tok = &tokens->data[0],
        .andor_op = PS_NO_IF,
        .ast = ast
    };

    for (size_t i = 0; i < tokens->size; ++i, ++scanner.cur_tok) {
        if (!scanner.cur_pline || !scanner.cur_cmd || scanner.queued_redir)
            if (scanner.cur_tok->kind != LX_TOK_WORD)
                goto fail;

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

            if (!scanner.cur_cmd)
                add_cmd(&scanner.cur_pline->cmds, &scanner);

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
            if (i != tokens->size - 1)
                goto fail;

            ast->bg = true;
            break;
        }
        case LX_TOK_RDR_IN:
        case LX_TOK_RDR_OUT:
        case LX_TOK_RDR_ERR:
        case LX_TOK_APPEND: {
            if (!scanner.cur_pline || !scanner.cur_cmd)
                goto fail;

            const lx_kind kind = scanner.cur_tok->kind;
            const int append = kind == LX_TOK_APPEND;

            if (queue_redir(kind, append, &scanner) == -1)
                goto fail;
            break;
        }
        default:
            break;
        }
    }

    if (scanner.queued_redir || !scanner.cur_cmd)
        goto fail;

    return 0;

fail:
    ps_free(ast);
    return -1;
}
