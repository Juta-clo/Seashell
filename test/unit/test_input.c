#include <assert.h>

#include "llog.h"
#include "unity_fixture.h"
#include "input.h"

TEST_GROUP(input);

/************ Fixture ************/

const char *line;
input_word word;

void expect_word(int start, int end) {
    TEST_ASSERT_EQUAL_INT(start, word.start);
    TEST_ASSERT_EQUAL_INT(end, word.end);
}

TEST_SETUP(input) {
    line = NULL;
    word = (input_word){0};
}

TEST_TEAR_DOWN(input) {
}

/************ Tests ************/

TEST(input, next_word_on_non_ws) {
    next_word("this is a line", 0, &word);
    expect_word(5, 6);
}

TEST(input, next_word_on_ws) {
    next_word(" this is a line", 0, &word);
    expect_word(1, 4);
}

TEST(input, next_word_when_there_is_none) {
    TEST_ASSERT_EQUAL_INT(-1, next_word("this is a line ", 14, &word));
    TEST_ASSERT_EQUAL_INT(-1, next_word("this is a line", 13, &word));
}

TEST(input, prev_word_on_non_ws) {
    prev_word("this is a line", 4, &word);
    expect_word(0, 3);
}

TEST(input, prev_word_on_ws) {
    prev_word("this is a line", 6, &word);
    expect_word(0, 3);
}

TEST(input, prev_word_when_there_is_none) {
    TEST_ASSERT_EQUAL_INT(-1, prev_word("this is a line ", 0, &word));
    TEST_ASSERT_EQUAL_INT(-1, prev_word(" this is a line", 0, &word));
}

TEST(input, cur_word) {
    cur_word("this is a line", 5, &word);
    expect_word(5, 6);
}

TEST(input, cur_word_when_there_is_none) {
    TEST_ASSERT_EQUAL_INT(-1, cur_word("this is a line ", 4, &word));
}

/************ Test runner ************/

TEST_GROUP_RUNNER(input) {
    LOG_INFO(""); LOG_INFO("STARTING TEST MODULE: INPUT"); LOG_INFO("");
    RUN_TEST_CASE(input, next_word_on_non_ws);
    RUN_TEST_CASE(input, next_word_on_ws);
    RUN_TEST_CASE(input, next_word_when_there_is_none);
    RUN_TEST_CASE(input, prev_word_on_non_ws);
    RUN_TEST_CASE(input, prev_word_on_ws);
    RUN_TEST_CASE(input, prev_word_when_there_is_none);
    RUN_TEST_CASE(input, cur_word);
    RUN_TEST_CASE(input, cur_word_when_there_is_none);
}
