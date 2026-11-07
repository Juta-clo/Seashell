#include "unity_fixture.h"
#include "parser.h"
#include "lexer.h"
#include "map.h"
#include "state.h"

TEST_GROUP(parser);

/************ Shared utils ************/

#define IO_NUM_IN  0
#define IO_NUM_OUT 1
#define IO_NUM_ERR 2

#define APPEND_ON  1
#define APPEND_OFF 0

#define EXP_DA(DA_T, ELEM_T, ...) \
    ((DA_T){ \
        .data = (ELEM_T[]) { __VA_ARGS__ }, \
        .size = sizeof((ELEM_T[]) { __VA_ARGS__ }) / sizeof(ELEM_T) \
     })

#define PLAIN(RAW) \
    ((exp_segment){ .raw = (RAW), .quote = PS_Q_NONE })

#define SEGMENT(RAW, QUOTE) \
    ((exp_segment){ .raw = (RAW), .quote = (QUOTE) })

#define SEGMENTS(...) \
    EXP_DA(exp_da_segment, exp_segment, __VA_ARGS__)

#define WORD(SEGMENTS) \
    ((exp_word){ .segments = (SEGMENTS) })

#define PLAIN_WORD(RAW) \
    WORD(SEGMENTS(PLAIN(RAW)))

#define DQ_WORD(RAW) \
    WORD(SEGMENTS(SEGMENT((RAW), PS_Q_DOUBLE)))

#define WORDS(...) \
    EXP_DA(exp_da_word, exp_word, __VA_ARGS__)

#define NO_REDIRS \
    ((exp_da_redir){0})

#define REDIR(TARGET, IO_NUM, APPEND) \
    ((exp_redir){ .target = (TARGET), .io_num = (IO_NUM), .append = (APPEND) })

#define REDIRS(...) \
    EXP_DA(exp_da_redir, exp_redir, __VA_ARGS__)

#define CMD_R(WORDS, REDIRS) \
    ((exp_cmd){ \
        .kind = PS_CMD_SIMPLE, \
        .words = (WORDS), \
        .redirs = (REDIRS) \
     })

#define CMD(...) \
    CMD_R(WORDS(__VA_ARGS__), NO_REDIRS)

#define SUBSH_R(_ast, ...) \
    ((exp_cmd){ \
        .kind = PS_CMD_SUBSH, \
        .subsh = (_ast), \
        .redirs = (__VA_ARGS__), \
    })

#define SUBSH(_ast) \
    SUBSH_R(_ast, NO_REDIRS)

#define SIMPLE_CMD(RAW) \
    CMD(WORDS(PLAIN_WORD(RAW)))

#define CMDS(...) \
    EXP_DA(exp_da_cmd, exp_cmd, __VA_ARGS__)

#define PIPELINE(...) \
    ((exp_pipeline){ .cmds = CMDS(__VA_ARGS__) })

#define NOIF(...) \
    ((exp_andor){ .pipeline = PIPELINE(__VA_ARGS__), .op = PS_NO_IF })

#define ANDIF(...) \
    ((exp_andor){ .pipeline = PIPELINE(__VA_ARGS__), .op = PS_AND_IF })

#define ORIF(...) \
    ((exp_andor){ .pipeline = PIPELINE(__VA_ARGS__), .op = PS_OR_IF })

#define ANDORS(...) \
    EXP_DA(exp_da_andor, exp_andor, __VA_ARGS__)

#define BG_OFF 0
#define BG_ON  1

#define AST(...) \
    ((exp_ast){ .andors = ANDORS(__VA_ARGS__), .bg = BG_OFF })

#define AST_BG(...) \
    ((exp_ast){ .andors = ANDORS(__VA_ARGS__), .bg = BG_ON })

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


typedef struct exp_andor exp_andor;

typedef struct {
    exp_andor *data;
    size_t size;
} exp_da_andor;

typedef struct {
    exp_da_andor andors;
    bool bg;
} exp_ast;

typedef struct {
    char *raw;
    ps_quote quote;
} exp_segment;

typedef struct {
    exp_segment *data;
    size_t size;
} exp_da_segment;

typedef struct {
    exp_da_segment segments;
} exp_word;

typedef struct {
    exp_word target;
    int io_num;
    int append;
} exp_redir;

typedef struct {
    exp_redir *data;
    size_t size;
} exp_da_redir;

typedef struct {
    exp_word *data;
    size_t size;
} exp_da_word;

