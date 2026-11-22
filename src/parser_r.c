#include "linc/array.h"
#include "map.h"
#include "parser.h"
#include "arena.h"
#include "state.h"
#include "utils.h"

#define ERR_MSG(...) \
    do { \
        if (p_scan->print_err) \
            err_msg("parse error " __VA_ARGS__); \
    } while (false)

#define ERR_TOK(_tok) \
    do { \
        if (p_scan->print_err) { \
            if ((_tok)->kind == LX_TOK_WORD) { \
                err_msg("parse error near %s", tok_word_str(_tok)); \
            } else { \
                err_msg("parse error near '%s'", tok_str((_tok)->kind)); \
            } \
        } \
        *linc_array_push(&p_scan->err_toks) = *_tok; \
    } while (false)

static ps_scan *p_scan;
static da_tok *p_toks;
static lx_tok *p_tok;
static size_t p_i;

static int parse_ast(ps_ast *ast);

static void tok_advance(void) {
    if (p_tok->kind == LX_EOF)
        return;

    ++p_i;
    p_tok = &p_toks->data[p_i];
}

static bool try_get_alias(char **alias, char **value) {
    if (p_tok->kind != LX_TOK_WORD)
        return false;

    if (p_tok->parts.size != 1 || p_tok->parts.data[0].quote != LX_Q_NONE)
        return false;

    void *out;

    *alias = p_tok->parts.data[0].raw;
    int rv = mp_str_lookup(&env.aliases, *alias, &out);
    *value = out;

    return (rv != -1);
}

static int expand_alias(const char *alias, const char *value) {
    da_tok alias_toks;

    int rv = lx_tokenize((char *)value, strlen(value), &alias_toks);
    if (rv != LX_OK) {
        ERR_MSG("%s", lx_errstr(rv));
        return -1;
    }

    if (mp_str_lookup(&env.aliases, value, NULL) != -1) {
        ERR_MSG("on recursive alias expansion");
        lx_free(&alias_toks);
        return -1;
    }

    if (strncmp(alias, value, strlen(value)) == 0)
        return 0;

    if (alias_toks.size == 0)
        return 0;

    lx_shift_toks(p_tok->i, alias_toks.size, alias_toks.data);
    int len = strlen(value) - strlen(alias);

    if (p_tok->kind != LX_EOF)
        lx_shift_toks(len, p_toks->size - (p_i + 1), &p_tok[1]);

    lx_free_tok(p_tok);
    linc_array_delete(p_toks, p_i, 1);

    memcpy(
        linc_array_insert(p_toks, p_i, alias_toks.size),
        alias_toks.data,
        alias_toks.size * sizeof(lx_tok)
    );

    linc_array_free(&alias_toks);
    p_tok = &p_toks->data[p_i];

    return 0;
}

static void copy_word(ps_word *dst, lx_tok *src) {
    *dst = (ps_word){ .i = src->i, .n = src->n, .segments.allo = p_scan->allo };

    for (size_t i = 0; i < src->parts.size; ++i) {
        lx_part *src_part = &src->parts.data[i];
        size_t n = strlen(src_part->raw) + 1;

        ps_segment *dst_segment = linc_array_push(&dst->segments);

        *dst_segment = (ps_segment){
            .i = src_part->i,
            .n = src_part->n,
            .raw = p_scan->allo.alloc(n, p_scan->allo.ctx),
        };

        memcpy(dst_segment->raw, src_part->raw, n);

        switch (src_part->quote) {
            case LX_Q_SINGLE: dst_segment->quote = PS_Q_SINGLE; break;
            case LX_Q_DOUBLE: dst_segment->quote = PS_Q_DOUBLE; break;
            case LX_Q_NONE:   dst_segment->quote = PS_Q_NONE;   break;
        }
    }
}

static int parse_word(ps_word *word) {
    *word = (ps_word){ .segments.allo = p_scan->allo };

    if (p_tok->kind != LX_TOK_WORD) {
        ERR_TOK(p_tok);
        return -1;
    }

    copy_word(word, p_tok);
    tok_advance();

    return 0;
}

static int parse_redir(ps_redir *redir) {
    if (p_tok->kind == LX_TOK_IO_NUM) {
        redir->io_num = p_tok->io_num;
        tok_advance();
    } else {
        switch (p_tok->kind) {
        case LX_TOK_RDR_IN:  redir->io_num = 0; break;
        case LX_TOK_RDR_OUT: redir->io_num = 1; break;
        case LX_TOK_APPEND:  redir->io_num = 1; break;
        case LX_TOK_FD_OUT:  redir->io_num = 1; break;
        case LX_TOK_FD_IN:   redir->io_num = 0; break;
        default:
            ERR_TOK(p_tok);
            return -1;
        }
    }

    if (p_tok->kind == LX_TOK_APPEND) {
        redir->append = true;
    } if (p_tok->kind == LX_TOK_FD_IN || p_tok->kind == LX_TOK_RDR_IN) {
        redir->rdr_in = true;
    } if (p_tok->kind == LX_TOK_FD_IN || p_tok->kind == LX_TOK_FD_OUT) {
        redir->fd_dup = true;
    } else if (!lx_kind_is_redir(p_tok->kind)) {
        ERR_TOK(p_tok);
        return -1;
    }

    tok_advance();

    if (!redir->fd_dup) {
        if (parse_word(&redir->target) == -1)
            return -1;
    } else {
        if (p_tok->kind != LX_TOK_IO_NUM) {
            ERR_TOK(p_tok);
            return -1;
        }

        redir->fd_target = p_tok->io_num;
        tok_advance();
    }

    return 0;
}

