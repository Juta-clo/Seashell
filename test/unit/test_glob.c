#include <assert.h>

#include "unity_fixture.h"
#include "path_glob.h"
#include <linc/array.h>

TEST_GROUP(glob);

/************ Fixture ************/

da_charp matches = {0};

TEST_SETUP(glob) {
    matches = (da_charp){0};
}

TEST_TEAR_DOWN(glob) {
    for (size_t i = 0; i < matches.size; ++i)
        free(matches.data[i]);

    linc_array_free(&matches);
}

/************ Tests ************/

TEST(glob, relative_glob) {
    TEST_ASSERT_EQUAL_INT(0, glob_pattern("READ*", 5, &matches));
    TEST_ASSERT_EQUAL_size_t(1, matches.size);
    TEST_ASSERT_EQUAL_STRING("./README.md", matches.data[0]);
}

TEST(glob, absolute_glob) {
    TEST_ASSERT_EQUAL_INT(0, glob_pattern("/usr/bin/ech*", 13, &matches));
    TEST_ASSERT_EQUAL_size_t(1, matches.size);
    TEST_ASSERT_EQUAL_STRING("/usr/bin/echo", matches.data[0]);
}

TEST(glob, no_matches_with_exact_match_pattern) {
    TEST_ASSERT_EQUAL_INT(0, glob_pattern("READ", 4, &matches));
    TEST_ASSERT_EQUAL_size_t(0, matches.size);
}

TEST(glob, multiple_matches) {
    TEST_ASSERT_EQUAL_INT(0, glob_pattern("d*", 2, &matches));
    TEST_ASSERT_EQUAL_size_t(1, matches.size);
    TEST_ASSERT_EQUAL_STRING("./deps", matches.data[0]);
}

TEST(glob, prefix_suffix_match) {
    TEST_ASSERT_EQUAL_INT(0, glob_pattern("*EAD*", 5, &matches));
    TEST_ASSERT_EQUAL_size_t(1, matches.size);
    TEST_ASSERT_EQUAL_STRING("./README.md", matches.data[0]);
}

/************ Test runner ************/

TEST_GROUP_RUNNER(glob) {
    RUN_TEST_CASE(glob, relative_glob);
    RUN_TEST_CASE(glob, absolute_glob);
    RUN_TEST_CASE(glob, no_matches_with_exact_match_pattern);
    RUN_TEST_CASE(glob, multiple_matches);
    RUN_TEST_CASE(glob, prefix_suffix_match);
}