typedef struct {
    ps_cmd_kind kind;

    exp_da_word words;
    exp_da_redir redirs;

    exp_ast subsh;
} exp_cmd;

typedef struct {
    exp_cmd *data;
    size_t size;
} exp_da_cmd;

typedef struct {
    exp_da_cmd cmds;
} exp_pipeline;

struct exp_andor {
    exp_pipeline pipeline;
    ps_andor_op op;
};

static void validate_job(const exp_ast *exp, const ps_ast *job);

static void validate_segment(const exp_segment *exp, const ps_segment *segment) {
    TEST_ASSERT_EQUAL_STRING(exp->raw, segment->raw);
    TEST_ASSERT_EQUAL(exp->quote, segment->quote);
}

static void validate_segments(const exp_da_segment *exp, const da_segment *segments) {
    TEST_ASSERT_EQUAL_size_t(exp->size, segments->size);
    for (size_t i = 0; i < segments->size; ++i) {
        const exp_segment *exp_segment = &exp->data[i];
        const ps_segment *segment = &segments->data[i];
        validate_segment(exp_segment, segment);
    }
}

static void validate_tok(const exp_word *exp, const ps_word *word) {
    validate_segments(&exp->segments, &word->segments);
}

static void validate_redir(const exp_redir *exp, const ps_redir *redir) {
    TEST_ASSERT_EQUAL_INT(exp->io_num, redir->io_num);
    TEST_ASSERT_EQUAL_INT(exp->append, redir->append);
    validate_tok(&exp->target, &redir->target);
}

static void validate_redirs(const exp_da_redir *exp, const da_redir *redirs) {
    TEST_ASSERT_EQUAL_size_t(exp->size, redirs->size);
    for (size_t i = 0; i < redirs->size; ++i) {
        const exp_redir *exp_redir = &exp->data[i];
        const ps_redir *redir = &redirs->data[i];
        validate_redir(exp_redir, redir);
    }
}

static void validate_words(const exp_da_word *exp, const da_word *words) {
    TEST_ASSERT_EQUAL_size_t(exp->size, words->size);
    for (size_t i = 0; i < words->size; ++i) {
        const exp_word *exp_tok = &exp->data[i];
        const ps_word *word = &words->data[i];
        validate_tok(exp_tok, word);
    }
}

static void validate_cmd(const exp_cmd *exp, const ps_cmd *cmd) {
    if (exp->kind == PS_CMD_SIMPLE) {
        TEST_ASSERT_EQUAL(PS_CMD_SIMPLE, cmd->kind);
        validate_words(&exp->words, &cmd->words);
        validate_redirs(&exp->redirs, &cmd->redirs);
    } else {
        TEST_ASSERT_EQUAL(PS_CMD_SUBSH, cmd->kind);
        validate_job(&exp->subsh, &cmd->subsh);
        validate_redirs(&exp->redirs, &cmd->redirs);
    }
}

static void validate_cmds(const exp_da_cmd *exp, const da_cmd *cmds) {
    TEST_ASSERT_EQUAL_size_t(exp->size, cmds->size);
    for (size_t i = 0; i < cmds->size; ++i) {
        const exp_cmd *exp_cmd = &exp->data[i];
        const ps_cmd *cmd = &cmds->data[i];
        validate_cmd(exp_cmd, cmd);
    }
}

static void validate_pipeline(const exp_pipeline *exp, const ps_pline *pipeline) {
    validate_cmds(&exp->cmds, &pipeline->cmds);
}

static void validate_andor(const exp_andor *exp, const ps_andor *andor) {
    TEST_ASSERT_EQUAL(exp->op, andor->op);
    validate_pipeline(&exp->pipeline, &andor->pline);
}

static void validate_andors(const exp_da_andor *exp, const da_andor *andors) {
    TEST_ASSERT_EQUAL_size_t(exp->size, andors->size);
    for (size_t i = 0; i < andors->size; ++i) {
        const exp_andor *exp_andor = &exp->data[i];
        const ps_andor *andor = &andors->data[i];
        validate_andor(exp_andor, andor);
    }
}

static void validate_job(const exp_ast *exp, const ps_ast *job) {
    TEST_ASSERT_EQUAL_INT(exp->bg, job->bg);
    validate_andors(&exp->andors, &job->andors);
}

