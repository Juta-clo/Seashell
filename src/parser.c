#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "parser.h"
#include "lexer.h" // IWYU pragma: keep - See 2026-06-25 Notes
#include "darr.h"

typedef struct {
    ps_pline *cur_pline;
    ps_cmd *cur_cmd;
    lx_tok *cur_tok;
    ps_redir *queued_redir;
    ps_andor_op cur_andor_op;
} ps_scanner;

static int init_ps_segment(ps_segment *segment) {
    assert(segment);

    *segment = (ps_segment){0};

    return 0;
}

static void free_ps_segment(ps_segment *segment) {
    free(segment->raw);
    *segment = (ps_segment){0};
}

static int init_word(ps_word *word) {
    assert(word);

    *word = (ps_word){0};
    da_init(&word->segments);

    return 0;
}

static void free_word(ps_word *word) {
    if (!word)
        return;

    free(word->arg);

    for (size_t i = 0; i < word->segments.size; ++i)
        free_ps_segment(&word->segments.data[i]);
    da_free(&word->segments);
    *word = (ps_word){0};
}

static int init_redir(ps_redir *redir) {
    assert(redir);

    *redir = (ps_redir){0};
    da_init(&redir->target.segments);

    return 0;
}

static void free_redir(ps_redir *redir) {
    if (!redir)
        return;

    free_word(&redir->target);
    *redir = (ps_redir){0};
}

static int init_cmd(ps_cmd *cmd) {
    assert(cmd);

    *cmd = (ps_cmd){0};
    da_init(&cmd->words);
    da_init(&cmd->redirs);

    return 0;
}

static void free_cmd(ps_cmd *cmd) {
    if (!cmd)
        return;

    if (cmd->argv) {
        for (char **arg = cmd->argv; *arg != NULL; ++arg)
            free(*arg);

        free(cmd->argv);
    }

    for (size_t i = 0; i < cmd->words.size; ++i)
        free_word(&cmd->words.data[i]);
    for (size_t i = 0; i < cmd->redirs.size; ++i)
        free_redir(&cmd->redirs.data[i]);

    da_free(&cmd->words);
    da_free(&cmd->redirs);

    *cmd = (ps_cmd){0};
}

static int init_pline(ps_pline *pline) {
    assert(pline);

    *pline = (ps_pline){0};
    da_init(&pline->cmds);

    return 0;
}

static void free_pline(ps_pline *pline) {
    if (!pline)
        return;

    for (size_t i = 0; i < pline->cmds.size; ++i)
        free_cmd(&pline->cmds.data[i]);
    da_free(&pline->cmds);

    *pline = (ps_pline){0};
}

static int init_andor(ps_andor *andor) {
    assert(andor);

    *andor = (ps_andor){0};
    if (init_pline(&andor->pline) == -1)
        return -1;

    return 0;
}

static void free_andor(ps_andor *andor) {
    if (!andor)
        return;

    free_pline(&andor->pline);

    *andor = (ps_andor){0};
}

static int init_job(ps_ast *ast) {
    assert(ast);

    *ast = (ps_ast){0};
    da_init(&ast->andors);

    return 0;
}

static int cpy_word(ps_word *dst, lx_tok *src) {
    assert(dst);
    assert(dst->segments.size == 0);

    for (size_t i = 0; i < src->parts.size; ++i) {
        lx_part *src_part = &src->parts.data[i];

        ps_segment *dst_segment = da_push_init(&dst->segments, init_ps_segment);

        size_t n = strlen(src_part->raw) + 1;

        dst_segment->raw = malloc(n);
        if (!dst_segment->raw)
            return -1;

        memcpy(dst_segment->raw, src_part->raw, n);

        switch (src_part->quote) {
            case LX_Q_SINGLE: dst_segment->quote = PS_Q_SINGLE; break;
            case LX_Q_DOUBLE: dst_segment->quote = PS_Q_DOUBLE; break;
            case LX_Q_NONE:   dst_segment->quote = PS_Q_NONE;   break;
        }
    }

    return 0;
}

static int add_andor(da_andor *andors, ps_andor_op op,
        ps_scanner *scanner) {
    assert(andors);

    ps_andor *andor = da_push_init(andors, init_andor);

    andor->op = op;

    scanner->cur_pline = &andor->pline;

    return 0;
}

static int ensure_pline(da_andor *andors, ps_andor_op op,
        ps_scanner *scanner) {
    if (!scanner->cur_pline)
        if (add_andor(andors, op, scanner) == -1)
            return -1;

    return 0;
}

static int add_cmd(da_cmd *cmds, ps_scanner *scanner) {
    assert(scanner);

    ps_cmd *cmd = da_push_init(cmds, init_cmd);

    scanner->cur_cmd = cmd;

    return 0;
}

static int ensure_cmd(ps_pline *pline, ps_scanner *scanner) {
    if (!scanner->cur_cmd)
        if (add_cmd(&pline->cmds, scanner) == -1)
            return -1;

    return 0;
}

static int queue_redir(lx_kind kind, int append, ps_scanner *scanner) {
    assert(scanner);

    ps_redir *redir = da_push_init(&scanner->cur_cmd->redirs, init_redir);

    redir->append = append;

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

void ps_free(ps_ast *ast) {
    if (!ast)
        return;

    for (size_t i = 0; i < ast->andors.size; ++i)
        free_andor(&ast->andors.data[i]);
    da_free(&ast->andors);

    *ast = (ps_ast){0};
}

int ps_parse(da_tok *tokens, ps_ast *ast) {
    if (!tokens || !ast)
        return -1;

    if (tokens->size == 0)
        return -1;

    if (init_job(ast) == -1)
        goto fail;

    ps_scanner scanner = {0};
    scanner.cur_tok = &tokens->data[0];
    scanner.cur_andor_op = PS_NO_IF;

    for (size_t i = 0; i < tokens->size; ++i, ++scanner.cur_tok) {
        if (!scanner.cur_pline || !scanner.cur_cmd || scanner.queued_redir) {
            if (scanner.cur_tok->kind != LX_TOK_WORD)
                goto fail;
        }

        switch (scanner.cur_tok->kind) {
        case LX_TOK_WORD: {
            if (scanner.queued_redir) {
                if (cpy_word(&scanner.queued_redir->target, scanner.cur_tok) == -1)
                    goto fail;
                scanner.queued_redir = NULL;
                continue;
            }

            ensure_pline(&ast->andors, scanner.cur_andor_op, &scanner);
            ensure_cmd(scanner.cur_pline, &scanner);

            ps_word *word = da_push_init(&scanner.cur_cmd->words, init_word);

            if (init_word(word) == -1)
                goto fail;

            if (cpy_word(word, scanner.cur_tok) == -1)
                goto fail;

            break;
        }

        case LX_TOK_PIPE:
            scanner.cur_cmd = NULL;
            break;

        case LX_TOK_AND_IF:
            scanner.cur_cmd = NULL;
            scanner.cur_pline = NULL;
            scanner.cur_andor_op = PS_AND_IF;
            break;

        case LX_TOK_OR_IF:
            scanner.cur_cmd = NULL;
            scanner.cur_pline = NULL;
            scanner.cur_andor_op = PS_OR_IF;
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
