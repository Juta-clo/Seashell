#include <unistd.h>
#include <time.h>
#include <stdlib.h>

#include "unity_fixture.h"
#include "lexer.h"

TEST_GROUP(lexer);

/************ Shared utils ************/

#define STRINGIFY(str) #str

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
    const lx_kind kind[9] = {
        LX_TOK_AND_IF,
        LX_TOK_OR_IF,
        LX_TOK_PIPE,
        LX_TOK_AMPER,
        LX_TOK_RDR_IN,
        LX_TOK_APPEND,
        LX_TOK_RDR_OUT,
        LX_TOK_FD_IN,
        LX_TOK_FD_OUT,
    };

    assert_tokens("&&|||&<>>><&>&", kind, NULL, NULL, 9, 0);
}

TEST(lexer, shell_usage) {
    const lx_kind kind[7] = {
        LX_TOK_WORD, LX_TOK_WORD,
        LX_TOK_RDR_OUT, LX_TOK_WORD, LX_TOK_AMPER, LX_TOK_WORD,
        LX_TOK_WORD
    };
    const char *raw[8] = {
        "solaar", "show", NULL,
        "weird file", "3", NULL, "cat", "./file.txt"
    };
    const lx_quote quote[8] = {
        LX_Q_NONE, LX_Q_NONE, LX_Q_NONE,
        LX_Q_DOUBLE, LX_Q_NONE, LX_Q_NONE, LX_Q_NONE,
        LX_Q_NONE,
    };

    assert_tokens("solaar show > \"weird file\"3& cat ./file.txt",
            kind, raw, quote, 7, 0);
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
    const lx_kind kind[2] = { LX_TOK_PIPE, LX_TOK_AMPER };
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

TEST(lexer, operator_left_and_right_paren) {
    const lx_kind kind[4] = {
        LX_TOK_LPAREN, LX_TOK_RPAREN, LX_TOK_RPAREN, LX_TOK_LPAREN
    };
    assert_tokens("())(", kind, NULL, NULL, 4, 0);
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

TEST(lexer, classifies_words_that_are_numbers) {
    da_tok toks;

    if (lx_tokenize(
        "123a a123 123 \"123\" \"123\"a 123\"a\" 123'5'",
        &toks
    ) != LX_OK)
        TEST_FAIL_MESSAGE("lx_tokenize() failed");

    TEST_ASSERT_EQUAL_size_t(7, toks.size);

    for (size_t i = 0; i < 7; ++i)
        TEST_ASSERT_EQUAL_INT(LX_TOK_WORD, toks.data[i].kind);

    TEST_ASSERT_FALSE(toks.data[0].is_num);
    TEST_ASSERT_FALSE(toks.data[1].is_num);
    TEST_ASSERT(toks.data[2].is_num);
    TEST_ASSERT_FALSE(toks.data[3].is_num);
    TEST_ASSERT_FALSE(toks.data[4].is_num);
    TEST_ASSERT_FALSE(toks.data[5].is_num);
    TEST_ASSERT_FALSE(toks.data[6].is_num);

    lx_free(&toks);
}

TEST(lexer, numbers_interspersed_with_tokens) {
    da_tok toks;

    if (lx_tokenize("|123&456||789&&", &toks) != LX_OK)
        TEST_FAIL_MESSAGE("lx_tokenize() failed");

    TEST_ASSERT_EQUAL_size_t(7, toks.size);
    TEST_ASSERT_EQUAL_INT(LX_TOK_PIPE, toks.data[0].kind);
    TEST_ASSERT_EQUAL_INT(LX_TOK_WORD, toks.data[1].kind);
    TEST_ASSERT(toks.data[1].is_num);
    TEST_ASSERT_EQUAL_INT(LX_TOK_AMPER, toks.data[2].kind);
    TEST_ASSERT_EQUAL_INT(LX_TOK_WORD, toks.data[3].kind);
    TEST_ASSERT(toks.data[3].is_num);
    TEST_ASSERT_EQUAL_INT(LX_TOK_OR_IF, toks.data[4].kind);
    TEST_ASSERT_EQUAL_INT(LX_TOK_WORD, toks.data[5].kind);
    TEST_ASSERT(toks.data[5].is_num);
    TEST_ASSERT_EQUAL_INT(LX_TOK_AND_IF, toks.data[6].kind);

    lx_free(&toks);
}

TEST(lexer, copied_number_tokens_maintain_data) {
    lx_tok src = {
        .kind = LX_TOK_WORD,
        .is_num = true
    };

    lx_tok dest;
    lx_tok_copy(&dest, &src);

    TEST_ASSERT_EQUAL_INT(LX_TOK_WORD, dest.kind);
    TEST_ASSERT(dest.is_num);
}

TEST(lexer, io_number_tokens_only_created_before_redirect) {
    da_tok toks;

    if (lx_tokenize("123> >123 12>34 \"123\"> >\"123\"", &toks) != LX_OK)
        TEST_FAIL_MESSAGE("lx_tokenize() failed");

    TEST_ASSERT_EQUAL_size_t(11, toks.size);

    TEST_ASSERT_EQUAL_INT(LX_TOK_IO_NUM, toks.data[0].kind);
    TEST_ASSERT_EQUAL_INT(123, toks.data[0].io_num);

    TEST_ASSERT_EQUAL_INT(LX_TOK_RDR_OUT, toks.data[1].kind);
    TEST_ASSERT_EQUAL_INT(LX_TOK_RDR_OUT, toks.data[2].kind);

    TEST_ASSERT_EQUAL_INT(LX_TOK_WORD, toks.data[3].kind);
    TEST_ASSERT(toks.data[3].is_num);

    TEST_ASSERT_EQUAL_INT(LX_TOK_IO_NUM, toks.data[4].kind);
    TEST_ASSERT_EQUAL_INT(12, toks.data[4].io_num);

    TEST_ASSERT_EQUAL_INT(LX_TOK_RDR_OUT, toks.data[5].kind);

    TEST_ASSERT_EQUAL_INT(LX_TOK_WORD, toks.data[6].kind);
    TEST_ASSERT(toks.data[6].is_num);

    TEST_ASSERT_EQUAL_INT(LX_TOK_WORD, toks.data[7].kind);
    TEST_ASSERT_FALSE(toks.data[7].is_num);

    TEST_ASSERT_EQUAL_INT(LX_TOK_RDR_OUT, toks.data[8].kind);
    TEST_ASSERT_EQUAL_INT(LX_TOK_RDR_OUT, toks.data[9].kind);

    TEST_ASSERT_EQUAL_INT(LX_TOK_WORD, toks.data[10].kind);
    TEST_ASSERT_FALSE(toks.data[10].is_num);

    lx_free(&toks);
}

TEST(lexer, io_number_that_overflows_integer){
    da_tok toks;

    if (lx_tokenize("2147483648>", &toks) != LX_OK)
        TEST_FAIL_MESSAGE("lx_tokenize() failed");

    TEST_ASSERT_EQUAL_INT(LX_TOK_WORD, toks.data[0].kind);
    TEST_ASSERT(toks.data[0].is_num);
    TEST_ASSERT_EQUAL_STRING("2147483648", toks.data[0].parts.data[0].raw);

    TEST_ASSERT_EQUAL_INT(LX_TOK_RDR_OUT, toks.data[1].kind);

    lx_free(&toks);
}

TEST(lexer, signed_io_numbers_are_rejected) {
    da_tok toks;

    if (lx_tokenize("-1> +1>", &toks) != LX_OK)
        TEST_FAIL_MESSAGE("lx_tokenize() failed");

    TEST_ASSERT_EQUAL_INT(LX_TOK_WORD, toks.data[0].kind);
    TEST_ASSERT_EQUAL_STRING("-1", toks.data[0].parts.data[0].raw);
    TEST_ASSERT_EQUAL_INT(LX_TOK_RDR_OUT, toks.data[1].kind);

    TEST_ASSERT_EQUAL_INT(LX_TOK_WORD, toks.data[2].kind);
    TEST_ASSERT_EQUAL_STRING("+1", toks.data[2].parts.data[0].raw);
    TEST_ASSERT_EQUAL_INT(LX_TOK_RDR_OUT, toks.data[3].kind);

    lx_free(&toks);
}

TEST(lexer, redirect_in_with_arbitrary_io_numbers) {
    da_tok toks;

    if (lx_tokenize("1<file 2< 999", &toks) != LX_OK)
        TEST_FAIL_MESSAGE("lx_tokenize() failed");

    TEST_ASSERT_EQUAL_INT(LX_TOK_IO_NUM, toks.data[0].kind);
    TEST_ASSERT_EQUAL_INT(1, toks.data[0].io_num);
    TEST_ASSERT_EQUAL_INT(LX_TOK_RDR_IN, toks.data[1].kind);
    TEST_ASSERT_EQUAL_INT(LX_TOK_WORD, toks.data[2].kind);
    TEST_ASSERT_EQUAL_STRING("file", toks.data[2].parts.data[0].raw);

    TEST_ASSERT_EQUAL_INT(LX_TOK_IO_NUM, toks.data[3].kind);
    TEST_ASSERT_EQUAL_INT(2, toks.data[3].io_num);
    TEST_ASSERT_EQUAL_INT(LX_TOK_RDR_IN, toks.data[4].kind);
    TEST_ASSERT_EQUAL_INT(LX_TOK_WORD, toks.data[5].kind);
    TEST_ASSERT_EQUAL_STRING("999", toks.data[5].parts.data[0].raw);

    lx_free(&toks);
}

TEST(lexer, fd_duplication) {
    da_tok toks;

    if (lx_tokenize("1>&25<&8>5<&8", &toks) != LX_OK)
        TEST_FAIL_MESSAGE("lx_tokenize() failed");

    TEST_ASSERT_EQUAL_size_t(9, toks.size);

    TEST_ASSERT_EQUAL_INT(LX_TOK_IO_NUM, toks.data[0].kind);
    TEST_ASSERT_EQUAL_INT(1, toks.data[0].io_num);
    TEST_ASSERT_EQUAL_INT(LX_TOK_FD_OUT, toks.data[1].kind);
    TEST_ASSERT_EQUAL_INT(LX_TOK_IO_NUM, toks.data[2].kind);
    TEST_ASSERT_EQUAL_INT(25, toks.data[2].io_num);
    TEST_ASSERT_EQUAL_INT(LX_TOK_FD_IN, toks.data[3].kind);
    TEST_ASSERT_EQUAL_INT(LX_TOK_IO_NUM, toks.data[4].kind);
    TEST_ASSERT_EQUAL_INT(8, toks.data[4].io_num);
    TEST_ASSERT_EQUAL_INT(LX_TOK_RDR_OUT, toks.data[5].kind);
    TEST_ASSERT_EQUAL_INT(LX_TOK_IO_NUM, toks.data[6].kind);
    TEST_ASSERT_EQUAL_INT(5, toks.data[6].io_num);
    TEST_ASSERT_EQUAL_INT(LX_TOK_FD_IN, toks.data[7].kind);
    TEST_ASSERT_EQUAL_INT(LX_TOK_IO_NUM, toks.data[8].kind);
    TEST_ASSERT_EQUAL_INT(8, toks.data[8].io_num);

    lx_free(&toks);
}

TEST(lexer, word_after_fd_duplication_token) {
    da_tok toks;

    if (lx_tokenize(">&hello", &toks) != LX_OK)
        TEST_FAIL_MESSAGE("lx_tokenize() failed");

    TEST_ASSERT_EQUAL_size_t(2, toks.size);

    TEST_ASSERT_EQUAL_INT(LX_TOK_FD_OUT, toks.data[0].kind);
    TEST_ASSERT_EQUAL_INT(LX_TOK_WORD, toks.data[1].kind);
    TEST_ASSERT_EQUAL_STRING("hello", toks.data[1].parts.data[0].raw);

    lx_free(&toks);
}

TEST(lexer, white_space_between_fd_out_and_io_number) {
    da_tok toks;

    if (lx_tokenize(">&   123", &toks) != LX_OK)
        TEST_FAIL_MESSAGE("lx_tokenize() failed");

    TEST_ASSERT_EQUAL_size_t(2, toks.size);

    TEST_ASSERT_EQUAL_INT(LX_TOK_FD_OUT, toks.data[0].kind);
    TEST_ASSERT_EQUAL_INT(LX_TOK_IO_NUM, toks.data[1].kind);
    TEST_ASSERT_EQUAL_INT(123, toks.data[1].io_num);

    lx_free(&toks);
}

/************ Test runner ************/

TEST_GROUP_RUNNER(lexer) {
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
}
