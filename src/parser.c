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

static lx_tok *scan_advance(size_t n, ps_scan *scan) {
    ASSERT_(LEQ, n, scan->toks->size - scan->i);

    scan->cur_tok += n;
    scan->i += n;

    return scan->cur_tok;
}

static lx_tok *scan_final(ps_scan *scan) {
    return (scan->i == scan->toks->size - 1) ? scan->cur_tok : nullptr;
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

static void copy_word(ps_word *dst, lx_tok *src, ps_scan *scan) {
    ASSERT(dst);
    ASSERT(dst->segments.size == 0);

    dst->i = src->i;
    dst->n = src->n;

    for (size_t i = 0; i < src->parts.size; ++i) {
        lx_part *src_part = &src->parts.data[i];
        size_t n = strlen(src_part->raw) + 1;

        ps_segment *dst_segment = linc_array_push(&dst->segments);

        *dst_segment = (ps_segment){
            .i = src_part->i,
            .n = src_part->n,
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

static void queue_redir(int io_num, bool fd_dup, ps_scan *scan) {
    ASSERT(scan);

    ps_redir *redir = linc_array_push(&scan->cur_cmd->redirs);

    if (io_num == -1) {
        switch (scan->cur_tok->kind) {
        case LX_TOK_RDR_IN:  io_num = 0; break;
        case LX_TOK_RDR_OUT: io_num = 1; break;
        case LX_TOK_APPEND:  io_num = 1; break;
        case LX_TOK_FD_OUT:  io_num = 1; break;
        case LX_TOK_FD_IN:   io_num = 0; break;
        default: fatal("unexpected paraser state");
        }
    }

    *redir = (ps_redir){
        .io_num = io_num,
        .append = (scan->cur_tok->kind == LX_TOK_APPEND),
        .rdr_in = (scan->cur_tok->kind == LX_TOK_RDR_IN ||
                   scan->cur_tok->kind == LX_TOK_FD_IN),
        .fd_dup = fd_dup,
        .target.segments.allo = scan->ast->allo
    };

    scan->queued_redir = redir;
}

static void add_cmd(da_cmd *cmds, ps_scan *scan) {
    ASSERT(scan);

    ps_cmd *cmd = linc_array_push(cmds);

    *cmd = (ps_cmd){
        .i = scan->cur_tok->i,
        .words.allo = scan->ast->allo,
        .redirs.allo = scan->ast->allo
    };

    scan->cur_cmd = cmd;
    scan->cur_cmd->kind = PS_CMD_SIMPLE;
}

static void add_andor(da_andor *andors, ps_andor_op op, ps_scan *scan) {
    ASSERT(andors);

    ps_andor *andor = linc_array_push(andors);

    *andor = (ps_andor){
        .i = scan->cur_tok->i,
        .op = op,
        .pline.cmds.allo = scan->ast->allo
    };

    scan->cur_andor = andor;
}

static void end_cmd(ps_scan *scan) {
    if (!scan->cur_cmd || scan->i == 0)
        return;

    ps_cmd *cmd = scan->cur_cmd;
    lx_tok *tok = &scan->cur_tok[-1];

    cmd->n = (tok->i + tok->n) - cmd->i;
    scan->cur_cmd = nullptr;
}

static void end_andor(ps_scan *scan) {
    if (!scan->cur_andor || scan->i == 0)
        return;

    ps_andor *andor = scan->cur_andor;
    lx_tok *tok = &scan->cur_tok[-1];

    andor->n = (tok->i + tok->n) - andor->i;
    scan->cur_andor = nullptr;
}

static int expand_alias(ps_scan *scan) {
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

    lx_shift_toks(scan->cur_tok->i, alias_toks.size, alias_toks.data);
    int len = strlen(value) - strlen(alias);

    scan->alias_shift += len;

    if (!scan_final(scan))
        lx_shift_toks(len, scan->toks->size - (scan->i + 1),
                &scan->toks->data[scan->i + 1]);

    lx_free_tok(scan->cur_tok);
    linc_array_delete(scan->toks, scan->i, 1);

    memcpy(
        linc_array_insert(scan->toks, scan->i, alias_toks.size),
        alias_toks.data,
        alias_toks.size * sizeof(lx_tok)
    );

    LOG_INFO("expanded alias {");
    log_tokens(&alias_toks);
    LOG_INFO("}");

    linc_array_free(&alias_toks);
    scan->cur_tok = &scan->toks->data[scan->i];

    return 0;
}

static int parse_subshell(ps_scan *scan) {
    int status = 0;

    if (scan->toks->data[scan->i].kind != LX_TOK_LPAREN) {
        ERR_TOK(scan->cur_tok);
        return -1;
    }

    size_t i = scan->i + 1;
    size_t n = 0;
    int paren_count = 1;

    for (; i < scan->toks->size; ++i, ++n) {
        lx_kind kind = scan->toks->data[i].kind;

        if (kind == LX_TOK_LPAREN)
            ++paren_count;

        if (kind == LX_TOK_RPAREN)
            --paren_count;

        if (paren_count == 0)
            break;
    }

    if (paren_count != 0 || n == 0) {
        if (i == scan->toks->size) {
            ERR_EOL();
        } else {
            ERR_TOK(&scan->toks->data[i]);
        }

        status = -1;
    }

    lx_tok tok;
    da_tok subsh_toks = {0};
    int rparen_i = scan->i + n + 1;

    for (size_t i = 0; i < n; ++i) {
        lx_tok_copy(&tok, &scan->toks->data[scan->i + 1 + i]);
        *linc_array_push(&subsh_toks) = tok;
    }

    scan->cur_cmd->kind = PS_CMD_SUBSH;

    scan->cur_cmd->subsh = (ps_ast){
        .allo = scan->ast->allo,
        .nested = true
    };

    ps_scan subsh_scan = {
        .toks = &subsh_toks,
        .print_err = scan->print_err,
        .alias = scan->alias,
    };

    if (ps_parse(&subsh_scan, &scan->cur_cmd->subsh) == -1)
        status = -1;

    int shift = subsh_scan.alias_shift;
    int shift_n = scan->toks->size - (rparen_i);

    scan->alias_shift += subsh_scan.alias_shift;

    if (!scan_final(scan))
        lx_shift_toks(shift, shift_n, &scan->toks->data[rparen_i]);

    scan_advance(n + 1, scan);

    lx_free(&subsh_toks);

    return status;
}

static bool in_andor(int i, ps_ast *ast, ps_info *info) {
    for (size_t andor_i = 0; andor_i < ast->andors.size; ++andor_i) {
        ps_andor *andor = &ast->andors.data[andor_i];

        if (i < andor->i || i > andor->i + andor->n)
            continue;

        if (andor->op == PS_AND_IF) {
            info->kinds |= PS_ANDIF;
        } else if (andor->op == PS_OR_IF) {
            info->kinds |= PS_ORIF;
        } else {
            info->kinds |= PS_NOIF;
        }

        info->andor = andor;

        return true;
    }

    return false;
}

static bool in_cmd(int i, ps_andor *andor, ps_info *info) {
    for (size_t cmd_i = 0; cmd_i < andor->pline.cmds.size; ++cmd_i) {
        ps_cmd *cmd = &andor->pline.cmds.data[cmd_i];

        if (i < cmd->i || i > cmd->i + cmd->n)
            continue;

        info->kinds |= PS_CMD;
        info->cmd = cmd;

        return true;
    }

    return false;
}

static bool in_word(int i, ps_cmd *cmd, ps_info *info) {
    for (size_t word_i = 0; word_i < cmd->words.size; ++word_i) {
        ps_word *word = &cmd->words.data[word_i];

        if (i < word->i || i > word->i + word->n)
            continue;

        if (word_i == 0) {
            info->kinds |= PS_NAME;
        } else {
            info->kinds |= PS_ARG;
        }

        info->kinds |= PS_CMD;
        info->word = word;

        return true;
    }

    return false;
}

static bool in_redir(int i, ps_cmd *cmd, ps_info *info) {
    for (size_t redir_i = 0; redir_i < cmd->redirs.size; ++redir_i) {
        ps_redir *redir = &cmd->redirs.data[redir_i];
        ps_word *target = &cmd->redirs.data[redir_i].target;

        if (target->segments.size == 0)
            continue;

        if (i < target->i || i > target->i + target->n)
            continue;

        info->kinds |= PS_REDIR;
        info->redir = redir;
        info->word = target;

        return true;
    }

    return false;
}

static bool in_segment(int i, ps_word *word, ps_info *info) {
    for (size_t seg_i = 0; seg_i < word->segments.size; ++seg_i) {
        ps_segment *seg = &word->segments.data[seg_i];

        if (i < seg->i || i > seg->i + seg->n)
            continue;

        info->kinds |= PS_SEG;
        info->seg = seg;

        return true;
    }

    return false;
}

void ps_get_info(int i, ps_ast *ast, ps_info *info) {
    ASSERT(ast && i >= 0 && info);

    *info = (ps_info){0};

    if (ast->andors.size == 0) {
        info->kinds |= PS_EMPTY;
        return;
    }

    if (in_andor(i, ast, info)) {
        if (in_cmd(i, info->andor, info)) {
            if (in_redir(i, info->cmd, info)) {
                in_segment(i, &info->redir->target, info);
            } else if (info->cmd->kind == PS_CMD_SUBSH) {
                ps_get_info(i, &info->cmd->subsh, info);
            } else if (in_word(i, info->cmd, info)) {
                in_segment(i, info->word, info);
            }
        }
    }
}

int ps_parse(ps_scan *scan, ps_ast *ast) {
    ASSERT(scan && ast);

    if (!ast->allo.alloc) {
        ast->allo = (linc_allo){
            .alloc = arena_alloc,
            .free = arena_free,
            .ctx = arena_create()
        };
    }

    ast->andors.allo = ast->allo;
    scan->ast = ast;
    scan->cur_tok = &scan->toks->data[0];

    if (!scan->toks || !ast || scan->toks->size == 0)
        goto fail;

    for (; scan->i != scan->toks->size; scan_advance(1, scan)) {
        lx_kind kind = scan->cur_tok->kind;

        /* Just parsed a delimeter so need a command */
        if (!scan->cur_andor || !scan->cur_cmd) {
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

        /* Just parsed a redirect so need a target */
        if (scan->queued_redir) {
            if (scan->queued_redir->fd_dup) {
                if (kind != LX_TOK_IO_NUM)
                    goto fail_tok;
            } else if (kind != LX_TOK_WORD) {
                goto fail_tok;
            }
        }

        switch (kind) {
        case LX_TOK_WORD:
            if (scan->queued_redir) {
                copy_word(&scan->queued_redir->target, scan->cur_tok, scan);
                scan->queued_redir = nullptr;
                break;
            }

            if (!scan->cur_cmd && scan->alias)
                if (expand_alias(scan) == -1)
                    goto fail;

            if (!scan->cur_andor)
                add_andor(&ast->andors, scan->andor_op, scan);

            if (!scan->cur_cmd)
                add_cmd(&scan->cur_andor->pline.cmds, scan);

            scan->cur_cmd->words.allo = scan->ast->allo;
            ps_word *word = linc_array_push(&scan->cur_cmd->words);
            *word = (ps_word){ .segments.allo = scan->ast->allo };

            copy_word(word, scan->cur_tok, scan);
            break;
        case LX_TOK_IO_NUM:
            if (scan->queued_redir) {
                scan->queued_redir->fd_target = scan->cur_tok->io_num;
                scan->queued_redir = nullptr;
                break;
            }

            /* If there is no queued redirect theat means the lexer
             * lexed a redirect token after the io-number. */
            ASSERT(!scan_final(scan));

            int io_num = scan->cur_tok->io_num;
            scan_advance(1, scan);

            if (scan->cur_tok->kind == LX_TOK_FD_OUT ||
                scan->cur_tok->kind == LX_TOK_FD_IN) {
                queue_redir(io_num, true, scan);
            } else if (scan->cur_tok->kind == LX_TOK_RDR_OUT ||
                       scan->cur_tok->kind == LX_TOK_RDR_IN ||
                       scan->cur_tok->kind == LX_TOK_APPEND) {
                queue_redir(io_num, false, scan);
            } else {
                ASSERT(false && "unexpected token stream");
            }

            break;
        case LX_TOK_FD_OUT:
        case LX_TOK_FD_IN:
            queue_redir(-1, true, scan);
            break;
        case LX_TOK_RDR_IN:
        case LX_TOK_RDR_OUT:
        case LX_TOK_APPEND:
            queue_redir(-1, false, scan);
            break;
        case LX_TOK_LPAREN:
            if (!scan->cur_andor)
                add_andor(&ast->andors, scan->andor_op, scan);

            if (scan->cur_cmd)
                goto fail_tok;

            add_cmd(&scan->cur_andor->pline.cmds, scan);

            if (parse_subshell(scan) == -1)
                goto fail;
            break;
        case LX_TOK_RPAREN:
            goto fail_tok;
        case LX_TOK_PIPE:
            end_cmd(scan);
            break;
        case LX_TOK_AND_IF:
            end_cmd(scan);
            end_andor(scan);
            scan->andor_op = PS_AND_IF;
            break;
        case LX_TOK_OR_IF:
            end_cmd(scan);
            end_andor(scan);
            scan->andor_op = PS_OR_IF;
            break;
        case LX_TOK_AMPER:
            if (!scan_final(scan) || !scan->cur_andor ||
                    !scan->cur_cmd || scan->queued_redir)
                goto fail_tok;

            end_cmd(scan);
            end_andor(scan);

            ast->bg = true;

            return 0;
        default:
            break;
        }
    }

    if (scan->queued_redir || !scan->cur_andor || !scan->cur_cmd) {
        ERR_EOL();
        goto fail;
    }

    end_cmd(scan);
    end_andor(scan);

    return 0;

fail_tok:
    ERR_TOK(scan->cur_tok);

fail:
    end_cmd(scan);
    end_andor(scan);

    return -1;
}

void ps_free(ps_ast *ast) {
    if (!ast || !ast->allo.ctx || ast->nested)
        return;

    arena_destroy(ast->allo.ctx);

    *ast = (ps_ast){0};
}
