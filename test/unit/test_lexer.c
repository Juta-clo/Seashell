#include <unistd.h>
#include <time.h>
#include <stdlib.h>

#include "unity_fixture.h"
#include "lexer.h"

TEST_GROUP(lexer);

/************ Shared utils ************/

#define I(_i) _i
#define N(_n) _n
#define IO(_io) _io

#define EXP_DA(DA_T, ELEM_T, ...) \
    ((DA_T){ \
        .data = (ELEM_T[]) { __VA_ARGS__ }, \
        .size = sizeof((ELEM_T[]) { __VA_ARGS__ }) / sizeof(ELEM_T) \
     })

#define PART(_raw, _quote, _i, _n) \
    ((lx_part){ \
        .raw = _raw, \
        .quote = _quote, \
        .i = _i, \
        .n = _n, \
    })

#define PARTS(...) \
    EXP_DA(da_part, lx_part, __VA_ARGS__)

#define OP(_kind, _i, _n) \
    ((lx_tok){ \
        .kind = _kind, \
        .i = _i, \
        .n = _n, \
    })

#define IO_NUM(_io_num, _i, _n) \
    ((lx_tok){ \
        .kind = LX_TOK_IO_NUM, \
        .is_num = true, \
        .i = _i, \
        .n = _n, \
        .io_num = _io_num \
    })

#define WORD(_i, _n, _is_num, ...) \
    ((lx_tok){ \
        .kind = LX_TOK_WORD, \
        .is_num = _is_num, \
        .i = _i, \
        .n = _n, \
        .parts = PARTS(__VA_ARGS__), \
    })

