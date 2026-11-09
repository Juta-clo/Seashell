#include "unity_fixture.h"
#include "parser.h"
#include "lexer.h"
#include "map.h"
#include "state.h"

TEST_GROUP(parser);

/************ Shared utils ************/

#define IO(num) num
#define APPEND_ON  1
#define APPEND_OFF 0

#define RDR_OUT false
#define RDR_IN true

#define I(_i) _i
#define N(_n) _n

#define BG_OFF 0
#define BG_ON  1

#define NO_REDIRS ((da_redir){0})

#define EXP_DA(DA_T, ELEM_T, ...) \
    ((DA_T){ \
        .data = (ELEM_T[]) { __VA_ARGS__ }, \
        .size = sizeof((ELEM_T[]) { __VA_ARGS__ }) / sizeof(ELEM_T) \
     })

#define SEGMENTS(...) EXP_DA(da_segment, ps_segment, __VA_ARGS__)
#define WORDS(...)    EXP_DA(da_word, ps_word, __VA_ARGS__)
#define REDIRS(...)   EXP_DA(da_redir, ps_redir, __VA_ARGS__)
#define CMDS(...)     EXP_DA(da_cmd, ps_cmd, __VA_ARGS__)
#define ANDORS(...)   EXP_DA(da_andor, ps_andor, __VA_ARGS__)

#define SEGMENT(_i, _n, _raw, _quote) \
    ((ps_segment){ .i = _i, .n = _n, .raw = (_raw), .quote = (_quote) })

#define WORD(_i, _n, _segments) \
    ((ps_word){ .i = _i, .n = _n, .segments = (_segments) })

#define WORD_SIMPLE(_i, _n, _raw) \
    WORD(_i, _n, SEGMENTS(SEGMENT(_i, _n, _raw, PS_Q_NONE)))

#define WORD_DBQ(_i, _n, _raw) \
    WORD(_i, _n, SEGMENTS(SEGMENT(_i + 1, _n - 2, _raw, PS_Q_DOUBLE)))

#define REDIR(_target, _io_num, _append, _rdr_in) \
    ((ps_redir){ \
        .target = _target, \
        .io_num = _io_num, \
        .append = _append, \
        .rdr_in = _rdr_in, \
        .fd_dup = false \
    })

#define REDIR_FD(_fd_target, _io_num, _append, _rdr_in) \
    ((ps_redir){ \
        .io_num = _io_num, \
        .append = _append, \
        .rdr_in = _rdr_in, \
        .fd_dup = true, \
        .fd_target = _fd_target \
    })

#define CMD_R(_i, _n, _words, _redirs) \
    ((ps_cmd){ \
        .i = _i, \
        .n = _n, \
        .kind = PS_CMD_SIMPLE, \
        .words = _words, \
        .redirs = _redirs \
     })

#define CMD(_i, _n, ...) \
    CMD_R(_i, _n, WORDS(__VA_ARGS__), NO_REDIRS)

#define CMD_SIMPLE(_i, _n, _word) \
    CMD_R(_i, _n, WORDS(WORD_SIMPLE(_i, _n, _word)), NO_REDIRS)

#define SUBSH_R(_i, _n, _ast, ...) \
    ((ps_cmd){ \
        .i = _i, \
        .n = _n, \
        .kind = PS_CMD_SUBSH, \
        .subsh = (_ast), \
        .redirs = (__VA_ARGS__), \
    })

#define SUBSH(_i, _n, _ast) \
    SUBSH_R(_i, _n, _ast, NO_REDIRS)

#define PIPELINE(...) \
    ((ps_pline){ .cmds = CMDS(__VA_ARGS__) })

#define NOIF(_i, _n, ...) \
    ((ps_andor){ \
        .i = _i, .n = _n, .pline = PIPELINE(__VA_ARGS__), .op = PS_NO_IF \
    })

#define ANDIF(_i, _n, ...) \
    ((ps_andor){ \
        .i = _i, .n = _n, .pline = PIPELINE(__VA_ARGS__), .op = PS_AND_IF \
    })

#define ORIF(_i, _n, ...) \
    ((ps_andor){ \
        .i = _i, .n = _n, .pline = PIPELINE(__VA_ARGS__), .op = PS_OR_IF \
    })


#define AST(...) \
    ((ps_ast){ .andors = ANDORS(__VA_ARGS__), .bg = BG_OFF })

#define AST_BG(...) \
    ((ps_ast){ .andors = ANDORS(__VA_ARGS__), .bg = BG_ON })

#define AST_SIMPLE(...) \
    AST(NOIF(CMD(__VA_ARGS__)))

#define add_alias(_alias, _value) \
    do { \
        char *add_alias_value = strdup(_value); \
        if (!add_alias_value) \
            TEST_FAIL_MESSAGE("strdup() failed"); \
        \
        if (mp_str_add(&env.aliases, _alias, add_alias_value) == -1) \
            TEST_FAIL_MESSAGE("mp_str_add() failed"); \
    } while (false)

ps_ast expected_ast;
da_tok expected_tokens;

static void validate_ast(const ps_ast *exp, const ps_ast *act);

static void validate_segment(const ps_segment *exp, const ps_segment *act) {
    TEST_ASSERT_EQUAL_INT(exp->i, act->i);
    TEST_ASSERT_EQUAL_INT(exp->n, act->n);
    TEST_ASSERT_EQUAL_STRING(exp->raw, act->raw);
    TEST_ASSERT_EQUAL(exp->quote, act->quote);
}

static void validate_segments(const da_segment *exp, const da_segment *act) {
    TEST_ASSERT_EQUAL_size_t(exp->size, act->size);

    for (size_t i = 0; i < act->size; ++i) {
        const ps_segment *exp_segment = &exp->data[i];
        const ps_segment *segment = &act->data[i];
        validate_segment(exp_segment, segment);
    }
}

