#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <linc.h>
#include <linc/array.h>
#include "parser.h"
#include "arena.h"
#include "lexer.h"
#include "utils.h"

#define ERR_MSG(...) \
    do { \
        if (scan->print_err) { \
            err_msg("parse error " __VA_ARGS__); \
            LOG_INFO("parse error " __VA_ARGS__); \
        } \
    } while (false)

#define ERR_TOK(_tok) \
    do { \
        if (scan->print_err) { \
            if ((_tok)->kind == LX_TOK_WORD) { \
                err_msg("parse error near '%s'", (_tok)->parts.data[0].raw); \
                LOG_INFO("parse error near '%s'", (_tok)->parts.data[0].raw); \
            } else { \
                err_msg("parse error near '%s'", tok_str((_tok)->kind)); \
                LOG_INFO("parse error near '%s'", tok_str((_tok)->kind)); \
            } \
        } \
    } while (false)

#define ERR_EOL() \
    do { \
        if (scan->print_err) { \
            if (scan->ast->nested) {  \
                err_msg("parse error near ')'"); \
            } else { \
                err_msg("parse error near '\\n'"); \
            } \
        } \
    } while (false)

typedef struct {
    int paren_n;
    bool print_err;
    bool need_tok;
    ps_pline *cur_pline;
    ps_cmd *cur_cmd;
    lx_tok *cur_tok;
    ps_redir *queued_redir;
    ps_andor_op andor_op;
    ps_ast *ast;
} ps_scanner;

static char *tok_str(lx_kind kind) {
    switch (kind) {
    case LX_TOK_PIPE:    return "|";
    case LX_TOK_BG:      return "&";
    case LX_TOK_RDR_IN:  return "<";
    case LX_TOK_RDR_OUT: return ">";
    case LX_TOK_RDR_ERR: return "2>";
    case LX_TOK_APPEND:  return ">>";
    case LX_TOK_AND_IF:  return "&&";
    case LX_TOK_OR_IF:   return "||";
    case LX_TOK_LPAREN:  return "(";
    case LX_TOK_RPAREN:  return ")";
    case LX_TOK_UNKNOWN: return "Unknown";
    case LX_TOK_WORD:    return "[word]";
    }
}

static void log_tokens(const da_tok *toks) {
    for (size_t i = 0; i < toks->size; ++i) {
        lx_tok *tok = &toks->data[i];

        if (tok->kind == LX_TOK_WORD) {
            LOG_INFO("    [\"%s\"]", tok->parts.data[0].raw);
        } else {
            LOG_INFO("    [%s]", tok_str(tok->kind));
        }
    }
}

static void copy_word(ps_word *dst, lx_tok *src, ps_scanner *scan) {
    ASSERT(dst);
    ASSERT(dst->segments.size == 0);

    for (size_t i = 0; i < src->parts.size; ++i) {
        lx_part *src_part = &src->parts.data[i];
        size_t n = strlen(src_part->raw) + 1;

        ps_segment *dst_segment = linc_array_push(&dst->segments);

        *dst_segment = (ps_segment){
            .raw = scan->ast->allo.alloc(n, scan->ast->allo.ctx),
        };

        memcpy(dst_segment->raw, src_part->raw, n);

        switch (src_part->quote) {
            case LX_Q_SINGLE: dst_segment->quote = PS_Q_SINGLE; break;
            case LX_Q_DOUBLE: dst_segment->quote = PS_Q_DOUBLE; break;
            case LX_Q_NONE:   dst_segment->quote = PS_Q_NONE;   break;
        }
    }
}

static void queue_redir(lx_kind kind, int append, ps_scanner *scan) {
    ASSERT(scan);

    ps_redir *redir = linc_array_push(&scan->cur_cmd->redirs);

    *redir = (ps_redir){
        .append = append,
        .target.segments.allo = scan->ast->allo
    };

    switch (kind) {
    case LX_TOK_RDR_IN:  redir->io_num = 0; break;
    case LX_TOK_RDR_OUT: redir->io_num = 1; break;
    case LX_TOK_RDR_ERR: redir->io_num = 2; break;
    case LX_TOK_APPEND:  redir->io_num = 1; break;
    default:
        fatal("unexpected paraser state");
    }

    scan->queued_redir = redir;
}

