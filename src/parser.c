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
        lx_tkcpy(linc_array_push(&p_scan->err_toks), _tok, &p_scan->allo); \
    } while (false)

static ps_scan *p_scan;
static da_tok *p_toks;
static lx_tok *p_tok;
static size_t p_i;

static int parse_ast(ps_ast *ast, lx_kind term);
static int parse_chain(ps_chain *chain);

static void tok_advance(void) {
    if (p_tok->kind == LX_EOF)
        return;

    ++p_i;
    p_tok = &p_toks->data[p_i];
}

static int tok_expect(lx_kind kind) {
    if (p_tok->kind != kind) {
        ERR_TOK(p_tok);
        return -1;
    }

    tok_advance();
    return 0;
}

static int get_cur_i(void) {
    if (p_i == 0)
        return 0;

    return p_tok[-1].i +p_tok[-1].n;
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
    da_tok alias_toks = { .allo = p_scan->allo };

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

    if (strncmp(alias, value, strlen(value)) == 0) {
        linc_array_delete(p_toks, p_i, 1);
        lx_free(&alias_toks);
        p_tok = &p_toks->data[p_i];
        return 0;
    }

    if (alias_toks.size == 0)
        return 0;

    lx_shift_toks(p_tok->i, alias_toks.size, alias_toks.data);
    int len = strlen(value) - strlen(alias);

    if (p_tok->kind != LX_EOF)
        lx_shift_toks(len, p_toks->size - (p_i + 1), &p_tok[1]);

    linc_array_delete(p_toks, p_i, 1);

    lx_tok *start = linc_array_insert(p_toks, p_i, alias_toks.size);
    for (size_t i = 0; i < alias_toks.size; ++i)
        lx_tkcpy(&start[i], &alias_toks.data[i], &p_scan->allo);

    lx_free(&alias_toks);
    p_tok = &p_toks->data[p_i];

    return 0;
}

static void copy_word(ps_word *dst, lx_tok *src) {
    *dst = (ps_word){
        .i = src->i, .n = src->n,
        .segments.allo = p_scan->allo,
    };

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
    *redir = (ps_redir){0};

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
    } else if (!lx_is_redir(p_tok->kind)) {
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
    if (tok_expect(LX_TOK_LPAREN) == -1)
        return -1;

    if (parse_ast(ast, LX_TOK_RPAREN) == -1)
        return -1;

    if (tok_expect(LX_TOK_RPAREN) == -1)
        return -1;

    return 0;
}

static int parse_if(ps_cmd *cmd) {
    if (p_tok->kind != LX_TOK_WORD || p_tok->spec != LX_SP_IF) {
        ERR_TOK(p_tok);
        return -1;
    }

    cmd->kind = PS_CMD_IF;

    cmd->if_st = (ps_if){
        .cond = (ps_ast){
            .allo = p_scan->allo,
            .chains.allo = p_scan->allo
        },
        .body = (ps_ast){
            .allo = p_scan->allo,
            .chains.allo = p_scan->allo
        }
    };

    tok_advance();

    if (tok_expect(LX_TOK_LPAREN) == -1)
        return -1;

    if (parse_ast(&cmd->if_st.cond, LX_TOK_RPAREN) == -1)
        return -1;

    if (tok_expect(LX_TOK_RPAREN) == -1)
        return -1;

    if (tok_expect(LX_TOK_LBRACE) == -1)
        return -1;

    if (parse_ast(&cmd->if_st.body, LX_TOK_RBRACE) == -1)
        return -1;

    if (tok_expect(LX_TOK_RBRACE) == -1)
        return -1;

    return 0;
}