static int parse_subsh(ps_ast *ast) {
    if (p_tok->kind != LX_TOK_LPAREN) {
        ERR_TOK(p_tok);
        return -1;
    }

    tok_advance();

    if (parse_ast(ast) == -1)
        return -1;

    if (p_tok->kind != LX_TOK_RPAREN) {
        ERR_TOK(p_tok);
        return -1;
    }

    tok_advance();

    return 0;
}

static int parse_cmd_element(ps_cmd *cmd) {
    if (p_tok->kind == LX_TOK_WORD) {
        ps_word *word = linc_array_push(&cmd->words);
        if (parse_word(word) == -1)
            return -1;
    } else if (lx_kind_is_redir(p_tok->kind) || p_tok->kind == LX_TOK_IO_NUM) {
        ps_redir *redir = linc_array_push(&cmd->redirs);
        *redir = (ps_redir){0};

        if (parse_redir(redir) == -1)
            return -1;
    } else if (p_tok->kind == LX_TOK_LPAREN) {
        cmd->kind = PS_CMD_SUBSH;

        if (parse_subsh(&cmd->subsh) == -1)
            return -1;
    } else {
        ERR_TOK(p_tok);
        return -1;
    }

    return 0;
}

static int parse_cmd(ps_cmd *cmd) {
    char *alias, *value;
    if (p_scan->alias && try_get_alias(&alias, &value)) {
        if (expand_alias(alias, value) == -1)
            return -1;
    }

    *cmd = (ps_cmd){
        .words.allo = p_scan->allo,
        .redirs.allo = p_scan->allo,
        .i = p_tok->i
    };

    if (parse_cmd_element(cmd) == -1)
        return -1;

    while (TOK_IN(p_tok->kind, LX_TOK_WORD, LX_TOK_IO_NUM) ||
           lx_kind_is_redir(p_tok->kind)) {

        if (cmd->kind == PS_CMD_SUBSH && p_tok->kind == LX_TOK_WORD) {
            ERR_TOK(p_tok);
            return -1;
        }

        if (parse_cmd_element(cmd) == -1)
            return -1;
    }

    cmd->n = (p_tok[-1].i + p_tok[-1].n) - cmd->i;
    return 0;
}

static int parse_pline(ps_andor *andor) {
    andor->pline = (ps_pline){ .cmds.allo = p_scan->allo };

    ps_cmd *cmd = linc_array_push(&andor->pline.cmds);
    if (parse_cmd(cmd) == -1)
        return -1;

    andor->i = cmd->i;
    andor->pline.i = cmd->i;

    while (p_tok->kind == LX_TOK_PIPE) {
        tok_advance();

        cmd = linc_array_push(&andor->pline.cmds);
        if (parse_cmd(cmd) == -1)
            return -1;
    }

    andor->n = (p_tok[-1].i + p_tok[-1].n) - andor->i;
    andor->pline.n = (p_tok[-1].i + p_tok[-1].n) - andor->pline.i;
    return 0;
}

static int parse_chain(ps_chain *chain) {
    *chain = (ps_chain){ .andors.allo = p_scan->allo };

    ps_andor *andor = linc_array_push(&chain->andors);
    *andor = (ps_andor){ .op = PS_NO_IF };
    if (parse_pline(andor) == -1)
        return -1;

    chain->i = andor->i;

    while (true) {
        if (p_tok->kind == LX_TOK_AND_IF) {
            tok_advance();
            andor = linc_array_push(&chain->andors);
            *andor = (ps_andor){ .op = PS_AND_IF, };
        } else if (p_tok->kind == LX_TOK_OR_IF) {
            tok_advance();
            andor = linc_array_push(&chain->andors);
            *andor = (ps_andor){ .op = PS_OR_IF };
        } else {
            break;
        }

        if (parse_pline(andor) == -1)
            return -1;
    }

    if (p_tok->kind == LX_TOK_AMPER) {
        chain->bg = true;
        tok_advance();
    }

    chain->n = (p_tok[-1].i + p_tok[-1].n) - chain->i;

    return 0;
}

static int parse_ast(ps_ast *ast) {
    while (true) {
        ps_chain *chain = linc_array_push(&ast->chains);

        if (parse_chain(chain) == -1)
            return -1;

        if (p_tok->kind == LX_TOK_SEMI) {
            tok_advance();
        } else {
            break;
        }
    }

    return 0;
}

void parse_r_free(ps_ast *ast) {
    ASSERT(ast && ast->allo.ctx);
    arena_destroy(ast->allo.ctx);
    *ast = (ps_ast){0};
}

int parse_r(ps_scan *scan, ps_ast *ast) {
    p_scan = scan;

    if (!p_scan->allo.alloc) {
        p_scan->allo = (linc_allo){
            .alloc = arena_alloc,
            .free = arena_free,
            .ctx = arena_create()
        };
    }

    p_scan->err_toks.allo = p_scan->allo;

    *ast = (ps_ast){
        .allo = p_scan->allo,
        .chains.allo = p_scan->allo
    };

    da_tok toks = {0};

    for (size_t i = 0; i < p_scan->toks->size; ++i) {
        lx_tok *dest = linc_array_push(&toks);
        lx_tok *src = &p_scan->toks->data[i];

        if (src->kind == LX_TOK_WORD) {
            lx_tok_copy(dest, src);
        } else {
            *dest = *src;
        }
    }

    linc_array_push(&toks)->kind = LX_EOF;

    p_toks = &toks;
    p_tok = p_toks->data;
    p_i = 0;

    if (parse_ast(ast) == -1)
        goto fail;

    if (p_tok->kind != LX_EOF) {
        ERR_TOK(p_tok);
        goto fail;
    }

    lx_free(&toks);
    return 0;

fail:
    lx_free(&toks);
    return -1;
}
