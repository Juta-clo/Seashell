#include <unistd.h>
#include <time.h>
#include <stdlib.h>

#include "unity_fixture.h"
#include "lexer.h"

TEST_GROUP(lexer);

/************ Shared utils ************/

void assert_tokens(
        const char *cmd,
        const lx_kind *exp_kind,
        const char **exp_raw,
        const lx_quote *exp_quote,
        size_t exp_size,
        int exp_rv
) {
    da_tok tokens = { 0 };
    TEST_ASSERT_EQUAL(exp_rv, lx_tokenize(cmd, &tokens));
    TEST_ASSERT_EQUAL_size_t(exp_size, tokens.size);

    if (exp_size == 0 || exp_rv != 0) {
        lx_free(&tokens);
        return;
    }

    size_t linear_i = 0;

    for (size_t i = 0; i < tokens.size; ++i) {
        lx_tok *tok = &tokens.data[i];
        TEST_ASSERT_EQUAL(exp_kind[i], tok->kind);

        if (exp_kind[i] == LX_TOK_WORD) {
            TEST_ASSERT_NOT_EQUAL_size_t(0, tok->parts.size);

            for (size_t y = 0; y < tok->parts.size; ++y) {
                lx_part *part = &tok->parts.data[y];
                TEST_ASSERT_EQUAL_STRING(exp_raw[linear_i], part->raw);
                TEST_ASSERT_EQUAL(exp_quote[linear_i], part->quote);
                ++linear_i;
            }
        } else {
            TEST_ASSERT_EQUAL_size_t(0, tok->parts.size);
            ++linear_i;
        }
    }

    lx_free(&tokens);
}

/************ Fixture ************/

TEST_SETUP(lexer) {}
TEST_TEAR_DOWN(lexer) {}

/************ Tests ************/

TEST(lexer, all_operators) {
    const lx_kind kind[8] = {
        LX_TOK_PIPE,
        LX_TOK_BG,
        LX_TOK_RDR_IN,
        LX_TOK_APPEND,
        LX_TOK_RDR_OUT,
        LX_TOK_AND_IF,
        LX_TOK_OR_IF,
        LX_TOK_RDR_ERR,
    };

    assert_tokens("|&<>>>&&||2>", kind, NULL, NULL, 8, 0);
}

TEST(lexer, shell_usage) {
    const lx_kind kind[9] = {
        LX_TOK_WORD, LX_TOK_WORD, LX_TOK_RDR_ERR, LX_TOK_WORD,
        LX_TOK_RDR_OUT, LX_TOK_WORD, LX_TOK_BG, LX_TOK_WORD,
        LX_TOK_WORD
    };
    const char *raw[10] = {
        "solaar", "show", NULL, "file.txt", NULL,
        "weird file", "3", NULL, "cat", "./file.txt"
    };
    const lx_quote quote[10] = {
        LX_Q_NONE, LX_Q_NONE, LX_Q_NONE, LX_Q_NONE, LX_Q_NONE,
        LX_Q_DOUBLE, LX_Q_NONE, LX_Q_NONE, LX_Q_NONE,
        LX_Q_NONE,
    };

    assert_tokens("solaar show 2>file.txt >  \"weird file\"3& cat ./file.txt",
            kind, raw, quote, 9, 0);
}

TEST(lexer, quotes_galore) {
    const lx_kind kind[10] = {
        LX_TOK_WORD, LX_TOK_WORD, LX_TOK_WORD, LX_TOK_WORD,
        LX_TOK_WORD
    };
    const char *raw[9] = {
        "echo", "", "", "a", "", "b", "c", "", "d"
    };
    const lx_quote quote[9] = {
        LX_Q_NONE, LX_Q_SINGLE, LX_Q_DOUBLE, LX_Q_NONE,
        LX_Q_SINGLE, LX_Q_NONE, LX_Q_NONE, LX_Q_DOUBLE, LX_Q_NONE
    };

    assert_tokens("echo '' \"\" a''b c\"\"d",
            kind, raw, quote, 5, 0);
}

/* Whitespaces */

TEST(lexer, whitespace_only) {
    assert_tokens(" ", NULL, NULL, NULL, 0, 0);
}

/* Operators */

TEST(lexer, operator_single) {
    const lx_kind kind[1] = { LX_TOK_PIPE, };
    assert_tokens("|", kind, NULL, NULL, 1, 0);
}

TEST(lexer, operator_multiple) {
    const lx_kind kind[2] = { LX_TOK_PIPE, LX_TOK_BG };
    assert_tokens("|&", kind, NULL, NULL, 2, 0);
}

