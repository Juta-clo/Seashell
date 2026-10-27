#include <stdio.h>

#include "t_assert.h"
#include "unity_fixture.h"

TEST_GROUP(config);

/************ Fixture ************/

FILE *config;

void create_config(const char *content) {
    config = fopen("/tmp/config_test.lua", "w+");
    if (!config)
        xtest_fatal("fopen: %m");

    if (fprintf(config, "%s", content) < 0)
        xtest_fatal("fprintf: %m");

    if (fclose(config) == EOF)
        xtest_fatal("fclose: %m");
}

TEST_SETUP(config) {
    config = nullptr;
}

TEST_TEAR_DOWN(config) {
    test_teardown();

    if (system("rm /tmp/config_test.lua") != 0)
        xtest_fatal("system(rm) failed");
}

/************ Tests ************/

// NOLINTBEGIN(bugprone-suspicious-missing-comma)

TEST(config, option_config_file_read) {
    create_config(
        "return {\n"
            "prompt = function()\n"
                "return \"$ \"\n"
            "end\n"
        "}\n"
    );

    test_setup("./seashell", "--config-file=/tmp/config_test.lua");
    assert_term(0, 2, "$ ");
}

TEST(config, escapes_in_prompt_are_handled) {
    create_config(
        "return {\n"
            "prompt = function()\n"
                "return \"\x1b[31m$\x1b[m \"\n"
            "end\n"
        "}\n"
    );

    test_setup("./seashell", "--config-file=/tmp/config_test.lua");
    assert_term(0, 2,
        "$ "
    );

    vt_send_str("echo x\n");
    assert_term(2, 2,
        "$ echo x",
        "x",
        "$ "
    );
}

// NOLINTEND(bugprone-suspicious-missing-comma)

/************ Test runner ************/

TEST_GROUP_RUNNER(config) {
    RUN_TEST_CASE(config, option_config_file_read);
    RUN_TEST_CASE(config, escapes_in_prompt_are_handled);
}