static void validate_word(const ps_word *exp, const ps_word *act) {
    TEST_ASSERT_EQUAL_INT(exp->i, act->i);
    TEST_ASSERT_EQUAL_INT(exp->n, act->n);

    validate_segments(&exp->segments, &act->segments);
}

static void validate_redir(const ps_redir *exp, const ps_redir *act) {
    TEST_ASSERT_EQUAL_INT(exp->io_num, act->io_num);
    TEST_ASSERT_EQUAL_INT(exp->append, act->append);
    TEST_ASSERT_EQUAL_INT(exp->rdr_in, act->rdr_in);
    TEST_ASSERT_EQUAL_INT(exp->fd_dup, act->fd_dup);

    if (!exp->fd_dup) {
        validate_word(&exp->target, &act->target);
    } else {
        TEST_ASSERT_EQUAL_INT(exp->fd_target, act->fd_target);
    }
}

static void validate_redirs(const da_redir *exp, const da_redir *act) {
    TEST_ASSERT_EQUAL_size_t(exp->size, act->size);

    for (size_t i = 0; i < act->size; ++i) {
        const ps_redir *exp_redir = &exp->data[i];
        const ps_redir *redir = &act->data[i];
        validate_redir(exp_redir, redir);
    }
}

static void validate_words(const da_word *exp, const da_word *act) {
    TEST_ASSERT_EQUAL_size_t(exp->size, act->size);

    for (size_t i = 0; i < act->size; ++i) {
        const ps_word *exp_tok = &exp->data[i];
        const ps_word *word = &act->data[i];
        validate_word(exp_tok, word);
    }
}

static void validate_cmd(const ps_cmd *exp, const ps_cmd *act) {
    TEST_ASSERT_EQUAL_INT(exp->i, act->i);
    TEST_ASSERT_EQUAL_INT(exp->n, act->n);

    if (exp->kind == PS_CMD_SIMPLE) {
        TEST_ASSERT_EQUAL(PS_CMD_SIMPLE, act->kind);
        validate_words(&exp->words, &act->words);
        validate_redirs(&exp->redirs, &act->redirs);
    } else {
        TEST_ASSERT_EQUAL(PS_CMD_SUBSH, act->kind);
        validate_ast(&exp->subsh, &act->subsh);
        validate_redirs(&exp->redirs, &act->redirs);
    }
}

static void validate_cmds(const da_cmd *exp, const da_cmd *act) {
    TEST_ASSERT_EQUAL_size_t(exp->size, act->size);

    for (size_t i = 0; i < act->size; ++i) {
        const ps_cmd *exp_cmd = &exp->data[i];
        const ps_cmd *cmd = &act->data[i];
        validate_cmd(exp_cmd, cmd);
    }
}

static void validate_pipeline(const ps_pline *exp, const ps_pline *act) {
    validate_cmds(&exp->cmds, &act->cmds);
}

static void validate_andor(const ps_andor *exp, const ps_andor *act) {
    TEST_ASSERT_EQUAL_INT(exp->i, act->i);
    TEST_ASSERT_EQUAL_INT(exp->n, act->n);

    TEST_ASSERT_EQUAL(exp->op, act->op);
    validate_pipeline(&exp->pline, &act->pline);
}

static void validate_andors(const da_andor *exp, const da_andor *act) {
    TEST_ASSERT_EQUAL_size_t(exp->size, act->size);

    for (size_t i = 0; i < act->size; ++i) {
        const ps_andor *exp_andor = &exp->data[i];
        const ps_andor *andor = &act->data[i];
        validate_andor(exp_andor, andor);
    }
}

static void validate_ast(const ps_ast *exp, const ps_ast *act) {
    TEST_ASSERT_EQUAL_INT(exp->bg, act->bg);
    validate_andors(&exp->andors, &act->andors);
}

static void validate(const char *shell_cmd, const ps_ast *exp) {
    if (lx_tokenize(shell_cmd, &expected_tokens) != LX_OK)
        TEST_FAIL_MESSAGE("lx_tokenize() failed");

    ps_scan scan = {
        .toks = &expected_tokens,
        .print_err = true,
        .alias = false
    };

    if (ps_parse(&scan, &expected_ast) == -1)
        TEST_FAIL_MESSAGE("ps_parse() failed");

    validate_ast(exp, &expected_ast);
}

static void validate_alias(const char *shell_cmd, const ps_ast *exp) {
    if (lx_tokenize(shell_cmd, &expected_tokens) != LX_OK)
        TEST_FAIL_MESSAGE("lx_tokenize() failed");

    ps_scan scan = {
        .toks = &expected_tokens,
        .print_err = true,
        .alias = true
    };

    if (ps_parse(&scan, &expected_ast) == -1)
        TEST_FAIL_MESSAGE("ps_parse() failed");

    validate_ast(exp, &expected_ast);
}

static void validate_fail(const char *shell_cmd) {
    if (lx_tokenize(shell_cmd, &expected_tokens) == -1)
        TEST_FAIL_MESSAGE("lx_tokenize() failed");

    ps_scan scan = {
        .toks = &expected_tokens,
        .print_err = false,
        .alias = false
    };

    if (ps_parse(&scan, &expected_ast) != -1)
        TEST_FAIL_MESSAGE("ps_parse() didn't fail");

    lx_free(&expected_tokens);
    expected_tokens = (da_tok){0};
}

/************ Fixture ************/

TEST_SETUP(parser) {
    expected_ast = (ps_ast){0};
    expected_tokens = (da_tok){0};
    mp_init(&env.aliases, 4096);
}

TEST_TEAR_DOWN(parser) {
    ps_free(&expected_ast);
    lx_free(&expected_tokens);
    mp_free(&env.aliases);
}

/************ Tests ************/

TEST(parser, general_fail_rejections) {
    validate_fail("a & b");
    validate_fail("echo hi >");
    validate_fail("cmd |");
    validate_fail("cmd &&");
    validate_fail("cmd ||");
    validate_fail("echo > |");
}