static void validate(const char *shell_cmd, const exp_ast *exp) {
    da_tok tokens = {0};
    TEST_ASSERT_NOT_EQUAL_INT(-1, lx_tokenize(shell_cmd, &tokens));

    ps_ast job = {0};
    TEST_ASSERT_NOT_EQUAL_INT(-1, ps_parse(&tokens, &job, true));

    validate_job(exp, &job);

    lx_free(&tokens);
    ps_free(&job);
}

static void validate_fail(const char *shell_cmd) {
    da_tok tokens = {0};
    ps_ast ast = {0};

    if (lx_tokenize(shell_cmd, &tokens) == -1)
        TEST_FAIL_MESSAGE("lx_tokenize() failed");

    if (ps_parse(&tokens, &ast, false) != -1) {
        ps_free(&ast);
        TEST_FAIL_MESSAGE("ps_parse() didn't fail");
    }

    lx_free(&tokens);
}

/************ Fixture ************/

TEST_SETUP(parser) {
    mp_init(&env.aliases, 4096);
}

TEST_TEAR_DOWN(parser) {
    mp_free(&env.aliases);
}

/************ Tests ************/

TEST(parser, bg_not_final_token_should_fail) {
    validate_fail("a & b");
}

TEST(parser, redirect_with_no_target_fails) {
    validate_fail("echo hi >");
}

TEST(parser, pipe_is_final_token_should_fail) {
    validate_fail("cmd |");
}

TEST(parser, andif_is_final_token_should_fail) {
    validate_fail("cmd &&");
}

TEST(parser, orif_is_final_token_should_fail) {
    validate_fail("cmd ||");
}

TEST(parser, no_cmd_redir_should_fail) {
    validate_fail("echo > |");
}

TEST(parser, bg_is_final_token_should_pass) {
    exp_ast exp = AST_BG(
        NOIF(
            CMD(
                PLAIN_WORD("a")
            )
        )
    );
    validate("a &", &exp);
}

TEST(parser, cmd_one_word) {
    exp_ast exp = AST_SIMPLE(
        PLAIN_WORD("a")
    );
    validate("a", &exp);
}

TEST(parser, cmd_two_word) {
    exp_ast exp = AST_SIMPLE(
        PLAIN_WORD("a"),
        PLAIN_WORD("b")
    );
    validate("a b", &exp);
}

TEST(parser, cmd_many_word) {
    exp_ast exp = AST_SIMPLE(
        PLAIN_WORD("command"),
        PLAIN_WORD("arg1"),
        PLAIN_WORD("arg2"),
        PLAIN_WORD("arg3"),
        PLAIN_WORD("arg4"),
    );
    validate("command arg1 arg2 arg3 arg4", &exp);
}

TEST(parser, two_cmd_pipeline) {
    exp_ast exp = AST(
        NOIF(
            CMD(PLAIN_WORD("a")),
            CMD(PLAIN_WORD("b"))
        )
    );
    validate("a | b", &exp);
}

TEST(parser, three_cmd_pipeline) {
    exp_ast exp = AST(
        NOIF(
            CMD(PLAIN_WORD("a")),
            CMD(PLAIN_WORD("b")),
            CMD(PLAIN_WORD("c")),
        )
    );
    validate("a | b | c", &exp);
}

TEST(parser, two_pipeline_and_if) {
    exp_ast exp = AST(
        NOIF(
            CMD(PLAIN_WORD("a"))
        ),
        ANDIF(
            CMD(PLAIN_WORD("b"))
        )
    );
    validate("a && b", &exp);
}

TEST(parser, two_pipeline_or_if) {
    exp_ast exp = AST(
        NOIF(
            CMD(PLAIN_WORD("a"))
        ),
        ORIF(
            CMD(PLAIN_WORD("b"))
        )
    );
    validate("a || b", &exp);
}

TEST(parser, container_control_flow) {
    exp_ast exp = AST(
        NOIF(
            CMD(
                PLAIN_WORD("wc"),
                PLAIN_WORD("-l"),
                PLAIN_WORD("file.txt")
            ),
            CMD(
                PLAIN_WORD("grep"),
                PLAIN_WORD("3")
            )
        ),
        ANDIF(
            CMD(
                PLAIN_WORD("echo"),
                DQ_WORD("there are three lines "
                    "in file.txt!")
            ),
        )
    );
    validate("wc -l file.txt | grep 3 && echo \"there are three "
            "lines in file.txt!\"", &exp);
}