static void add_cmd(da_cmd *cmds, ps_scanner *scan) {
    ASSERT(scan);

    ps_cmd *cmd = linc_array_push(cmds);

    *cmd = (ps_cmd){
        .words.allo = scan->ast->allo,
        .redirs.allo = scan->ast->allo
    };

    scan->cur_cmd = cmd;
    scan->cur_cmd->kind = PS_CMD_SIMPLE;
}

static void add_andor(da_andor *andors, ps_andor_op op,
        ps_scanner *scanner) {
    ASSERT(andors);

    ps_andor *andor = linc_array_push(andors);

    *andor = (ps_andor){
        .op = op,
        .pline.cmds.allo = scanner->ast->allo
    };

    scanner->cur_pline = &andor->pline;
}

static int expand_alias(ps_scanner *scan, da_tok *toks, size_t i) {
    /* Is the word a potential alias expansion candidate? */
    ASSERT(scan->cur_tok->kind == LX_TOK_WORD);
    bool expandable = scan->cur_tok->parts.size == 1 &&
                      scan->cur_tok->parts.data[0].quote == LX_Q_NONE;
    if (!expandable)
        return 0;

    /* Is there an actual defined alias match? */
    char *alias = scan->cur_tok->parts.data[0].raw;
    void *value;
    mp_str_lookup(&env.aliases, alias, &value);
    if (!value)
        return 0;

    /* Is the alias value lexically valid? */
    da_tok alias_toks;
    int rv = lx_tokenize((char *)value, &alias_toks);
    if (rv != LX_OK) {
        ERR_MSG("on alias: %s", lx_errstr(rv));
        return -1;
    }

    /* Is the result syntactically valid given the context? */
    if (alias_toks.size == 0 || alias_toks.data[0].kind != LX_TOK_WORD) {
        ERR_MSG("on alias: %s -> '%s'", alias, (char *)value);
        lx_free(&alias_toks);
        return -1;
    }

    lx_free_tok(scan->cur_tok);
    linc_array_delete(toks, i, 1);

    memcpy(
        linc_array_insert(toks, i, alias_toks.size),
        alias_toks.data,
        alias_toks.size * sizeof(lx_tok)
    );

    linc_array_free(&alias_toks);
    scan->cur_tok = &toks->data[i];

    LOG_INFO("expanded alias {");
    log_tokens(&alias_toks);
    LOG_INFO("}");

    return 0;
}

static int parse_subshell(ps_scanner *scan, da_tok *toks, size_t *ind) {
    if (toks->data[*ind].kind != LX_TOK_LPAREN || *ind == toks->size - 1) {
        ERR_TOK(scan->cur_tok);
        return -1;
    }

    size_t i = *ind + 1;
    size_t n = 0;
    int paren_count = 1;

    for (; i < toks->size; ++i, ++n) {
        lx_kind kind = toks->data[i].kind;

        if (kind == LX_TOK_LPAREN)
            ++paren_count;

        if (kind == LX_TOK_RPAREN)
            --paren_count;

        if (paren_count == 0)
            break;
    }

    if (paren_count != 0 || n == 0) {
        if (i == toks->size) {
            ERR_EOL();
        } else {
            ERR_TOK(&toks->data[i]);
        }

        return -1;
    }

    lx_tok tok;
    da_tok subsh_toks = {0};

    for (size_t i = 0; i < n; ++i) {
        lx_tok_copy(&tok, &toks->data[*ind + 1 + i]);
        *linc_array_push(&subsh_toks) = tok;
    }

    scan->cur_cmd->kind = PS_CMD_SUBSH;
    scan->cur_cmd->subsh.allo = scan->ast->allo;
    scan->cur_cmd->subsh.nested = true;

    if (ps_parse(&subsh_toks, &scan->cur_cmd->subsh, scan->print_err) == -1) {
        lx_free(&subsh_toks);
        return -1;
    }

    scan->cur_tok += n + 1;
    *ind += n + 1;

    lx_free(&subsh_toks);
    return 0;
}

void ps_free(ps_ast *ast) {
    if (!ast || !ast->allo.ctx || ast->nested)
        return;

    arena_destroy(ast->allo.ctx);

    *ast = (ps_ast){0};
}