TEST(parser, bg_is_final_token_should_pass) {
    ps_ast exp = AST_BG(
        NOIF(I(0), N(1),
            CMD_SIMPLE(I(0), N(1), "a")
        )
    );

    validate("a &", &exp);
}

TEST(parser, cmd_one_word) {
    ps_ast exp = AST(NOIF(I(0), N(1),
        CMD_SIMPLE(I(0), N(1), "a")
    ));

    validate("a", &exp);
}

TEST(parser, cmd_two_word) {
    ps_ast exp = AST(NOIF(I(0), N(3),
        CMD(I(0), N(3),
            WORD_SIMPLE(I(0), N(1), "a"),
            WORD_SIMPLE(I(2), N(1), "b")
        )
    ));

    validate("a b", &exp);
}

TEST(parser, cmd_many_word) {
    ps_ast exp = AST(NOIF(I(0), N(27),
        CMD(I(0), N(27),
            WORD_SIMPLE(I(0), N(7), "command"),
            WORD_SIMPLE(I(8), N(4), "arg1"),
            WORD_SIMPLE(I(13), N(4), "arg2"),
            WORD_SIMPLE(I(18), N(4), "arg3"),
            WORD_SIMPLE(I(23), N(4), "arg4"),
        )
    ));

    validate("command arg1 arg2 arg3 arg4", &exp);
}

TEST(parser, two_cmd_pipeline) {
    ps_ast exp = AST(
        NOIF(I(0), N(5),
            CMD_SIMPLE(I(0), N(1), "a"),
            CMD_SIMPLE(I(4), N(1), "b")
        )
    );

    validate("a | b", &exp);
}

TEST(parser, three_cmd_pipeline) {
    ps_ast exp = AST(
        NOIF(I(0), N(9),
            CMD_SIMPLE(I(0), N(1), "a"),
            CMD_SIMPLE(I(4), N(1), "b"),
            CMD_SIMPLE(I(8), N(1), "c"),
        )
    );

    validate("a | b | c", &exp);
}

TEST(parser, two_pipeline_and_if) {
    ps_ast exp = AST(
        NOIF(I(0), N(1), CMD_SIMPLE(I(0), N(1), "a")),
        ANDIF(I(5), N(1), CMD_SIMPLE(I(5), N(1), "b"))
    );

    validate("a && b", &exp);
}

TEST(parser, two_pipeline_or_if) {
    ps_ast exp = AST(
        NOIF(I(0), N(1), CMD_SIMPLE(I(0), N(1), "a")),
        ORIF(I(5), N(1), CMD_SIMPLE(I(5), N(1), "b"))
    );

    validate("a || b", &exp);
}

TEST(parser, container_control_flow) {
    ps_ast exp = AST(
        NOIF(I(0), N(23),
            CMD(I(0), N(14),
                WORD_SIMPLE(I(0), N(2), "wc"),
                WORD_SIMPLE(I(3), N(2), "-l"),
                WORD_SIMPLE(I(6), N(8), "file.txt")
            ),
            CMD(I(17), N(6), WORD_SIMPLE(I(17), N(4), "grep"),
                WORD_SIMPLE(I(22), N(1), "3"))
        ),
        ANDIF(I(27), N(41),
            CMD(I(27), N(41),
                WORD_SIMPLE(I(27), N(4), "echo"),
                WORD(I(32), N(36),
                    SEGMENTS(
                        SEGMENT(I(33), N(34),
                            "there are three lines in file.txt!", PS_Q_DOUBLE)
                    )
                )
            )
        )
    );

    validate("wc -l file.txt | grep 3 && echo \"there are three "
            "lines in file.txt!\"", &exp);
}

TEST(parser, and_if_with_redirects) {
    ps_ast exp = AST(
        NOIF(I(0), N(1), CMD_SIMPLE(I(0), N(1), "a")),
        ANDIF(I(5), N(9),
            CMD_R(I(5), N(9),
                WORDS(WORD_SIMPLE(I(5), N(1), "b")),
                REDIRS(REDIR(
                    WORD_SIMPLE(I(10), N(4), "file"), IO(1), APPEND_ON, RDR_OUT
                ))
            )
        )
    );

    validate("a && b >> file", &exp);
}

TEST(parser, or_if_then_and_if) {
    ps_ast exp = AST(
        NOIF(I(0), N(1), CMD_SIMPLE(I(0), N(1), "a")),
        ORIF(I(5), N(1), CMD_SIMPLE(I(5), N(1), "b")),
        ANDIF(I(10), N(1), CMD_SIMPLE(I(10), N(1), "c"))
    );

    validate("a || b && c", &exp);
}

TEST(parser, redir_in) {
    ps_ast exp = AST(
        NOIF(I(0), N(15),
            CMD_R(I(0), N(15),
                WORDS(WORD_SIMPLE(I(0), N(3), "cat")),
                REDIRS(REDIR(WORD_SIMPLE(I(6), N(9), "input.txt"), IO(0),
                        APPEND_OFF, RDR_IN))
            )
        )
    );

    validate("cat < input.txt", &exp);
}

TEST(parser, redir_out) {
    ps_ast exp = AST(
        NOIF(I(0), N(13),
            CMD_R(I(0), N(13),
                WORDS(WORD_SIMPLE(I(0), N(2), "ls")),
                REDIRS(REDIR(WORD_SIMPLE(I(5), N(8), "file.txt"), IO(1),
                        APPEND_OFF, RDR_OUT))
            )
        )
    );

    validate("ls > file.txt", &exp);
}

TEST(parser, redir_err) {
    ps_ast exp = AST(
        NOIF(I(0), N(24),
            CMD_R(I(0), N(24),
                WORDS(WORD_SIMPLE(I(0), N(6), "solaar"),
                    WORD_SIMPLE(I(7), N(4), "show")),
                REDIRS(REDIR(WORD_SIMPLE(I(15), N(9), "/dev/null"), IO(2),
                        APPEND_OFF, RDR_OUT))
            )
        )
    );

    validate("solaar show 2> /dev/null", &exp);
}