TEST(parser, and_if_with_redirects) {
    exp_ast exp = AST(
        NOIF(
            CMD(PLAIN_WORD("a"))
        ),
        ANDIF(
            CMD_R(
                WORDS(
                    PLAIN_WORD("b")
                ),
                REDIRS(
                    REDIR(
                        PLAIN_WORD("file"),
                        IO_NUM_OUT, APPEND_ON
                    )
                )
            )
        ),
    );
    validate("a && b >> file", &exp);
}

TEST(parser, or_if_then_and_if) {
    exp_ast exp = AST(
        NOIF(
            CMD(PLAIN_WORD("a"))
        ),
        ORIF(
            CMD(PLAIN_WORD("b")),
        ),
        ANDIF(
            CMD(PLAIN_WORD("c")),
        )
    );
    validate("a || b && c", &exp);
}

TEST(parser, redir_in) {
    exp_ast exp = AST(
        NOIF(
            CMD_R(
                WORDS(
                    PLAIN_WORD("cat"),
                ),
                REDIRS(
                    REDIR(
                        PLAIN_WORD("input.txt"),
                        IO_NUM_IN, APPEND_OFF
                    )
                )
            )
        )
    );
    validate("cat < input.txt", &exp);
}

TEST(parser, redir_out) {
    exp_ast exp = AST(
        NOIF(
            CMD_R(
                WORDS(
                    PLAIN_WORD("ls"),
                ),
                REDIRS(
                    REDIR(
                        PLAIN_WORD("file.txt"),
                        1, 0
                    )
                )
            )
        )
    );
    validate("ls > file.txt", &exp);
}

TEST(parser, redir_err) {
    exp_ast exp = AST(
        NOIF(
            CMD_R(
                WORDS(
                    PLAIN_WORD("solaar"),
                    PLAIN_WORD("show"),
                ),
                REDIRS(
                    REDIR(
                        PLAIN_WORD("/dev/null"),
                        IO_NUM_ERR, APPEND_OFF
                    )
                )
            )
        )
    );
    validate("solaar show 2> /dev/null", &exp);
}

TEST(parser, redir_append) {
    exp_ast exp = AST(
        NOIF(
            CMD_R(
                WORDS(
                    PLAIN_WORD("ping"),
                    PLAIN_WORD("1.1.1.1")
                ),
                REDIRS(
                    REDIR(
                        PLAIN_WORD("log.txt"),
                        IO_NUM_OUT, APPEND_ON
                    )
                )
            )
        )
    );
    validate("ping 1.1.1.1 >> log.txt", &exp);
}

TEST(parser, multiple_redirects) {
    exp_ast exp = AST(
        NOIF(
            CMD_R(
                WORDS(
                    PLAIN_WORD("syslog"),
                ),
                REDIRS(
                    REDIR(
                        PLAIN_WORD("output"),
                        IO_NUM_OUT, APPEND_OFF
                    ),
                    REDIR(
                        PLAIN_WORD("output2"),
                        IO_NUM_OUT, APPEND_OFF
                    ),
                    REDIR(
                        PLAIN_WORD("errors"),
                        IO_NUM_ERR, APPEND_OFF
                    ),
                )
            )
        )
    );
    validate("syslog > output > output2 2> errors", &exp);
}

TEST(parser, subshell_not_in_command_position_should_fail) {
    validate_fail("a (b && c)");
}

TEST(parser, subshell_after_redirect_should_fail) {
    validate_fail("a > (b && c)");
}

TEST(parser, subshell_nested_syntax_error_should_fail) {
    validate_fail("(a |)");
}

TEST(parser, subshell_simple_command) {
    exp_ast subsh = AST_SIMPLE(
        PLAIN_WORD("echo"),
        PLAIN_WORD("x")
    );

    exp_ast exp = AST(NOIF(SUBSH(subsh)));

    validate("(echo x)", &exp);
}

TEST(parser, subshell_with_pipeline) {
    exp_ast subsh = AST(
        NOIF(
            CMD(PLAIN_WORD("a")),
            CMD(PLAIN_WORD("b"))
        )
    );

    exp_ast exp = AST(NOIF(SUBSH(subsh)));

    validate("(a | b)", &exp);
}

TEST(parser, subshell_with_andors) {
    exp_ast subsh = AST(
        NOIF(
            CMD(PLAIN_WORD("a"))
        ),
        ANDIF(
            CMD(PLAIN_WORD("b"))
        )
    );

    exp_ast exp = AST(NOIF(SUBSH(subsh)));

    validate("(a && b)", &exp);
}