TEST(lexer, operator_before_word) {
    const lx_kind kind[2] = { LX_TOK_PIPE, LX_TOK_WORD };
    const char *raw[2] = { NULL, "a" };
    const lx_quote quote[2] = { LX_Q_NONE, LX_Q_NONE };
    assert_tokens("|a", kind, raw, quote, 2, 0);
}

TEST(lexer, operator_before_space) {
    const lx_kind kind[1] = { LX_TOK_PIPE };
    assert_tokens("| ", kind, NULL, NULL, 1, 0);
}

TEST(lexer, operator_after_space) {
    const lx_kind kind[1] = { LX_TOK_PIPE };
    assert_tokens(" |", kind, NULL, NULL, 1, 0);
}

TEST(lexer, operator_two_char_op) {
    const lx_kind kind[1] = { LX_TOK_APPEND };
    assert_tokens(">>", kind, NULL, NULL, 1, 0);
}

TEST(lexer, operator_two_char_op_before_word) {
    const lx_kind kind[2] = { LX_TOK_APPEND, LX_TOK_WORD };
    const char *raw[2] = { NULL, "a" };
    const lx_quote quote[2] = { LX_Q_NONE, LX_Q_NONE };
    assert_tokens(">>a", kind, raw, quote, 2, 0);
}

TEST(lexer, operator_two_char_op_after_word) {
    const lx_kind kind[2] = { LX_TOK_WORD, LX_TOK_APPEND };
    const char *raw[2] = { "a",  NULL };
    const lx_quote quote[2] = { LX_Q_NONE, LX_Q_NONE };
    assert_tokens("a>>", kind, raw, quote, 2, 0);
}

TEST(lexer, operator_two_char_op_before_op) {
    const lx_kind kind[2] = { LX_TOK_APPEND, LX_TOK_PIPE };
    assert_tokens(">>|", kind, NULL, NULL, 2, 0);
}

TEST(lexer, operator_two_char_op_after_op) {
    const lx_kind kind[2] = { LX_TOK_PIPE, LX_TOK_APPEND };
    assert_tokens("|>>", kind, NULL, NULL, 2, 0);
}

TEST(lexer, operator_two_char_op_before_whitespace) {
    const lx_kind kind[1] = { LX_TOK_APPEND };
    assert_tokens(">> ", kind, NULL, NULL, 1, 0);
}

TEST(lexer, operator_two_char_op_after_whitespace) {
    const lx_kind kind[1] = { LX_TOK_APPEND };
    assert_tokens(" >>", kind, NULL, NULL, 1, 0);
}

/* Words */

TEST(lexer, word_single_letter) {
    const lx_kind kind[1] = { LX_TOK_WORD, };
    const char *raw[1] = { "a" };
    const lx_quote quote[1] = { LX_Q_NONE };
    assert_tokens("a", kind, raw, quote, 1, 0);
}

TEST(lexer, word_multiple_letter) {
    const lx_kind kind[1] = { LX_TOK_WORD, };
    const char *raw[1] = { "ab" };
    const lx_quote quote[1] = { LX_Q_NONE };
    assert_tokens("ab", kind, raw, quote, 1, 0);
}

TEST(lexer, word_before_operator) {
    const lx_kind kind[2] = { LX_TOK_WORD, LX_TOK_PIPE };
    const char *raw[2] = { "a", NULL };
    const lx_quote quote[2] = { LX_Q_NONE, LX_Q_NONE };
    assert_tokens("a|", kind, raw, quote, 2, 0);
}

TEST(lexer, word_before_space) {
    const lx_kind kind[1] = { LX_TOK_WORD };
    const char *raw[1] = { "a" };
    const lx_quote quote[1] = { LX_Q_NONE };
    assert_tokens("a ", kind, raw, quote, 1, 0);
}

TEST(lexer, word_after_space) {
    const lx_kind kind[1] = { LX_TOK_WORD };
    const char *raw[1] = { "a" };
    const lx_quote quote[1] = { LX_Q_NONE };
    assert_tokens(" a", kind, raw, quote, 1, 0);
}

/* Backslashes */

TEST(lexer, backslash_only) {
    assert_tokens("\\", NULL, NULL, NULL, 0, LX_ERREMPTYESC);
}

TEST(lexer, backslash_after_backslash) {
    const lx_kind kind[1] = { LX_TOK_WORD };
    const char *raw[1] = { "\\\\" };
    const lx_quote quote[1] = { LX_Q_NONE };
    assert_tokens("\\\\", kind, raw, quote, 1, 0);
}

TEST(lexer, backslash_before_operator) {
    const lx_kind kind[1] = { LX_TOK_WORD };
    const char *raw[1] = { "\\|" };
    const lx_quote quote[1] = { LX_Q_NONE };
    assert_tokens("\\|", kind, raw, quote, 1, 0);
}

TEST(lexer, backslash_after_operator) {
    assert_tokens("|\\", NULL, NULL, NULL, 0, LX_ERREMPTYESC);
}