TEST(parser, redir_append) {
    ps_ast exp = AST(
        NOIF(I(0), N(23),
            CMD_R(I(0), N(23),
                WORDS(WORD_SIMPLE(I(0), N(4), "ping"),
                    WORD_SIMPLE(I(5), N(7), "1.1.1.1")),
                REDIRS(REDIR(WORD_SIMPLE(I(16), N(7), "log.txt"), IO(1),
                        APPEND_ON, RDR_OUT))
            )
        )
    );

    validate("ping 1.1.1.1 >> log.txt", &exp);
}

TEST(parser, multiple_redirects) {
    ps_ast exp = AST(
        NOIF(I(0), N(35),
            CMD_R(I(0), N(35),
                WORDS(WORD_SIMPLE(I(0), N(6), "syslog")),
                REDIRS(
                    REDIR(WORD_SIMPLE(I(9), N(6), "output"), IO(1),
                        APPEND_OFF, RDR_OUT),
                    REDIR(WORD_SIMPLE(I(18), N(7), "output2"), IO(1),
                        APPEND_OFF, RDR_OUT),
                    REDIR(WORD_SIMPLE(I(29), N(6), "errors"), IO(2),
                        APPEND_OFF, RDR_OUT)
                )
            )
        )
    );

    validate("syslog > output > output2 2> errors", &exp);
}

TEST(parser, subshell_fail_rejections) {
    validate_fail("a (b && c)");
    validate_fail("a > (b && c)");
    validate_fail("(a |)");
}

TEST(parser, subshell_simple_command) {
    ps_ast subsh = AST(
        NOIF(I(1), N(6),
            CMD(I(1), N(6), WORD_SIMPLE(I(1), N(4), "echo"),
                WORD_SIMPLE(I(6), N(1), "x"))
        )
    );

    ps_ast exp = AST(NOIF(I(0), N(8), SUBSH(I(0), N(8), subsh)));

    validate("(echo x)", &exp);
}

TEST(parser, subshell_with_pipeline) {
    ps_ast subsh = AST(NOIF(I(1), N(5),
        CMD_SIMPLE(I(1), N(1), "a"), CMD_SIMPLE(I(5), N(1), "b")
    ));

    ps_ast exp = AST(NOIF(I(0), N(7), SUBSH(I(0), N(7), subsh)));

    validate("(a | b)", &exp);
}

TEST(parser, subshell_with_andors) {
    ps_ast subsh = AST(
        NOIF(I(1), N(1), CMD_SIMPLE(I(1), N(1), "a")),
        ANDIF(I(6), N(1), CMD_SIMPLE(I(6), N(1), "b"))
    );

    ps_ast exp = AST(NOIF(I(0), N(8), SUBSH(I(0), N(8), subsh)));

    validate("(a && b)", &exp);
}

TEST(parser, subshell_in_end_of_pipeline) {
    ps_ast subsh = AST(NOIF(I(5), N(1), CMD_SIMPLE(I(5), N(1), "b")));

    ps_ast exp = AST(NOIF(I(0), N(7),
        CMD_SIMPLE(I(0), N(1), "a"), SUBSH(I(4), N(3), subsh)
    ));

    validate("a | (b)", &exp);
}

TEST(parser, subshell_in_beginning_of_pipeline) {
    ps_ast subsh = AST(NOIF(I(1), N(1), CMD_SIMPLE(I(1), N(1), "a")));

    ps_ast exp = AST(NOIF(I(0), N(7),
        SUBSH(I(0), N(3), subsh), CMD_SIMPLE(I(6), N(1), "b")
    ));

    validate("(a) | b", &exp);
}

TEST(parser, subshell_in_end_of_andor) {
    ps_ast subsh = AST(NOIF(I(6), N(1), CMD_SIMPLE(I(6), N(1), "b")));

    ps_ast exp = AST(
        NOIF(I(0), N(1), CMD_SIMPLE(I(0), N(1), "a")),
        ANDIF(I(5), N(3), SUBSH(I(5), N(3), subsh))
    );

    validate("a && (b)", &exp);
}

TEST(parser, subshell_in_beginning_of_andor) {
    ps_ast subsh = AST(NOIF(I(1), N(1), CMD_SIMPLE(I(1), N(1), "a")));

    ps_ast exp = AST(
        NOIF(I(0), N(3), SUBSH(I(0), N(3), subsh)),
        ANDIF(I(7), N(1), CMD_SIMPLE(I(7), N(1), "b"))
    );

    validate("(a) && b", &exp);
}

TEST(parser, subshell_with_complex_command) {
    ps_ast subsh = AST_BG(
        NOIF(I(1), N(1), CMD_SIMPLE(I(1), N(1), "a")),
        ANDIF(I(6), N(5),
            CMD_SIMPLE(I(6), N(1), "b"),
            CMD_SIMPLE(I(10), N(1), "c")
        ),
        ORIF(I(15), N(5),
            CMD_R(I(15), N(5),
                WORDS(WORD_SIMPLE(I(15), N(1), "d")),
                REDIRS(REDIR(WORD_SIMPLE(I(19), N(1), "e"), IO(1),
                        APPEND_OFF, RDR_OUT))
            )
        )
    );

    ps_ast exp = AST(NOIF(I(0), N(23), SUBSH(I(0), N(23), subsh)));

    validate("(a && b | c || d > e &)", &exp);
}

TEST(parser, subshell_within_a_subshell) {
    ps_ast exp = AST(
        NOIF(I(0), N(5),
            SUBSH(I(0), N(5),
                AST(NOIF(I(1), N(3),
                    SUBSH(I(1), N(3), AST(NOIF(I(2),
                            N(1), CMD_SIMPLE(I(2), N(1), "a")))
                    )
                ))
            )
        )
    );

    validate("((a))", &exp);
}