int ps_parse(da_tok *toks, ps_ast *ast, bool print_err) {
    ASSERT(toks && ast);

    if (!ast->andors.allo.alloc) {
        ast->allo = (linc_allo){
            .alloc = arena_alloc,
            .free = arena_free,
            .ctx = arena_create()
        };
    }

    ast->andors.allo = ast->allo;

    ps_scanner *scan = &(ps_scanner){
        .cur_tok = &toks->data[0],
        .andor_op = PS_NO_IF,
        .ast = ast,
        .print_err = print_err
    };

    if (!toks || !ast || toks->size == 0)
        goto fail;

    for (size_t i = 0; i < toks->size; ++i, ++scan->cur_tok) {
        lx_kind kind = scan->cur_tok->kind;

        /* Just parsed a delimeter so need a command */
        if (!scan->cur_pline || !scan->cur_cmd) {
            if (kind != LX_TOK_WORD && kind != LX_TOK_LPAREN)
                goto fail_tok;
        }

        /* Just parsed a command (subshell) */
        if (scan->cur_cmd && scan->cur_cmd->kind == PS_CMD_SUBSH) {
            if ((kind == LX_TOK_WORD && !scan->queued_redir))
                goto fail_tok;

            if (kind == LX_TOK_LPAREN)
                goto fail_tok;
        }

        /* Just parsed a redirect so need a word (target) */
        if (scan->queued_redir) {
            if (kind != LX_TOK_WORD)
                goto fail_tok;

            copy_word(&scan->queued_redir->target, scan->cur_tok, scan);
            scan->queued_redir = nullptr;

            continue;
        }

        switch (kind) {
        case LX_TOK_WORD:
            if (!scan->cur_pline)
                add_andor(&ast->andors, scan->andor_op, scan);

            if (!scan->cur_cmd) {
                add_cmd(&scan->cur_pline->cmds, scan);

                int expanded = expand_alias(scan, toks, i);
                if (expanded == -1)
                    goto fail;
            }

            scan->cur_cmd->words.allo = scan->ast->allo;
            ps_word *word = linc_array_push(&scan->cur_cmd->words);
            *word = (ps_word){ .segments.allo = scan->ast->allo };

            copy_word(word, scan->cur_tok, scan);

            scan->need_tok = false;
            break;
        case LX_TOK_LPAREN:
            if (!scan->cur_pline)
                add_andor(&ast->andors, scan->andor_op, scan);

            if (scan->cur_cmd)
                goto fail_tok;

            add_cmd(&scan->cur_pline->cmds, scan);

            if (parse_subshell(scan, toks, &i) == -1)
                goto fail;

            scan->need_tok = false;
            break;
        case LX_TOK_RPAREN:
            goto fail_tok;
        case LX_TOK_PIPE:
            scan->cur_cmd = nullptr;
            scan->need_tok = true;
            break;
        case LX_TOK_AND_IF:
            scan->cur_cmd = nullptr;
            scan->cur_pline = nullptr;
            scan->andor_op = PS_AND_IF;
            scan->need_tok = true;
            break;
        case LX_TOK_OR_IF:
            scan->cur_cmd = nullptr;
            scan->cur_pline = nullptr;
            scan->andor_op = PS_OR_IF;
            scan->need_tok = true;
            break;
        case LX_TOK_BG: {
            if (i != toks->size - 1)
                goto fail_tok;

            ast->bg = true;
            break;
        }
        case LX_TOK_RDR_IN:
        case LX_TOK_RDR_OUT:
        case LX_TOK_RDR_ERR:
        case LX_TOK_APPEND: {
            if (!scan->cur_pline || !scan->cur_cmd)
                goto fail_tok;

            const lx_kind kind = scan->cur_tok->kind;
            const int append = kind == LX_TOK_APPEND;

            queue_redir(kind, append, scan);
            break;
        }
        default:
            break;
        }
    }

    if (scan->queued_redir || scan->need_tok) {
        ERR_EOL();
        goto fail;
    }

    return 0;

fail_tok:
    ERR_TOK(scan->cur_tok);

fail:
    ps_free(ast);
    return -1;
}