#define WORD_NUMBER(_number, _i, _n) \
    WORD(_i, _n, true, PART(#_number, LX_Q_NONE, _i, _n))

#define WORD_SIMPLE(_raw, _i, _n) \
    WORD(_i, _n, false, PART(_raw, LX_Q_NONE, _i, _n))

#define WORD_DOUBLE(_raw, _i, _n) \
    WORD(_i, _n, false, PART(_raw, LX_Q_DOUBLE, _i + 1, _n - 1))

#define WORD_SINGLE(_raw, _i, _n) \
    WORD(_i, _n, false, PART(_raw, LX_Q_SINGLE, _i + 1, _n - 1))

#define TOKS(...) \
    EXP_DA(da_tok, lx_tok, __VA_ARGS__)

#define STRINGIFY(str) #str

da_tok actual_tokens;

void validate_part(const lx_part *exp, const lx_part *act) {
    TEST_ASSERT_EQUAL_STRING(exp->raw, act->raw);
    TEST_ASSERT_EQUAL(exp->quote, act->quote);
    TEST_ASSERT_EQUAL_INT(exp->i, act->i);
    TEST_ASSERT_EQUAL_INT(exp->n, act->n);
}

void validate_tok(const lx_tok *exp, const lx_tok *act) {
    TEST_ASSERT_EQUAL(exp->kind, act->kind);
    TEST_ASSERT_EQUAL(exp->is_num, act->is_num);
    TEST_ASSERT_EQUAL(exp->i, act->i);
    TEST_ASSERT_EQUAL(exp->n, act->n);

    if (exp->kind == LX_TOK_WORD) {
        TEST_ASSERT_EQUAL_size_t(exp->parts.size, act->parts.size);

        for (size_t i = 0; i < exp->parts.size; ++i) {
            const lx_part *exp_part = &exp->parts.data[i];
            const lx_part *act_part = &act->parts.data[i];

            validate_part(exp_part, act_part);
        }
    } else if (exp->kind == LX_TOK_IO_NUM) {
        TEST_ASSERT_EQUAL_INT(exp->io_num, act->io_num);
    }
}

void validate_toks(const da_tok *exp, const da_tok *act) {
    TEST_ASSERT_EQUAL_size_t(exp->size, act->size);

    for (size_t i = 0; i < exp->size; ++i) {
        const lx_tok *exp_tok = &exp->data[i];
        const lx_tok *act_tok = &act->data[i];

        validate_tok(exp_tok, act_tok);
    }
}

void validate(const char *cmd, const da_tok *exp) {
    TEST_ASSERT_EQUAL_INT(LX_OK, lx_tokenize(cmd, &actual_tokens));

    validate_toks(exp, &actual_tokens);
}

void validate_fail(const char *cmd, const da_tok *exp, int status) {
    TEST_ASSERT_EQUAL_INT(status, lx_tokenize(cmd, &actual_tokens));

    validate_toks(exp, &actual_tokens);
    lx_free(&actual_tokens);
}

/************ Fixture ************/

TEST_SETUP(lexer) {
    actual_tokens = (da_tok){0};
}

TEST_TEAR_DOWN(lexer) {
    lx_free(&actual_tokens);
}

/************ Tests ************/

TEST(lexer, failure_cases) {
    da_tok exp = TOKS(WORD_SIMPLE("\\", I(0), N(1)));
    validate_fail("\\", &exp, LX_ERREMPTYESC);

    exp = TOKS(
        OP(LX_TOK_PIPE, I(0), N(1)),
        WORD_SIMPLE("\\", I(1), N(1))
    );
    validate_fail("|\\", &exp, LX_ERREMPTYESC);

    exp = TOKS(WORD_SIMPLE("\\", I(1), N(1)));
    validate_fail(" \\", &exp, LX_ERREMPTYESC);

    exp = TOKS(WORD_SIMPLE("a\\", I(0), N(2)));
    validate_fail("a\\", &exp, LX_ERREMPTYESC);

    exp = TOKS(
        WORD(I(0), N(1), false, PART("", LX_Q_DOUBLE, I(1), N(0)))
    );
    validate_fail("\"", &exp, LX_ERRNOENDQUOTE);

    exp = TOKS(
        WORD(I(0), N(1), false, PART("", LX_Q_SINGLE, I(1), N(0)))
    );
    validate_fail("'", &exp, LX_ERRNOENDQUOTE);
}

TEST(lexer, all_operators) {
    da_tok exp = TOKS(
        OP(LX_TOK_AND_IF, I(0), N(2)),
        OP(LX_TOK_OR_IF, I(2), N(2)),
        OP(LX_TOK_PIPE, I(4), N(1)),
        OP(LX_TOK_AMPER, I(5), N(1)),
        OP(LX_TOK_RDR_IN, I(6), N(1)),
        OP(LX_TOK_APPEND, I(7), N(2)),
        OP(LX_TOK_RDR_OUT, I(9), N(1)),
        OP(LX_TOK_FD_IN, I(10), N(2)),
        OP(LX_TOK_FD_OUT, I(12), N(2)),
    );

    validate("&&|||&<>>><&>&", &exp);
}

TEST(lexer, shell_usage) {
    da_tok exp = TOKS(
        WORD_SIMPLE("solaar", I(0), N(6)),
        WORD_SIMPLE("show", I(7), N(4)),
        OP(LX_TOK_RDR_OUT, I(12), N(1)),
        WORD(I(14), N(13), false,
            PART("weird file", LX_Q_DOUBLE, I(15), N(10)),
            PART("3", LX_Q_NONE, I(26), N(1)),
        ),
        OP(LX_TOK_AMPER, I(27), N(1)),
        WORD_SIMPLE("cat", I(29), N(3)),
        WORD_SIMPLE("./file.txt", I(33), N(10)),
    );

    validate("solaar show > \"weird file\"3& cat ./file.txt", &exp);
}

TEST(lexer, quotes_galore) {
    da_tok exp = TOKS(
        WORD_SIMPLE("echo", I(0), N(4)),
        WORD(I(5), N(2), false, PART("", LX_Q_SINGLE, I(6), N(0))),
        WORD(I(8), N(2), false, PART("", LX_Q_DOUBLE, I(9), N(0))),
        WORD(I(11), N(4), false,
            PART("a", LX_Q_NONE, I(11), N(1)),
            PART("", LX_Q_SINGLE, I(13), N(0)),
            PART("b", LX_Q_NONE, I(14), N(1)),
        ),
        WORD(I(16), N(4), false,
            PART("c", LX_Q_NONE, I(16), N(1)),
            PART("", LX_Q_DOUBLE, I(18), N(0)),
            PART("d", LX_Q_NONE, I(19), N(1)),
        ),
    );

    validate("echo '' \"\" a''b c\"\"d", &exp);
}

TEST(lexer, whitespace_only) {
    da_tok exp = TOKS();

    validate(" ", &exp);
}

TEST(lexer, operator_single) {
    da_tok exp = TOKS(
        OP(LX_TOK_PIPE, I(0), N(1)),
    );

    validate("|", &exp);
}

TEST(lexer, operator_multiple) {
    da_tok exp = TOKS(
        OP(LX_TOK_PIPE, I(0), N(1)),
        OP(LX_TOK_AMPER, I(1), N(1)),
    );

    validate("|&", &exp);
}

TEST(lexer, operator_before_word) {
    da_tok exp = TOKS(
        OP(LX_TOK_PIPE, I(0), N(1)),
        WORD_SIMPLE("a", I(1), N(1)),
    );

    validate("|a", &exp);
}

TEST(lexer, operator_before_space) {
    da_tok exp = TOKS(
        OP(LX_TOK_PIPE, I(0), N(1)),
    );

    validate("| ", &exp);
}

TEST(lexer, operator_after_space) {
    da_tok exp = TOKS(
        OP(LX_TOK_PIPE, I(1), N(1)),
    );

    validate(" |", &exp);
}

TEST(lexer, operator_two_char_op) {
    da_tok exp = TOKS(
        OP(LX_TOK_APPEND, I(0), N(2)),
    );

    validate(">>", &exp);
}

TEST(lexer, operator_two_char_op_before_word) {
    da_tok exp = TOKS(
        OP(LX_TOK_APPEND, I(0), N(2)),
        WORD_SIMPLE("a", I(2), N(1)),
    );

    validate(">>a", &exp);
}

TEST(lexer, operator_two_char_op_after_word) {
    da_tok exp = TOKS(
        WORD_SIMPLE("a", I(0), N(1)),
        OP(LX_TOK_APPEND, I(1), N(2)),
    );

    validate("a>>", &exp);
}

TEST(lexer, operator_two_char_op_before_op) {
    da_tok exp = TOKS(
        OP(LX_TOK_APPEND, I(0), N(2)),
        OP(LX_TOK_PIPE, I(2), N(1)),
    );

    validate(">>|", &exp);
}

TEST(lexer, operator_two_char_op_after_op) {
    da_tok exp = TOKS(
        OP(LX_TOK_PIPE, I(0), N(1)),
        OP(LX_TOK_APPEND, I(1), N(2)),
    );

    validate("|>>", &exp);
}

TEST(lexer, operator_two_char_op_before_whitespace) {
    da_tok exp = TOKS(
        OP(LX_TOK_APPEND, I(0), N(2)),
    );

    validate(">> ", &exp);
}

TEST(lexer, operator_two_char_op_after_whitespace) {
    da_tok exp = TOKS(
        OP(LX_TOK_APPEND, I(1), N(2)),
    );

    validate(" >>", &exp);
}

TEST(lexer, operator_left_and_right_paren) {
    da_tok exp = TOKS(
        OP(LX_TOK_LPAREN, I(0), N(1)),
        OP(LX_TOK_RPAREN, I(1), N(1)),
        OP(LX_TOK_RPAREN, I(2), N(1)),
        OP(LX_TOK_LPAREN, I(3), N(1)),
    );

    validate("())(", &exp);
}

TEST(lexer, word_single_letter) {
    da_tok exp = TOKS(
        WORD_SIMPLE("a", I(0), N(1)),
    );

    validate("a", &exp);
}

TEST(lexer, word_multiple_letter) {
    da_tok exp = TOKS(
        WORD_SIMPLE("ab", I(0), N(2)),
    );

    validate("ab", &exp);
}

TEST(lexer, word_before_operator) {
    da_tok exp = TOKS(
        WORD_SIMPLE("a", I(0), N(1)),
        OP(LX_TOK_PIPE, I(1), N(1)),
    );

    validate("a|", &exp);
}

TEST(lexer, word_before_space) {
    da_tok exp = TOKS(
        WORD_SIMPLE("a", I(0), N(1)),
    );

    validate("a ", &exp);
}

TEST(lexer, word_after_space) {
    da_tok exp = TOKS(
        WORD_SIMPLE("a", I(1), N(1)),
    );

    validate(" a", &exp);
}

TEST(lexer, backslash_after_backslash) {
    da_tok exp = TOKS(
        WORD_SIMPLE("\\\\", I(0), N(2)),
    );

    validate("\\\\", &exp);
}

TEST(lexer, backslash_before_operator) {
    da_tok exp = TOKS(
        WORD_SIMPLE("\\|", I(0), N(2)),
    );

    validate("\\|", &exp);
}

TEST(lexer, backslash_before_whitespace) {
    da_tok exp = TOKS(
        WORD_SIMPLE("\\ ", I(0), N(2)),
    );

    validate("\\ ", &exp);
}

TEST(lexer, backslash_before_word) {
    da_tok exp = TOKS(
        WORD_SIMPLE("\\a", I(0), N(2)),
    );

    validate("\\a", &exp);
}

TEST(lexer, double_quote_empty) {
    da_tok exp = TOKS(
        WORD(I(0), N(2), false, PART("", LX_Q_DOUBLE, I(1), N(0))),
    );

    validate("\"\"", &exp);
}

TEST(lexer, double_quote_word) {
    da_tok exp = TOKS(
        WORD(I(0), N(3), false, PART("a", LX_Q_DOUBLE, I(1), N(1))),
    );

    validate("\"a\"", &exp);
}

TEST(lexer, double_quote_operator) {
    da_tok exp = TOKS(
        WORD(I(0), N(3), false, PART("|", LX_Q_DOUBLE, I(1), N(1))),
    );

    validate("\"|\"", &exp);
}

TEST(lexer, double_quote_whitespace) {
    da_tok exp = TOKS(
        WORD(I(0), N(3), false, PART(" ", LX_Q_DOUBLE, I(1), N(1))),
    );

    validate("\" \"", &exp);
}

TEST(lexer, double_quote_escaped_quote) {
    da_tok exp = TOKS(
        WORD(I(0), N(4), false,
            PART("\\\"", LX_Q_DOUBLE, I(1), N(2))
        ),
    );

    validate("\"\\\"\"", &exp);
}

TEST(lexer, single_quote_empty) {
    da_tok exp = TOKS(
        WORD(I(0), N(2), false, PART("", LX_Q_SINGLE, I(1), N(0))),
    );

    validate("''", &exp);
}

TEST(lexer, single_quote_word) {
    da_tok exp = TOKS(
        WORD(I(0), N(3), false, PART("a", LX_Q_SINGLE, I(1), N(1))),
    );

    validate("'a'", &exp);
}

TEST(lexer, single_quote_operator) {
    da_tok exp = TOKS(
        WORD(I(0), N(3), false, PART("|", LX_Q_SINGLE, I(1), N(1))),
    );

    validate("'|'", &exp);
}

TEST(lexer, single_quote_whitespace) {
    da_tok exp = TOKS(
        WORD(I(0), N(3), false, PART(" ", LX_Q_SINGLE, I(1), N(1))),
    );

    validate("' '", &exp);
}

TEST(lexer, single_quote_backslash) {
    da_tok exp = TOKS(
        WORD(I(0), N(3), false, PART("\\", LX_Q_SINGLE, I(1), N(1))),
    );

    validate("'\\'", &exp);
}

TEST(lexer, part_none_then_double) {
    da_tok exp = TOKS(
        WORD(I(0), N(4), false,
            PART("a", LX_Q_NONE, I(0), N(1)),
            PART("b", LX_Q_DOUBLE, I(2), N(1)),
        ),
    );

    validate("a\"b\"", &exp);
}

TEST(lexer, part_double_then_none) {
    da_tok exp = TOKS(
        WORD(I(0), N(4), false,
            PART("a", LX_Q_DOUBLE, I(1), N(1)),
            PART("b", LX_Q_NONE, I(3), N(1)),
        ),
    );

    validate("\"a\"b", &exp);
}

TEST(lexer, part_double_then_double) {
    da_tok exp = TOKS(
        WORD(I(0), N(6), false,
            PART("a", LX_Q_DOUBLE, I(1), N(1)),
            PART("b", LX_Q_DOUBLE, I(4), N(1)),
        ),
    );

    validate("\"a\"\"b\"", &exp);
}

TEST(lexer, part_none_then_single) {
    da_tok exp = TOKS(
        WORD(I(0), N(4), false,
            PART("a", LX_Q_NONE, I(0), N(1)),
            PART("b", LX_Q_SINGLE, I(2), N(1)),
        ),
    );

    validate("a'b'", &exp);
}

TEST(lexer, part_single_then_none) {
    da_tok exp = TOKS(
        WORD(I(0), N(4), false,
            PART("a", LX_Q_SINGLE, I(1), N(1)),
            PART("b", LX_Q_NONE, I(3), N(1)),
        ),
    );

    validate("'a'b", &exp);
}

TEST(lexer, part_single_then_single) {
    da_tok exp = TOKS(
        WORD(I(0), N(6), false,
            PART("a", LX_Q_SINGLE, I(1), N(1)),
            PART("b", LX_Q_SINGLE, I(4), N(1)),
        ),
    );

    validate("'a''b'", &exp);
}

TEST(lexer, part_double_then_single) {
    da_tok exp = TOKS(
        WORD(I(0), N(6), false,
            PART("a", LX_Q_DOUBLE, I(1), N(1)),
            PART("b", LX_Q_SINGLE, I(4), N(1)),
        ),
    );

    validate("\"a\"'b'", &exp);
}

TEST(lexer, part_single_then_double) {
    da_tok exp = TOKS(
        WORD(I(0), N(6), false,
            PART("a", LX_Q_SINGLE, I(1), N(1)),
            PART("b", LX_Q_DOUBLE, I(4), N(1)),
        ),
    );

    validate("'a'\"b\"", &exp);
}

TEST(lexer, classifies_words_that_are_numbers) {
    da_tok exp = TOKS(
        WORD_SIMPLE("123a", I(0), N(4)),
        WORD_SIMPLE("a123", I(5), N(4)),
        WORD_NUMBER(123, I(10), N(3)),
        WORD(I(14), N(5), false, PART("123", LX_Q_DOUBLE, I(15), N(3))),
        WORD(I(20), N(6), false,
            PART("123", LX_Q_DOUBLE, I(21), N(3)),
            PART("a", LX_Q_NONE, I(25), N(1)),
        ),
        WORD(I(27), N(6), false,
            PART("123", LX_Q_NONE, I(27), N(3)),
            PART("a", LX_Q_DOUBLE, I(31), N(1)),
        ),
        WORD(I(34), N(6), false,
            PART("123", LX_Q_NONE, I(34), N(3)),
            PART("5", LX_Q_SINGLE, I(38), N(1)),
        ),
    );

    validate("123a a123 123 \"123\" \"123\"a 123\"a\" 123'5'", &exp);
}

TEST(lexer, numbers_interspersed_with_tokens) {
    da_tok exp = TOKS(
        OP(LX_TOK_PIPE, I(0), N(1)),
        WORD_NUMBER(123, I(1), N(3)),
        OP(LX_TOK_AMPER, I(4), N(1)),
        WORD_NUMBER(456, I(5), N(3)),
        OP(LX_TOK_OR_IF, I(8), N(2)),
        WORD_NUMBER(789, I(10), N(3)),
        OP(LX_TOK_AND_IF, I(13), N(2)),
    );

    validate("|123&456||789&&", &exp);
}

TEST(lexer, copied_number_tokens_maintain_data) {
    lx_tok src = WORD_NUMBER(123, I(5), N(3));
    lx_tok dest;

    lx_tok_copy(&dest, &src);
    validate_tok(&src, &dest);
    lx_free_tok(&dest);
}

TEST(lexer, io_number_tokens_only_created_before_redirect) {
    da_tok exp = TOKS(
        IO_NUM(IO(123), I(0), N(3)),
        OP(LX_TOK_RDR_OUT, I(3), N(1)),
        OP(LX_TOK_RDR_OUT, I(5), N(1)),
        WORD_NUMBER(123, I(6), N(3)),
        IO_NUM(IO(12), I(10), N(2)),
        OP(LX_TOK_RDR_OUT, I(12), N(1)),
        WORD_NUMBER(34, I(13), N(2)),
        WORD(I(16), N(5), false, PART("123", LX_Q_DOUBLE, I(17), N(3))),
        OP(LX_TOK_RDR_OUT, I(21), N(1)),
        OP(LX_TOK_RDR_OUT, I(23), N(1)),
        WORD(I(24), N(5), false, PART("123", LX_Q_DOUBLE, I(25), N(3))),
    );

    validate("123> >123 12>34 \"123\"> >\"123\"", &exp);
}

TEST(lexer, io_number_that_overflows_integer) {
    da_tok exp = TOKS(
        WORD_NUMBER(2147483648, I(0), N(10)),
        OP(LX_TOK_RDR_OUT, I(10), N(1)),
    );

    validate("2147483648>", &exp);
}

TEST(lexer, signed_io_numbers_are_rejected) {
    da_tok exp = TOKS(
        WORD_NUMBER(-1, I(0), N(2)),
        OP(LX_TOK_RDR_OUT, I(2), N(1)),
        WORD_NUMBER(+1, I(4), N(2)),
        OP(LX_TOK_RDR_OUT, I(6), N(1)),
    );

    validate("-1> +1>", &exp);
}

TEST(lexer, redirect_in_with_arbitrary_io_numbers) {
    da_tok exp = TOKS(
        IO_NUM(IO(1), I(0), N(1)),
        OP(LX_TOK_RDR_IN, I(1), N(1)),
        WORD_SIMPLE("file", I(2), N(4)),
        IO_NUM(IO(2), I(7), N(1)),
        OP(LX_TOK_RDR_IN, I(8), N(1)),
        WORD_NUMBER(999, I(10), N(3))
    );

    validate("1<file 2< 999", &exp);
}

TEST(lexer, fd_duplication) {
    da_tok exp = TOKS(
        IO_NUM(IO(1), I(0), N(1)),
        OP(LX_TOK_FD_OUT, I(1), N(2)),
        IO_NUM(IO(25), I(3), N(2)),
        OP(LX_TOK_FD_IN, I(5), N(2)),
        IO_NUM(IO(8), I(7), N(1)),
        OP(LX_TOK_RDR_OUT, I(8), N(1)),
        IO_NUM(IO(5), I(9), N(1)),
        OP(LX_TOK_FD_IN, I(10), N(2)),
        IO_NUM(IO(8), I(12), N(1)),
    );

    validate("1>&25<&8>5<&8", &exp);
}

TEST(lexer, word_after_fd_duplication_token) {
    da_tok exp = TOKS(
        OP(LX_TOK_FD_OUT, I(0), N(2)),
        WORD_SIMPLE("hello", I(2), N(5))
    );

    validate(">&hello", &exp);
}

TEST(lexer, white_space_between_fd_out_and_io_number) {
    da_tok exp = TOKS(
        OP(LX_TOK_FD_OUT, I(0), N(2)),
        IO_NUM(IO(123), I(5), N(3))
    );

    validate(">&   123", &exp);
}

TEST(lexer, operator_token_spans_are_preserved) {
    da_tok exp = TOKS(
        OP(LX_TOK_PIPE, I(0), N(1)),
        OP(LX_TOK_FD_IN, I(2), N(2)),
        OP(LX_TOK_AND_IF, I(5), N(2)),
        OP(LX_TOK_PIPE, I(7), N(1)),
        OP(LX_TOK_AND_IF, I(8), N(2)),
        OP(LX_TOK_OR_IF, I(10), N(2)),
    );

    validate("| <& &&|&&||", &exp);
}

TEST(lexer, word_token_span_is_preserved) {
    da_tok exp = TOKS(
        WORD(I(0), N(7), false,
            PART("a", LX_Q_NONE, I(0), N(1)),
            PART("b", LX_Q_DOUBLE, I(2), N(1)),
            PART("c", LX_Q_SINGLE, I(5), N(1)),
        ),
        WORD_SIMPLE("abc", I(8), N(3))
    );

    validate("a\"b\"'c' abc", &exp);
}

TEST(lexer, mixed_operators_and_tokens_span_preservation) {
    da_tok exp = TOKS(
        WORD_SIMPLE("abc", I(0), N(3)),
        WORD_NUMBER(123, I(4), N(3)),
        OP(LX_TOK_OR_IF, I(7), N(2)),
        WORD(I(9), N(6), false,
            PART("ab", LX_Q_SINGLE, I(10), N(2)),
            PART("cd", LX_Q_NONE, I(13), N(2)),
        ),
        OP(LX_TOK_AMPER, I(15), N(1))
    );

    validate("abc 123||'ab'cd&", &exp);
}

TEST(lexer, unfinished_parts_are_completed) {
    da_tok exp = TOKS(
        WORD_DOUBLE("abc", I(0), N(4)),
    );

    validate_fail("\"abc", &exp, LX_ERRNOENDQUOTE);
}

/************ Test runner ************/

TEST_GROUP_RUNNER(lexer) {
    RUN_TEST_CASE(lexer, failure_cases);
    RUN_TEST_CASE(lexer, all_operators);
    RUN_TEST_CASE(lexer, shell_usage);
    RUN_TEST_CASE(lexer, quotes_galore);
    RUN_TEST_CASE(lexer, whitespace_only);
    RUN_TEST_CASE(lexer, operator_single);
    RUN_TEST_CASE(lexer, operator_multiple);
    RUN_TEST_CASE(lexer, operator_before_word);
    RUN_TEST_CASE(lexer, operator_before_space);
    RUN_TEST_CASE(lexer, operator_after_space);
    RUN_TEST_CASE(lexer, operator_two_char_op);
    RUN_TEST_CASE(lexer, operator_two_char_op_before_word);
    RUN_TEST_CASE(lexer, operator_two_char_op_after_word);
    RUN_TEST_CASE(lexer, operator_two_char_op_before_op);
    RUN_TEST_CASE(lexer, operator_two_char_op_after_op);
    RUN_TEST_CASE(lexer, operator_two_char_op_before_whitespace);
    RUN_TEST_CASE(lexer, operator_two_char_op_after_whitespace);
    RUN_TEST_CASE(lexer, operator_left_and_right_paren);
    RUN_TEST_CASE(lexer, word_single_letter);
    RUN_TEST_CASE(lexer, word_multiple_letter);
    RUN_TEST_CASE(lexer, word_before_operator);
    RUN_TEST_CASE(lexer, word_before_space);
    RUN_TEST_CASE(lexer, word_after_space);
    RUN_TEST_CASE(lexer, backslash_after_backslash);
    RUN_TEST_CASE(lexer, backslash_before_operator);
    RUN_TEST_CASE(lexer, backslash_before_whitespace);
    RUN_TEST_CASE(lexer, backslash_before_word);
    RUN_TEST_CASE(lexer, double_quote_empty);
    RUN_TEST_CASE(lexer, double_quote_word);
    RUN_TEST_CASE(lexer, double_quote_operator);
    RUN_TEST_CASE(lexer, double_quote_whitespace);
    RUN_TEST_CASE(lexer, double_quote_escaped_quote);
    RUN_TEST_CASE(lexer, single_quote_empty);
    RUN_TEST_CASE(lexer, single_quote_word);
    RUN_TEST_CASE(lexer, single_quote_operator);
    RUN_TEST_CASE(lexer, single_quote_whitespace);
    RUN_TEST_CASE(lexer, single_quote_backslash);
    RUN_TEST_CASE(lexer, part_none_then_double);
    RUN_TEST_CASE(lexer, part_double_then_none);
    RUN_TEST_CASE(lexer, part_double_then_double);
    RUN_TEST_CASE(lexer, part_none_then_single);
    RUN_TEST_CASE(lexer, part_single_then_none);
    RUN_TEST_CASE(lexer, part_single_then_single);
    RUN_TEST_CASE(lexer, part_double_then_single);
    RUN_TEST_CASE(lexer, part_single_then_double);
    RUN_TEST_CASE(lexer, classifies_words_that_are_numbers);
    RUN_TEST_CASE(lexer, copied_number_tokens_maintain_data);
    RUN_TEST_CASE(lexer, io_number_tokens_only_created_before_redirect);
    RUN_TEST_CASE(lexer, io_number_that_overflows_integer);
    RUN_TEST_CASE(lexer, signed_io_numbers_are_rejected);
    RUN_TEST_CASE(lexer, redirect_in_with_arbitrary_io_numbers);
    RUN_TEST_CASE(lexer, fd_duplication);
    RUN_TEST_CASE(lexer, word_after_fd_duplication_token);
    RUN_TEST_CASE(lexer, white_space_between_fd_out_and_io_number);
    RUN_TEST_CASE(lexer, numbers_interspersed_with_tokens);
    RUN_TEST_CASE(lexer, operator_token_spans_are_preserved);
    RUN_TEST_CASE(lexer, word_token_span_is_preserved);
    RUN_TEST_CASE(lexer, mixed_operators_and_tokens_span_preservation);
    RUN_TEST_CASE(lexer, unfinished_parts_are_completed);
}