TEST(parser, subshell_in_end_of_pipeline) {
    exp_ast subsh = AST_SIMPLE(PLAIN_WORD("b"));

    exp_ast exp = AST(
        NOIF(
            CMD(PLAIN_WORD("a")),
            SUBSH(subsh)
        )
    );

    validate("a | (b)", &exp);
}

TEST(parser, subshell_in_beginning_of_pipeline) {
    exp_ast subsh = AST_SIMPLE(PLAIN_WORD("a"));

    exp_ast exp = AST(
        NOIF(
            SUBSH(subsh),
            CMD(PLAIN_WORD("b"))
        )
    );

    validate("(a) | b", &exp);
}

TEST(parser, subshell_in_end_of_andor) {
    exp_ast subsh = AST_SIMPLE(PLAIN_WORD("b"));

    exp_ast exp = AST(
        NOIF(
            CMD(PLAIN_WORD("a")),
        ),
        ANDIF(
            SUBSH(subsh)
        )
    );

    validate("a && (b)", &exp);
}

TEST(parser, subshell_in_beginning_of_andor) {
    exp_ast subsh = AST_SIMPLE(PLAIN_WORD("a"));

    exp_ast exp = AST(
        NOIF(
            SUBSH(subsh)
        ),
        ANDIF(
            CMD(PLAIN_WORD("b")),
        )
    );

    validate("(a) && b", &exp);
}

TEST(parser, subshell_with_complex_command) {
    exp_ast subsh = AST_BG(
        NOIF(CMD(PLAIN_WORD("a"))),
        ANDIF(CMD(PLAIN_WORD("b")), CMD(PLAIN_WORD("c"))),
        ORIF(CMD_R(
            WORDS(PLAIN_WORD("d")),
            REDIRS(REDIR(PLAIN_WORD("e"), 1, false)))
        ),
    );

    exp_ast exp = AST(NOIF(SUBSH(subsh)));

    validate("(a && b | c || d > e &)", &exp);
}

TEST(parser, subshell_within_a_subshell) {
    exp_ast exp = AST(NOIF(SUBSH(
        AST(NOIF(SUBSH(
            AST_SIMPLE(PLAIN_WORD("a"))))
        )
    )));

    validate("((a))", &exp);
}

TEST(parser, subshell_within_a_pipeline_in_a_subshell) {
    exp_ast exp = AST(NOIF(SUBSH(
        AST(NOIF(
            SUBSH(AST_SIMPLE(PLAIN_WORD("a"))),
            CMD(PLAIN_WORD("b"))
        ))
    )));

    validate("((a) | b)", &exp);
}

TEST(parser, subshell_with_redirect) {
    exp_ast exp = AST(
        NOIF(SUBSH_R(
            AST_SIMPLE(PLAIN_WORD("a")),
            REDIRS(REDIR(PLAIN_WORD("b"), 1, false))
        ))
    );

    validate("(a) > b", &exp);
}

TEST(parser, subshell_with_multiple_redirects) {
    exp_ast exp = AST(
        NOIF(SUBSH_R(
            AST_SIMPLE(PLAIN_WORD("a")),
            REDIRS(
                REDIR(PLAIN_WORD("b"), 1, false),
                REDIR(PLAIN_WORD("c"), 2, false),
                REDIR(PLAIN_WORD("d"), 1, true)
            )
        ))
    );

    validate("(a) > b 2> c >> d", &exp);
}

TEST(parser, subshell_with_alias_expansion) {
    if (mp_str_add(&env.aliases, "p", strdup("echo hello")) == -1)
        TEST_FAIL_MESSAGE("mp_str_add() failed");

    exp_ast exp = AST(
        NOIF(SUBSH(
            AST_SIMPLE(PLAIN_WORD("echo"), PLAIN_WORD("hello"))
        ))
    );

    validate("(p)", &exp);
}

TEST(parser, alias_simple_word) {
    add_alias("foo", "bar");
    exp_ast exp = AST_SIMPLE(PLAIN_WORD("bar"));
    validate("foo", &exp);
}

TEST(parser, alias_multiple_words) {
    add_alias("foo", "bar bee boo");
    exp_ast exp = AST_SIMPLE(
        PLAIN_WORD("bar"),
        PLAIN_WORD("bee"),
        PLAIN_WORD("boo"),
    );
    validate("foo", &exp);
}

