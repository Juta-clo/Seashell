#include <assert.h>
#include <sys/stat.h>

#include "unity_fixture.h"
#include "pty_test.h"

TEST_GROUP(glob);

/************ Fixture ************/

#define GLOB_DIR "./glob_dir/"

da_charp files = {0};

void add_file(const char *name) {
    xclose(xopen(name, O_CREAT | O_RDWR, 0600));
    *da_push(&files) = xstrdup(name);
}

void dir_cleanup(void) {
    for (size_t i = 0; i < files.size; ++i) {
        xunlink(files.data[i]);
        free(files.data[i]);
    }

    da_free(&files);
    xrmdir(GLOB_DIR);
}

TEST_SETUP(glob) {
    xmkdir(GLOB_DIR, 0700);
    new_shell();
}

TEST_TEAR_DOWN(glob) {
    delete_shell();
    dir_cleanup();
}

/************ Tests ************/

TEST(glob, glob_all_files) {
    add_file("a");
    add_file("b");
    add_file("c");

    pty_send("/bin/echo ./glob_dir/*\n");
    pty_ensure("a b c\n");
}

/************ Test runner ************/

TEST_GROUP_RUNNER(glob) {
    RUN_TEST_CASE(glob, glob_all_files);
}