TEST(parser, subshell_within_a_pipeline_in_a_subshell) {
    ps_ast exp = AST(
        NOIF(I(0), N(9),
            SUBSH(I(0), N(9),
                AST(NOIF(I(1), N(7),
                    SUBSH(I(1), N(3), AST(NOIF(I(2), N(1),
                        CMD_SIMPLE(I(2), N(1), "a")))
                    ),
                    CMD_SIMPLE(I(7), N(1), "b")
                ))
            )
        )
    );

    validate("((a) | b)", &exp);
}

TEST(parser, subshell_with_redirect) {
    ps_ast exp = AST(
        NOIF(I(0), N(7),
            SUBSH_R(I(0), N(7),
                AST(NOIF(I(1), N(1),
                    CMD_SIMPLE(I(1), N(1), "a"))
                ),
                REDIRS(REDIR(WORD_SIMPLE(I(6), N(1), "b"), IO(1),
                        APPEND_OFF, RDR_OUT))
            )
        )
    );

    validate("(a) > b", &exp);
}

TEST(parser, subshell_with_multiple_redirects) {
    ps_ast exp = AST(
        NOIF(I(0), N(17),
            SUBSH_R(I(0), N(17),
                AST(NOIF(I(1), N(1), CMD_SIMPLE(I(1), N(1), "a"))),
                REDIRS(
                    REDIR(WORD_SIMPLE(I(6), N(1), "b"), IO(1),
                        APPEND_OFF, RDR_OUT),
                    REDIR(WORD_SIMPLE(I(11), N(1), "c"), IO(2),
                        APPEND_OFF, RDR_OUT),
                    REDIR(WORD_SIMPLE(I(16), N(1), "d"), IO(1),
                        APPEND_ON, RDR_OUT)
                )
            )
        )
    );

    validate("(a) > b 2> c >> d", &exp);
}

TEST(parser, subshell_with_alias_expansion) {
    add_alias("p", "echo hello");

    ps_ast subsh = AST(NOIF(I(1), N(10),
        CMD(I(1), N(10),
            WORD_SIMPLE(I(1), N(4), "echo"),
            WORD_SIMPLE(I(6), N(5), "hello")
        )
    ));

    ps_ast exp = AST(NOIF(I(0), N(12), SUBSH(I(0), N(12), subsh)));

    validate_alias("(p)", &exp);
}

TEST(parser, subshell_with_complex_alias_expansion) {
    add_alias("bar", "baboom");

    ps_ast subsh = AST(NOIF(I(1), N(18),
        CMD_SIMPLE(I(1), N(3), "foo"),
        CMD_SIMPLE(I(7), N(6), "baboom"),
        CMD_SIMPLE(I(16), N(3), "bot"),
    ));

    ps_ast exp = AST(NOIF(I(0), N(20), SUBSH(I(0), N(20), subsh)));

    validate_alias("(foo | bar | bot)", &exp);
}

TEST(parser, subshell_with_quoted_alias_expansion) {
    add_alias("foo", "\"boom\"");

    ps_ast subsh = AST(NOIF(I(1), N(6),
        CMD(I(1), N(6), WORD_DBQ(I(1), N(6), "boom")),
    ));

    ps_ast exp = AST(NOIF(I(0), N(14),
        SUBSH(I(0), N(8), subsh),
        CMD_SIMPLE(I(11), N(3), "bar")
    ));

    validate_alias("(foo) | bar", &exp);
}

TEST(parser, subshell_with_leading_white_space_alias) {
    add_alias("foo", "    boom");

    ps_ast subsh = AST(NOIF(I(5), N(4),
        CMD_SIMPLE(I(5), N(4), "boom")
    ));

    ps_ast exp = AST(NOIF(I(0), N(16),
        SUBSH(I(0), N(10), subsh),
        CMD_SIMPLE(I(13), N(3), "bar"),
    ));

    validate_alias("(foo) | bar", &exp);
}

TEST(parser, double_nested_subshell_leading_white_space) {
    add_alias("foo", "    boom");

    ps_ast subsh_second = AST(NOIF(I(6), N(4),
        CMD_SIMPLE(I(6), N(4), "boom")
    ));

    ps_ast subsh_first = AST(NOIF(I(1), N(10),
        SUBSH(I(1), N(10), subsh_second)
    ));

    ps_ast exp = AST(NOIF(I(0), N(18),
        SUBSH(I(0), N(12), subsh_first),
        CMD_SIMPLE(I(15), N(3), "bar"),
    ));

    validate_alias("((foo)) | bar", &exp);
}

TEST(parser, alias_spans_are_reflected_in_ast) {
    add_alias("bar", "baboom");

    ps_ast exp = AST(NOIF(I(0), N(18),
        CMD_SIMPLE(I(0), N(3), "foo"),
        CMD_SIMPLE(I(6), N(6), "baboom"),
        CMD_SIMPLE(I(15), N(3), "bot"),
    ));

    validate_alias("foo | bar | bot", &exp);
}

TEST(parser, alias_simple_word) {
    add_alias("foo", "bar");

    ps_ast exp = AST(NOIF(I(0), N(3), CMD_SIMPLE(I(0), N(3), "bar")));

    validate_alias("foo", &exp);
}

TEST(parser, alias_multiple_words) {
    add_alias("foo", "bar bee boo");

    ps_ast exp = AST(
        NOIF(I(0), N(11),
            CMD(I(0), N(11),
                WORD_SIMPLE(I(0), N(3), "bar"),
                WORD_SIMPLE(I(4), N(3), "bee"),
                WORD_SIMPLE(I(8), N(3), "boo")
            )
        )
    );

    validate_alias("foo", &exp);
}

TEST(parser, alias_with_redirection) {
    add_alias("foo", "bar > bee");

    ps_ast exp = AST(
        NOIF(I(0), N(9),
            CMD_R(I(0), N(9),
                WORDS(WORD_SIMPLE(I(0), N(3), "bar")),
                REDIRS(REDIR(WORD_SIMPLE(I(6), N(3), "bee"), IO(1),
                        APPEND_OFF, RDR_OUT))
            )
        )
    );
    validate_alias("foo", &exp);
}