TEST(parser, alias_with_redirection) {
    add_alias("foo", "bar > bee");
    exp_ast exp = AST(
        NOIF(CMD_R(
            WORDS(PLAIN_WORD("bar")),
            REDIRS(REDIR(PLAIN_WORD("bee"), 1, false))
        )
    ));
    validate("foo", &exp);
}

TEST(parser, alias_with_multiple_redirections) {
    add_alias("foo", "bar > bee 2> boo");
    exp_ast exp = AST(
        NOIF(CMD_R(
            WORDS(PLAIN_WORD("bar")),
            REDIRS(
                REDIR(PLAIN_WORD("bee"), 1, false),
                REDIR(PLAIN_WORD("boo"), 2, false)
            )
        )
    ));
    validate("foo", &exp);
}

TEST(parser, alias_with_pipeline) {
    add_alias("foo", "bar | bee");
    exp_ast exp = AST(
        NOIF(
            CMD(PLAIN_WORD("bar")),
            CMD(PLAIN_WORD("bee"))
        )
    );
    validate("foo", &exp);
}

TEST(parser, alias_with_andor) {
    add_alias("foo", "bar && bee");
    exp_ast exp = AST(
        NOIF(CMD(PLAIN_WORD("bar"))),
        ANDIF(CMD(PLAIN_WORD("bee")))
    );
    validate("foo", &exp);
}

TEST(parser, alias_that_expands_into_pipeline) {
    add_alias("bar", "| bee");
    exp_ast exp = AST(NOIF(CMD(
        PLAIN_WORD("foo"),
        PLAIN_WORD("bar")
    )));
    validate("foo bar", &exp);
}

TEST(parser, append_mode_is_set) {
    exp_ast exp = AST(NOIF(CMD_R(
        WORDS(PLAIN_WORD("echo"), PLAIN_WORD("x")),
        REDIRS(REDIR(PLAIN_WORD("file"), 123, 1))
    )));

    validate("echo x 123>> file", &exp);
}

TEST(parser, arbitrary_io_numbers) {
    exp_ast exp = AST(NOIF(
        CMD_R(WORDS(PLAIN_WORD("a")),
            REDIRS(REDIR(PLAIN_WORD("f"), 1, 0))),
        CMD_R(WORDS(PLAIN_WORD("a")),
            REDIRS(REDIR(PLAIN_WORD("f"), 2, 0))),
        CMD_R(WORDS(PLAIN_WORD("a")),
            REDIRS(REDIR(PLAIN_WORD("f"), 3, 0))),
        CMD_R(WORDS(PLAIN_WORD("a")),
            REDIRS(REDIR(PLAIN_WORD("f"), 9999, 0))),
        CMD_R(WORDS(PLAIN_WORD("a")),
            REDIRS(REDIR(PLAIN_WORD("f"), 1, 1))),
        CMD_R(WORDS(PLAIN_WORD("a")),
            REDIRS(REDIR(PLAIN_WORD("f"), 2, 1))),
        CMD_R(WORDS(PLAIN_WORD("a")),
            REDIRS(REDIR(PLAIN_WORD("f"), 3, 1))),
        CMD_R(WORDS(PLAIN_WORD("a")),
            REDIRS(REDIR(PLAIN_WORD("f"), 9999, 1))),
    ));

    validate(
        "a 1> f | a 2> f | a 3> f | a 9999> f |"
        "a 1>> f | a 2>> f | a 3>> f | a 9999>> f",
        &exp);
}

TEST(parser, multiple_arbitrary_io_number_redirects) {
    exp_ast exp = AST(NOIF(
    CMD_R(WORDS(PLAIN_WORD("a")),
        REDIRS(
            REDIR(PLAIN_WORD("foo"), 1, 0),
            REDIR(PLAIN_WORD("123"), 2, 0),
            REDIR(PLAIN_WORD("bar"), 35, 0),
            REDIR(PLAIN_WORD("log"), 1003, 1)
        )),
    ));

    validate("a 1> foo 2> 123 35>bar 1003>>log", &exp);
}

TEST(parser, io_number_not_consumed_when_white_space) {
    exp_ast exp = AST(NOIF(
    CMD_R(WORDS(PLAIN_WORD("a"), PLAIN_WORD("123")),
        REDIRS(REDIR(PLAIN_WORD("foo"), 1, 0))),
    ));

    validate("a 123 >foo", &exp);
}