TEST(lexer, backslash_before_whitespace) {
    const lx_kind kind[1] = { LX_TOK_WORD };
    const char *raw[1] = { "\\ " };
    const lx_quote quote[1] = { LX_Q_NONE };
    assert_tokens("\\ ", kind, raw, quote, 1, 0);
}

TEST(lexer, backslash_after_whitespace) {
    assert_tokens(" \\", NULL, NULL, NULL, 0, LX_ERREMPTYESC);
}

TEST(lexer, backslash_before_word) {
    const lx_kind kind[1] = { LX_TOK_WORD };
    const char *raw[1] = { "\\a" };
    const lx_quote quote[1] = { LX_Q_NONE };
    assert_tokens("\\a", kind, raw, quote, 1, 0);
}

TEST(lexer, backslash_after_word) {
    assert_tokens("a\\", NULL, NULL, NULL, 0, LX_ERREMPTYESC);
}

/* Double quotes */

TEST(lexer, double_quote_empty) {
    const lx_kind kind[1] = { LX_TOK_WORD };
    const char *raw[1] = { "" };
    const lx_quote quote[1] = { LX_Q_DOUBLE };
    assert_tokens("\"\"", kind, raw, quote, 1, 0);
}

TEST(lexer, double_quote_word) {
    const lx_kind kind[1] = { LX_TOK_WORD };
    const char *raw[1] = { "a" };
    const lx_quote quote[1] = { LX_Q_DOUBLE };
    assert_tokens("\"a\"", kind, raw, quote, 1, 0);
}

TEST(lexer, double_quote_operator) {
    const lx_kind kind[1] = { LX_TOK_WORD };
    const char *raw[1] = { "|" };
    const lx_quote quote[1] = { LX_Q_DOUBLE };
    assert_tokens("\"|\"", kind, raw, quote, 1, 0);
}

TEST(lexer, double_quote_whitespace) {
    const lx_kind kind[1] = { LX_TOK_WORD };
    const char *raw[1] = { " " };
    const lx_quote quote[1] = { LX_Q_DOUBLE };
    assert_tokens("\" \"", kind, raw, quote, 1, 0);
}

TEST(lexer, double_quote_escaped_quote) {
    const lx_kind kind[1] = { LX_TOK_WORD };
    const char *raw[1] = { "\\\"" };
    const lx_quote quote[1] = { LX_Q_DOUBLE };
    assert_tokens("\"\\\"\"", kind, raw, quote, 1, 0);
}

TEST(lexer, double_quote_no_end_quote) {
    assert_tokens("\"", NULL, NULL, NULL, 0, LX_ERRNOENDQUOTE);
}

/* Single quotes */

TEST(lexer, single_quote_empty) {
    const lx_kind kind[1] = { LX_TOK_WORD };
    const char *raw[1] = { "" };
    const lx_quote quote[1] = { LX_Q_SINGLE };
    assert_tokens("''", kind, raw, quote, 1, 0);
}

TEST(lexer, single_quote_word) {
    const lx_kind kind[1] = { LX_TOK_WORD };
    const char *raw[1] = { "a" };
    const lx_quote quote[1] = { LX_Q_SINGLE };
    assert_tokens("'a'", kind, raw, quote, 1, 0);
}

TEST(lexer, single_quote_operator) {
    const lx_kind kind[1] = { LX_TOK_WORD };
    const char *raw[1] = { "|" };
    const lx_quote quote[1] = { LX_Q_SINGLE };
    assert_tokens("'|'", kind, raw, quote, 1, 0);
}

TEST(lexer, single_quote_whitespace) {
    const lx_kind kind[1] = { LX_TOK_WORD };
    const char *raw[1] = { " " };
    const lx_quote quote[1] = { LX_Q_SINGLE };
    assert_tokens("' '", kind, raw, quote, 1, 0);
}

TEST(lexer, single_quote_backslash) {
    const lx_kind kind[1] = { LX_TOK_WORD };
    const char *raw[1] = { "\\" };
    const lx_quote quote[1] = { LX_Q_SINGLE };
    assert_tokens("'\\'", kind, raw, quote, 1, 0);
}

TEST(lexer, single_quote_no_end_quote) {
    assert_tokens("'", NULL, NULL, NULL, 0, LX_ERRNOENDQUOTE);
}

/* Parts */

TEST(lexer, part_none_then_double) {
    const lx_kind kind[1] = { LX_TOK_WORD };
    const char *raw[2] = { "a", "b" };
    const lx_quote quote[2] = { LX_Q_NONE, LX_Q_DOUBLE };
    assert_tokens("a\"b\"", kind, raw, quote, 1, 0);
}