TEST(parser, alias_with_multiple_redirections) {
    add_alias("foo", "bar > bee 2> boo");

    ps_ast exp = AST(
        NOIF(I(0), N(16),
            CMD_R(I(0), N(16),
                WORDS(WORD_SIMPLE(I(0), N(3), "bar")),
                REDIRS(
                    REDIR(WORD_SIMPLE(I(6), N(3), "bee"), IO(1),
                        APPEND_OFF, RDR_OUT),
                    REDIR(WORD_SIMPLE(I(13), N(3), "boo"), IO(2),
                        APPEND_OFF, RDR_OUT)
                )
            )
        )
    );

    validate_alias("foo", &exp);
}

TEST(parser, alias_with_pipeline) {
    add_alias("foo", "bar | bee");

    ps_ast exp = AST(NOIF(I(0), N(9),
        CMD_SIMPLE(I(0), N(3), "bar"),
        CMD_SIMPLE(I(6), N(3), "bee")
    ));

    validate_alias("foo", &exp);
}

TEST(parser, alias_with_andor) {
    add_alias("foo", "bar && bee");

    ps_ast exp = AST(
        NOIF(I(0), N(3), CMD_SIMPLE(I(0), N(3), "bar")),
        ANDIF(I(7), N(3), CMD_SIMPLE(I(7), N(3), "bee"))
    );

    validate_alias("foo", &exp);
}

TEST(parser, alias_that_expands_into_pipeline) {
    add_alias("bar", "| bee");

    ps_ast exp = AST(NOIF(I(0), N(7),
        CMD(I(0), N(7),
            WORD_SIMPLE(I(0), N(3), "foo"),
            WORD_SIMPLE(I(4), N(3), "bar")
        )
    ));

    validate_alias("foo bar", &exp);
}

TEST(parser, alias_with_leading_white_space) {
    add_alias("foo", "    boom");

    ps_ast exp = AST(NOIF(I(4), N(8),
        CMD(I(4), N(8),
            WORD_SIMPLE(I(4), N(4), "boom"),
            WORD_SIMPLE(I(9), N(3), "bar")
        )
    ));

    validate_alias("foo bar", &exp);
}

TEST(parser, append_mode_is_set) {
    ps_ast exp = AST(NOIF(I(0), N(17),
        CMD_R(I(0), N(17),
            WORDS(WORD_SIMPLE(I(0), N(4), "echo"), WORD_SIMPLE(I(5), N(1), "x")),
            REDIRS(REDIR(WORD_SIMPLE(I(13), N(4), "file"), IO(123),
                    APPEND_ON, RDR_OUT))
        ))
    );

    validate("echo x 123>> file", &exp);
}

TEST(parser, io_number_fail_rejections) {
    validate_fail("echo 123>");
    validate_fail("echo 123>>");
    validate_fail("echo 123>> | next");
    validate_fail("echo 123>> && next");
    validate_fail("echo 123>> >out");
}

TEST(parser, arbitrary_io_numbers) {
    ps_ast exp = AST(
        NOIF(I(0), N(78),
            CMD_R(I(0), N(6),
                WORDS(WORD_SIMPLE(I(0), N(1), "a")),
                REDIRS(REDIR(WORD_SIMPLE(I(5), N(1), "f"), IO(1),
                        APPEND_OFF, RDR_OUT))
            ),
            CMD_R(I(9), N(6),
                WORDS(WORD_SIMPLE(I(9), N(1), "a")),
                REDIRS(REDIR(WORD_SIMPLE(I(14), N(1), "f"), IO(2),
                        APPEND_OFF, RDR_OUT))
            ),
            CMD_R(I(18), N(6),
                WORDS(WORD_SIMPLE(I(18), N(1), "a")),
                REDIRS(REDIR(WORD_SIMPLE(I(23), N(1), "f"), IO(3),
                        APPEND_OFF, RDR_OUT))
            ),
            CMD_R(I(27), N(9),
                WORDS(WORD_SIMPLE(I(27), N(1), "a")),
                REDIRS(REDIR(WORD_SIMPLE(I(35), N(1), "f"), IO(9999),
                        APPEND_OFF, RDR_OUT))
            ),
            CMD_R(I(38), N(7),
                WORDS(WORD_SIMPLE(I(38), N(1), "a")),
                REDIRS(REDIR(WORD_SIMPLE(I(44), N(1), "f"), IO(1),
                        APPEND_ON, RDR_OUT))
            ),
            CMD_R(I(48), N(7),
                WORDS(WORD_SIMPLE(I(48), N(1), "a")),
                REDIRS(REDIR(WORD_SIMPLE(I(54), N(1), "f"), IO(2),
                        APPEND_ON, RDR_OUT))
            ),
            CMD_R(I(58), N(7),
                WORDS(WORD_SIMPLE(I(58), N(1), "a")),
                REDIRS(REDIR(WORD_SIMPLE(I(64), N(1), "f"), IO(3),
                        APPEND_ON, RDR_OUT))
            ),
            CMD_R(I(68), N(10),
                WORDS(WORD_SIMPLE(I(68), N(1), "a")),
                REDIRS(REDIR(WORD_SIMPLE(I(77), N(1), "f"), IO(9999),
                        APPEND_ON, RDR_OUT))
            )
        )
    );

    validate(
        "a 1> f | a 2> f | a 3> f | a 9999> f |"
        "a 1>> f | a 2>> f | a 3>> f | a 9999>> f",
        &exp);
}

TEST(parser, multiple_arbitrary_io_number_redirects) {
    ps_ast exp = AST(
        NOIF(I(0), N(32),
            CMD_R(I(0), N(32),
                WORDS(WORD_SIMPLE(I(0), N(1), "a")),
                REDIRS(
                    REDIR(WORD_SIMPLE(I(5), N(3), "foo"), IO(1),
                        APPEND_OFF, RDR_OUT),
                    REDIR(WORD_SIMPLE(I(12), N(3), "123"), IO(2),
                        APPEND_OFF, RDR_OUT),
                    REDIR(WORD_SIMPLE(I(19), N(3), "bar"), IO(35),
                        APPEND_OFF, RDR_OUT),
                    REDIR(WORD_SIMPLE(I(29), N(3), "log"), IO(1003),
                        APPEND_ON, RDR_OUT)
                )
            )
        )
    );

    validate("a 1> foo 2> 123 35>bar 1003>>log", &exp);
}