TEST(parser, argument_after_redirect) {
    exp_ast exp = AST(NOIF(
    CMD_R(WORDS(PLAIN_WORD("echo"), PLAIN_WORD("tail")),
        REDIRS(REDIR(PLAIN_WORD("out"), 123, 1))),
    ));

    validate("echo 123>>out tail", &exp);

}

TEST(parser, non_append_between_io_number_appends) {
    exp_ast exp = AST(NOIF(
    CMD_R(WORDS(PLAIN_WORD("echo")),
        REDIRS(
            REDIR(PLAIN_WORD("a"), 3, 1),
            REDIR(PLAIN_WORD("b"), 1, 0),
            REDIR(PLAIN_WORD("c"), 4, 1)
        )),
    ));

    validate("echo 3>>a >b 4>>c", &exp);
}

TEST(parser, io_numbers_inside_subshells) {
    exp_ast exp = AST(NOIF(SUBSH(AST(NOIF(
    CMD_R(WORDS(PLAIN_WORD("echo")),
        REDIRS(REDIR(PLAIN_WORD("out"), 123, 1))),
    )))));

    validate("(echo 123>>out)", &exp);
}

TEST(parser, io_number_redirect_subshell_output) {
    exp_ast exp = AST(NOIF(
        SUBSH_R(
            AST(NOIF(CMD(PLAIN_WORD("echo")))),
            REDIRS(REDIR(PLAIN_WORD("out"), 123, 1))
        )
    ));

    validate("(echo) 123>>out", &exp);
}

TEST(parser, andor_connected_commands_after_io_number_redirect) {
    exp_ast exp = AST(
        NOIF(CMD_R(
            WORDS(PLAIN_WORD("echo")),
            REDIRS(REDIR(PLAIN_WORD("out"), 123, 1))
        )),
        ANDIF(CMD(PLAIN_WORD("next")))
    );

    validate("echo 123>>out && next", &exp);
}

TEST(parser, io_number_fail_rejections) {
    validate_fail("echo 123>");
    validate_fail("echo 123>>");
    validate_fail("echo 123>> | next");
    validate_fail("echo 123>> && next");
    validate_fail("echo 123>> >out");
}

/************ Test runner ************/

TEST_GROUP_RUNNER(parser) {
    RUN_TEST_CASE(parser, bg_not_final_token_should_fail);
    RUN_TEST_CASE(parser, redirect_with_no_target_fails);
    RUN_TEST_CASE(parser, no_cmd_redir_should_fail);
    RUN_TEST_CASE(parser, pipe_is_final_token_should_fail);
    RUN_TEST_CASE(parser, andif_is_final_token_should_fail);
    RUN_TEST_CASE(parser, orif_is_final_token_should_fail);
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

    /* subshell */
    RUN_TEST_CASE(parser, subshell_not_in_command_position_should_fail);
    RUN_TEST_CASE(parser, subshell_after_redirect_should_fail);
    RUN_TEST_CASE(parser, subshell_nested_syntax_error_should_fail);
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

    /* alias expansion */
    RUN_TEST_CASE(parser, alias_simple_word);
    RUN_TEST_CASE(parser, alias_multiple_words);
    RUN_TEST_CASE(parser, alias_with_redirection);
    RUN_TEST_CASE(parser, alias_with_multiple_redirections);
    RUN_TEST_CASE(parser, alias_with_pipeline);
    RUN_TEST_CASE(parser, alias_with_andor);
    RUN_TEST_CASE(parser, alias_that_expands_into_pipeline);

    /* variable io numbers */
    RUN_TEST_CASE(parser, append_mode_is_set);
    RUN_TEST_CASE(parser, arbitrary_io_numbers);
    RUN_TEST_CASE(parser, multiple_arbitrary_io_number_redirects);
    RUN_TEST_CASE(parser, io_number_not_consumed_when_white_space);
    RUN_TEST_CASE(parser, argument_after_redirect);
    RUN_TEST_CASE(parser, non_append_between_io_number_appends);
    RUN_TEST_CASE(parser, io_numbers_inside_subshells);
    RUN_TEST_CASE(parser, io_number_redirect_subshell_output);
    RUN_TEST_CASE(parser, andor_connected_commands_after_io_number_redirect);
    RUN_TEST_CASE(parser, io_number_fail_rejections);
}