static int parse_while(ps_cmd *cmd) {
    if (p_tok->kind != LX_TOK_WORD || p_tok->spec != LX_SP_WHILE) {
        ERR_TOK(p_tok);
        return -1;
    }

    cmd->kind = PS_CMD_WHILE;

    cmd->while_st = (ps_while){
        .cond = (ps_ast){
            .allo = p_scan->allo,
            .chains.allo = p_scan->allo
        },
        .body = (ps_ast){
            .allo = p_scan->allo,
            .chains.allo = p_scan->allo
        }
    };

    tok_advance();

    if (tok_expect(LX_TOK_LPAREN) == -1)
        return -1;

    if (parse_ast(&cmd->while_st.cond, LX_TOK_RPAREN) == -1)
        return -1;

    if (tok_expect(LX_TOK_RPAREN) == -1)
        return -1;

    if (tok_expect(LX_TOK_LBRACE) == -1)
        return -1;

    if (parse_ast(&cmd->while_st.body, LX_TOK_RBRACE) == -1)
        return -1;

    if (tok_expect(LX_TOK_RBRACE) == -1)
        return -1;

    return 0;
}

static int parse_for(ps_cmd *cmd) {
    if (p_tok->kind != LX_TOK_WORD || p_tok->spec != LX_SP_FOR) {
        ERR_TOK(p_tok);
        return -1;
    }

    cmd->kind = PS_CMD_FOR;
    cmd->for_st = (ps_for){
        .ind = { .segments.allo = p_scan->allo, },
        .vals = { .allo = p_scan->allo, },
        .body = { .allo = p_scan->allo, .chains.allo = p_scan->allo, },
    };

    tok_advance();

    if (parse_word(&cmd->for_st.ind) == -1)
        return -1;

    if (cmd->for_st.ind.segments.size != 1 ||
        cmd->for_st.ind.segments.data[0].quote != PS_Q_NONE) {
        ERR_TOK(&p_tok[-1]);
        return -1;
    }

    if (tok_expect(LX_TOK_SEMI) == -1)
        return -1;

    while (p_tok->kind == LX_TOK_WORD) {
        ps_word *word = linc_array_push(&cmd->for_st.vals);
        if (parse_word(word) == -1)
            return -1;
    }

    if (tok_expect(LX_TOK_LBRACE) == -1)
        return -1;

    if (parse_ast(&cmd->for_st.body, LX_TOK_RBRACE) == -1)
        return -1;

    if (tok_expect(LX_TOK_RBRACE) == -1)
        return -1;

    return 0;
}

static int parse_cmd_element(ps_cmd *cmd, bool first) {
    if (p_tok->kind == LX_TOK_WORD) {
        if (!first || p_tok->spec == LX_SP_NONE) {
            ps_word *word = linc_array_push(&cmd->simple.words);
            if (parse_word(word) == -1)
                return -1;
        } else {
            if (p_tok->spec == LX_SP_IF) {
                if (parse_if(cmd) == -1)
                    return -1;
            } else if (p_tok->spec == LX_SP_WHILE) {
                if (parse_while(cmd) == -1)
                    return -1;
            } else if (p_tok->spec == LX_SP_FOR) {
                if (parse_for(cmd) == -1)
                    return -1;
            } else {
                fatal("unexpected command type");
            }
        }
    } else if (lx_is_redir(p_tok->kind) || p_tok->kind == LX_TOK_IO_NUM) {
        ps_redir *redir = linc_array_push(&cmd->redirs);
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
    int status = -1;

    *cmd = (ps_cmd){ .i = p_tok->i };

    char *alias, *value;
    if (p_scan->alias && try_get_alias(&alias, &value)) {
        if (expand_alias(alias, value) == -1)
            goto done;
    }

    *cmd = (ps_cmd){
        .simple.words.allo = p_scan->allo,
        .redirs.allo = p_scan->allo,
        .i = p_tok->i
    };

    if (parse_cmd_element(cmd, true) == -1)
        goto done;

    while (TOK_IN(p_tok->kind, LX_TOK_WORD, LX_TOK_IO_NUM) ||
           lx_is_redir(p_tok->kind)) {

        if (cmd->kind != PS_CMD_SIMPLE && p_tok->kind == LX_TOK_WORD) {
            ERR_TOK(p_tok);
            goto done;
        }

        if (parse_cmd_element(cmd, false) == -1)
            goto done;
    }

    status = 0;

done:
    cmd->n = get_cur_i() - cmd->i;
    return status;
}

static int parse_pline(ps_andor *andor) {
    int status = -1;

    andor->pline = (ps_pline){ .cmds.allo = p_scan->allo };

    ps_cmd *cmd = linc_array_push(&andor->pline.cmds);
    if (parse_cmd(cmd) == -1)
        goto done;

    andor->i = cmd->i;
    andor->pline.i = cmd->i;

    while (p_tok->kind == LX_TOK_PIPE) {
        tok_advance();

        cmd = linc_array_push(&andor->pline.cmds);
        if (parse_cmd(cmd) == -1)
            goto done;
    }

    status = 0;

done:
    andor->n = get_cur_i() - andor->i;
    andor->pline.n = get_cur_i() - andor->pline.i;
    return status;
}

static int parse_chain(ps_chain *chain) {
    int status = -1;

    *chain = (ps_chain){ .andors.allo = p_scan->allo };

    ps_andor *andor = linc_array_push(&chain->andors);
    *andor = (ps_andor){ .op = PS_NO_IF };
    if (parse_pline(andor) == -1)
        goto done;

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
            goto done;
    }

    if (p_tok->kind == LX_TOK_AMPER) {
        chain->bg = true;
        tok_advance();
    }

    status = 0;

done:
    chain->n = get_cur_i() - chain->i;
    return status;
}