TEST(parser, io_number_not_consumed_when_white_space) {
    ps_ast exp = AST(
        NOIF(I(0), N(10),
            CMD_R(I(0), N(10),
                WORDS(
                    WORD_SIMPLE(I(0), N(1), "a"),
                    WORD_SIMPLE(I(2), N(3), "123")
                ),
                REDIRS(REDIR(WORD_SIMPLE(I(7), N(3), "foo"), IO(1),
                        APPEND_OFF, RDR_OUT))
            )
        )
    );

    validate("a 123 >foo", &exp);
}

TEST(parser, argument_after_redirect) {
    ps_ast exp = AST(
        NOIF(I(0), N(18),
            CMD_R(I(0), N(18),
                WORDS(
                    WORD_SIMPLE(I(0), N(4), "echo"),
                    WORD_SIMPLE(I(14), N(4), "tail")
                ),
                REDIRS(REDIR(WORD_SIMPLE(I(10), N(3), "out"), IO(123),
                        APPEND_ON, RDR_OUT))
            )
        )
    );

    validate("echo 123>>out tail", &exp);

}

TEST(parser, non_append_between_io_number_appends) {
    ps_ast exp = AST(
        NOIF(I(0), N(17),
            CMD_R(I(0), N(17),
                WORDS(WORD_SIMPLE(I(0), N(4), "echo")),
                REDIRS(
                    REDIR(WORD_SIMPLE(I(8), N(1), "a"), IO(3),
                        APPEND_ON, RDR_OUT),
                    REDIR(WORD_SIMPLE(I(11), N(1), "b"), IO(1),
                        APPEND_OFF, RDR_OUT),
                    REDIR(WORD_SIMPLE(I(16), N(1), "c"), IO(4),
                        APPEND_ON, RDR_OUT)
                )
            )
        )
    );

    validate("echo 3>>a >b 4>>c", &exp);
}

TEST(parser, io_numbers_inside_subshells) {
    ps_ast exp = AST(NOIF(I(0), N(15),
        SUBSH(I(0), N(15),
            AST(NOIF(I(1), N(13),
                CMD_R(I(1), N(13),
                    WORDS(WORD_SIMPLE(I(1), N(4), "echo")),
                    REDIRS(
                        REDIR(WORD_SIMPLE(I(11), N(3), "out"), IO(123),
                            APPEND_ON, RDR_OUT)
                    )
                )
            ))
        )
    ));

    validate("(echo 123>>out)", &exp);
}

TEST(parser, io_number_redirect_subshell_output) {
    ps_ast exp = AST(NOIF(I(0), N(15),
        SUBSH_R(I(0), N(15),
            AST(NOIF(I(1), N(4), CMD_SIMPLE(I(1), N(4), "echo"))),
            REDIRS(REDIR(WORD_SIMPLE(I(12), N(3), "out"), IO(123),
                    APPEND_ON, RDR_OUT))
        )
    ));

    validate("(echo) 123>>out", &exp);
}

TEST(parser, andor_connected_commands_after_io_number_redirect) {
    ps_ast exp = AST(
        NOIF(I(0), N(13),
            CMD_R(I(0), N(13),
                WORDS(WORD_SIMPLE(I(0), N(4), "echo")),
                REDIRS(REDIR(WORD_SIMPLE(I(10), N(3), "out"), IO(123),
                        APPEND_ON, RDR_OUT))
            )
        ),
        ANDIF(I(17), N(4), CMD_SIMPLE(I(17), N(4), "next"))
    );

    validate("echo 123>>out && next", &exp);
}

TEST(parser, fd_duplication_fail_rejections) {
    validate_fail("echo 1>&");
    validate_fail("echo 1>&x");
    validate_fail("echo 1>&>");
    validate_fail("echo 1>&||");
    validate_fail("echo 1>&&");
    validate_fail("echo 1>>&1");
    validate_fail("echo 1>>&x");
    validate_fail("echo 1>&123x");
}

TEST(parser, fd_duplication) {
    ps_ast exp = AST(
        NOIF(I(0), N(11),
            CMD_R(I(0), N(11),
                WORDS(
                    WORD_SIMPLE(I(0), N(4), "echo"),
                    WORD_SIMPLE(I(5), N(1), "x")
                ),
                REDIRS(REDIR_FD(IO(1), IO(2), APPEND_OFF, RDR_OUT))
            )
        )
    );

    validate("echo x 2>&1", &exp);
}

TEST(parser, multiple_fd_duplications) {
    ps_ast exp = AST(
        NOIF(I(0), N(23),
            CMD_R(I(0), N(23),
                WORDS(
                    WORD_SIMPLE(I(0), N(4), "echo"),
                    WORD_SIMPLE(I(5), N(1), "x")
                ),
                REDIRS(
                    REDIR_FD(IO(2), IO(1), APPEND_OFF, RDR_OUT),
                    REDIR_FD(IO(1), IO(2), APPEND_OFF, RDR_OUT),
                    REDIR_FD(IO(3), IO(999), APPEND_OFF, RDR_IN)
                )
            )
        )
    );

    validate("echo x 1>&2 2>&1 999<&3", &exp);
}

TEST(parser, file_redirections_after_fd_duplication) {
    ps_ast exp = AST(
        NOIF(I(0), N(15),
            CMD_R(I(0), N(15),
                WORDS(
                    WORD_SIMPLE(I(0), N(4), "echo"),
                    WORD_SIMPLE(I(5), N(1), "x")
                ),
                REDIRS(
                    REDIR_FD(IO(2), IO(1), APPEND_OFF, RDR_OUT),
                    REDIR(WORD_SIMPLE(I(12), N(3), "out"), IO(1),
                        APPEND_OFF, RDR_OUT)
                )
            )
        )
    );

    validate("echo x 1>&2>out", &exp);
}