TEST(lexer, part_double_then_none) {
    const lx_kind kind[1] = { LX_TOK_WORD };
    const char *raw[2] = { "a", "b" };
    const lx_quote quote[2] = { LX_Q_DOUBLE, LX_Q_NONE };
    assert_tokens("\"a\"b", kind, raw, quote, 1, 0);
}

TEST(lexer, part_double_then_double) {
    const lx_kind kind[1] = { LX_TOK_WORD };
    const char *raw[2] = { "a", "b" };
    const lx_quote quote[2] = { LX_Q_DOUBLE, LX_Q_DOUBLE };
    assert_tokens("\"a\"\"b\"", kind, raw, quote, 1, 0);
}

TEST(lexer, part_none_then_single) {
    const lx_kind kind[1] = { LX_TOK_WORD };
    const char *raw[2] = { "a", "b" };
    const lx_quote quote[2] = { LX_Q_NONE, LX_Q_SINGLE };
    assert_tokens("a'b'", kind, raw, quote, 1, 0);
}

TEST(lexer, part_single_then_none) {
    const lx_kind kind[1] = { LX_TOK_WORD };
    const char *raw[2] = { "a", "b" };
    const lx_quote quote[2] = { LX_Q_SINGLE, LX_Q_NONE };
    assert_tokens("'a'b", kind, raw, quote, 1, 0);
}

TEST(lexer, part_single_then_single) {
    const lx_kind kind[1] = { LX_TOK_WORD };
    const char *raw[2] = { "a", "b" };
    const lx_quote quote[2] = { LX_Q_SINGLE, LX_Q_SINGLE };
    assert_tokens("'a''b'", kind, raw, quote, 1, 0);
}

TEST(lexer, part_double_then_single) {
    const lx_kind kind[1] = { LX_TOK_WORD };
    const char *raw[2] = { "a", "b" };
    const lx_quote quote[2] = { LX_Q_DOUBLE, LX_Q_SINGLE };
    assert_tokens("\"a\"'b'", kind, raw, quote, 1, 0);
}

TEST(lexer, part_single_then_double) {
    const lx_kind kind[1] = { LX_TOK_WORD };
    const char *raw[2] = { "a", "b" };
    const lx_quote quote[2] = { LX_Q_SINGLE, LX_Q_DOUBLE };
    assert_tokens("'a'\"b\"", kind, raw, quote, 1, 0);
}

/************ Test runner ************/

TEST_GROUP_RUNNER(lexer) {
    LOG_INFO(""); LOG_INFO("STARTING TEST MODULE: LEXER"); LOG_INFO("");
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
    RUN_TEST_CASE(lexer, word_single_letter);
    RUN_TEST_CASE(lexer, word_multiple_letter);
    RUN_TEST_CASE(lexer, word_before_operator);
    RUN_TEST_CASE(lexer, word_before_space);
    RUN_TEST_CASE(lexer, word_after_space);
    RUN_TEST_CASE(lexer, backslash_only);
    RUN_TEST_CASE(lexer, backslash_after_backslash);
    RUN_TEST_CASE(lexer, backslash_before_operator);
    RUN_TEST_CASE(lexer, backslash_after_operator);
    RUN_TEST_CASE(lexer, backslash_before_whitespace);
    RUN_TEST_CASE(lexer, backslash_after_whitespace);
    RUN_TEST_CASE(lexer, backslash_before_word);
    RUN_TEST_CASE(lexer, backslash_after_word);
    RUN_TEST_CASE(lexer, double_quote_empty);
    RUN_TEST_CASE(lexer, double_quote_word);
    RUN_TEST_CASE(lexer, double_quote_operator);
    RUN_TEST_CASE(lexer, double_quote_whitespace);
    RUN_TEST_CASE(lexer, double_quote_escaped_quote);
    RUN_TEST_CASE(lexer, double_quote_no_end_quote);
    RUN_TEST_CASE(lexer, single_quote_empty);
    RUN_TEST_CASE(lexer, single_quote_word);
    RUN_TEST_CASE(lexer, single_quote_operator);
    RUN_TEST_CASE(lexer, single_quote_whitespace);
    RUN_TEST_CASE(lexer, single_quote_backslash);
    RUN_TEST_CASE(lexer, single_quote_no_end_quote);
    RUN_TEST_CASE(lexer, part_none_then_double);
    RUN_TEST_CASE(lexer, part_double_then_none);
    RUN_TEST_CASE(lexer, part_double_then_double);
    RUN_TEST_CASE(lexer, part_none_then_single);
    RUN_TEST_CASE(lexer, part_single_then_none);
    RUN_TEST_CASE(lexer, part_single_then_single);
    RUN_TEST_CASE(lexer, part_double_then_single);
    RUN_TEST_CASE(lexer, part_single_then_double);
}