static int parse_ast(ps_ast *ast, lx_kind term) {
    while (true) {
        ps_chain *chain = linc_array_push(&ast->chains);

        if (parse_chain(chain) == -1)
            return -1;

        if (p_tok->kind == LX_TOK_SEMI) {
            tok_advance();

            if (p_tok->kind == term)
                break;
        } else {
            break;
        }
    }

    return 0;
}

void ps_free(ps_ast *ast) {
    ASSERT(ast && ast->allo.ctx);
    arena_destroy(ast->allo.ctx);
    *ast = (ps_ast){0};
}

int ps_parse(ps_scan *scan, ps_ast *ast) {
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

    da_tok toks = { .allo = p_scan->allo };

    for (size_t i = 0; i < p_scan->toks->size; ++i) {
        lx_tok *dest = linc_array_push(&toks);
        lx_tkcpy(dest, &p_scan->toks->data[i], &p_scan->allo);
    }

    *linc_array_push(&toks) = (lx_tok){ .kind = LX_EOF };

    p_toks = &toks;
    p_tok = p_toks->data;
    p_i = 0;

    if (parse_ast(ast, LX_EOF) == -1)
        return -1;

    if (p_tok->kind != LX_EOF) {
        ERR_TOK(p_tok);
        return -1;
    }

    return 0;
}

static bool in_chain(int ind, ps_ast *ast, ps_info *info) {
    for (size_t i = 0; i < ast->chains.size; ++i) {
        ps_chain *chain = &ast->chains.data[i];

        if (ind < chain->i || ind > chain->i + chain->n)
            continue;

        info->kinds |= PS_INFO_CHAIN;
        info->chain = chain;

        return true;
    }

    return false;
}

static bool in_andor(int ind, ps_chain *chain, ps_info *info) {
    for (size_t i = 0; i < chain->andors.size; ++i) {
        ps_andor *andor = &chain->andors.data[i];

        if (ind < andor->i || ind > andor->i + andor->n)
            continue;

        if (andor->op == PS_AND_IF) {
            info->kinds |= PS_INFO_AND_IF;
        } else if (andor->op == PS_OR_IF) {
            info->kinds |= PS_INFO_OR_IF;
        } else {
            info->kinds |= PS_INFO_NO_IF;
        }

        info->andor = andor;

        return true;
    }

    return false;
}