TEST(parser, multiple_fd_duplications_no_white_space) {
    ps_ast exp = AST(
        NOIF(I(0), N(15),
            CMD_R(I(0), N(15),
                WORDS(
                    WORD_SIMPLE(I(0), N(4), "echo"),
                    WORD_SIMPLE(I(5), N(1), "x")
                ),
                REDIRS(
                    REDIR_FD(IO(23), IO(1), APPEND_OFF, RDR_OUT),
                    REDIR_FD(IO(2), IO(1), APPEND_OFF, RDR_OUT)
                )
            )
        )
    );

    validate("echo x 1>&23>&2", &exp);
}

TEST(parser, white_space_between_fd_target_and_ampersand) {
    ps_ast exp = AST(
        NOIF(I(0), N(14),
            CMD_R(I(0), N(14),
                WORDS(
                    WORD_SIMPLE(I(0), N(4), "echo"),
                    WORD_SIMPLE(I(5), N(1), "x")
                ),
                REDIRS(REDIR_FD(IO(2), IO(1), APPEND_OFF, RDR_OUT))
            )
        )
    );

    validate("echo x 1>&   2", &exp);
}

/************ Test runner ************/

TEST_GROUP_RUNNER(parser) {
    RUN_TEST_CASE(parser, general_fail_rejections);
    RUN_TEST_CASE(parser, bg_is_final_token_should_pass);
    RUN_TEST_CASE(parser, cmd_one_word);
    RUN_TEST_CASE(parser, cmd_two_word);
    RUN_TEST_CASE(parser, cmd_many_word);
    RUN_TEST_CASE(parser, two_cmd_pipeline);
    RUN_TEST_CASE(parser, three_cmd_pipeline);
    RUN_TEST_CASE(parser, two_pipeline_and_if);
    RUN_TEST_CASE(parser, two_pipeline_or_if);
    RUN_TEST_CASE(parser, or_if_then_and_if);
    RUN_TEST_CASE(parser, container_control_flow);
    RUN_TEST_CASE(parser, and_if_with_redirects);
    RUN_TEST_CASE(parser, redir_in);
    RUN_TEST_CASE(parser, redir_out);
    RUN_TEST_CASE(parser, redir_err);
    RUN_TEST_CASE(parser, redir_append);
    RUN_TEST_CASE(parser, multiple_redirects);

    // /* subshell */
    RUN_TEST_CASE(parser, subshell_fail_rejections);
    RUN_TEST_CASE(parser, subshell_simple_command);
    RUN_TEST_CASE(parser, subshell_with_pipeline);
    RUN_TEST_CASE(parser, subshell_with_andors);
    RUN_TEST_CASE(parser, subshell_in_end_of_pipeline);
    RUN_TEST_CASE(parser, subshell_in_beginning_of_pipeline);
    RUN_TEST_CASE(parser, subshell_in_end_of_andor);
    RUN_TEST_CASE(parser, subshell_in_beginning_of_andor);
    RUN_TEST_CASE(parser, subshell_with_complex_command);
    RUN_TEST_CASE(parser, subshell_within_a_subshell);
    RUN_TEST_CASE(parser, subshell_within_a_pipeline_in_a_subshell);
    RUN_TEST_CASE(parser, subshell_with_redirect);
    RUN_TEST_CASE(parser, subshell_with_multiple_redirects);
    RUN_TEST_CASE(parser, subshell_with_alias_expansion);
    RUN_TEST_CASE(parser, subshell_with_complex_alias_expansion);
    RUN_TEST_CASE(parser, subshell_with_quoted_alias_expansion);
    RUN_TEST_CASE(parser, subshell_with_leading_white_space_alias);
    RUN_TEST_CASE(parser, double_nested_subshell_leading_white_space);

    /* alias expansion */
    RUN_TEST_CASE(parser, alias_spans_are_reflected_in_ast);
    RUN_TEST_CASE(parser, alias_simple_word);
    RUN_TEST_CASE(parser, alias_multiple_words);
    RUN_TEST_CASE(parser, alias_with_redirection);
    RUN_TEST_CASE(parser, alias_with_multiple_redirections);
    RUN_TEST_CASE(parser, alias_with_pipeline);
    RUN_TEST_CASE(parser, alias_with_andor);
    RUN_TEST_CASE(parser, alias_that_expands_into_pipeline);
    RUN_TEST_CASE(parser, alias_with_leading_white_space);

    /* variable io numbers */
    RUN_TEST_CASE(parser, io_number_fail_rejections);
    RUN_TEST_CASE(parser, append_mode_is_set);
    RUN_TEST_CASE(parser, arbitrary_io_numbers);
    RUN_TEST_CASE(parser, multiple_arbitrary_io_number_redirects);
    RUN_TEST_CASE(parser, io_number_not_consumed_when_white_space);
    RUN_TEST_CASE(parser, argument_after_redirect);
    RUN_TEST_CASE(parser, non_append_between_io_number_appends);
    RUN_TEST_CASE(parser, io_numbers_inside_subshells);
    RUN_TEST_CASE(parser, io_number_redirect_subshell_output);
    RUN_TEST_CASE(parser, andor_connected_commands_after_io_number_redirect);

    /* file descriptor duplication */
    RUN_TEST_CASE(parser, fd_duplication_fail_rejections);
    RUN_TEST_CASE(parser, fd_duplication);
    RUN_TEST_CASE(parser, multiple_fd_duplications);
    RUN_TEST_CASE(parser, file_redirections_after_fd_duplication);
    RUN_TEST_CASE(parser, multiple_fd_duplications_no_white_space);
    RUN_TEST_CASE(parser, white_space_between_fd_target_and_ampersand);
}