static bool in_cmd(int ind, ps_andor *andor, ps_info *info) {
    for (size_t i = 0; i < andor->pline.cmds.size; ++i) {
        ps_cmd *cmd = &andor->pline.cmds.data[i];

        if (ind < cmd->i || ind > cmd->i + cmd->n)
            continue;

        info->kinds |= PS_INFO_CMD;
        info->cmd = cmd;

        return true;
    }

    return false;
}

static bool in_segment(int ind, ps_word *word, ps_info *info);

static bool in_word(int ind, ps_word *word, ps_info *info) {
    if (ind < word->i || ind > word->i + word->n)
        return false;

    info->word = word;

    in_segment(ind, word, info);

    return true;
}

static bool in_redir(int ind, ps_cmd *cmd, ps_info *info) {
    for (size_t i = 0; i < cmd->redirs.size; ++i) {
        ps_redir *redir = &cmd->redirs.data[i];
        ps_word *target = &cmd->redirs.data[i].target;

        if (target->segments.size == 0)
            continue;

        if (ind < target->i || ind > target->i + target->n)
            continue;

        info->kinds |= PS_INFO_REDIR;
        info->redir = redir;
        info->word = target;

        return true;
    }

    return false;
}

static bool in_segment(int ind, ps_word *word, ps_info *info) {
    for (size_t i = 0; i < word->segments.size; ++i) {
        ps_segment *seg = &word->segments.data[i];

        if (ind < seg->i || ind > seg->i + seg->n)
            continue;

        info->kinds |= PS_INFO_SEG;
        info->seg = seg;

        return true;
    }

    return false;
}

static void in_for(int ind, ps_for *for_st, ps_info *info) {
    if (in_word(ind, &for_st->ind, info))
        return;

    for (size_t i = 0; i < for_st->vals.size; ++i)
        if (in_word(ind, &for_st->vals.data[i], info))
            return;

    ps_get_info(ind, &for_st->body, info);
}

static void in_simple(int ind, ps_simple *simple, ps_info *info) {
    for (size_t i = 0; i < simple->words.size; ++i) {
        if (in_word(ind, &simple->words.data[i], info)) {
            if (i == 0) {
                info->kinds |= PS_INFO_NAME;
            } else {
                info->kinds |= PS_INFO_ARG;
            }

            return;
        }
    }
}

void ps_get_info(int ind, ps_ast *ast, ps_info *info) {
    ASSERT(ast && ind >= 0 && info);

    info->kinds = 0;

    if (ast->chains.size == 0) {
        info->kinds |= PS_INFO_EMPTY;
        return;
    }

    if (in_chain(ind, ast, info)) {
        if (in_andor(ind, info->chain, info)) {
            if (in_cmd(ind, info->andor, info)) {
                if (in_redir(ind, info->cmd, info)) {
                    in_segment(ind, &info->redir->target, info);
                } else if (info->cmd->kind == PS_CMD_SUBSH) {
                    ps_get_info(ind, &info->cmd->subsh, info);
                } else if (info->cmd->kind == PS_CMD_IF) {
                    ps_get_info(ind, &info->cmd->if_st.cond, info);

                    if (info->kinds == 0)
                        ps_get_info(ind, &info->cmd->if_st.body, info);
                } else if (info->cmd->kind == PS_CMD_WHILE) {
                    ps_get_info(ind, &info->cmd->while_st.cond, info);

                    if (info->kinds == 0)
                        ps_get_info(ind, &info->cmd->while_st.body, info);
                } else if (info->cmd->kind == PS_CMD_FOR) {
                    in_for(ind, &info->cmd->for_st, info);
                } else if (info->cmd->kind == PS_CMD_SIMPLE) {
                    in_simple(ind, &info->cmd->simple, info);
                } else {
                    fatal("unexpected command type");
                }
            }
        }
    }
}
