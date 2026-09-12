#include <assert.h>
#include <string.h>
#include <sys/stat.h>

#include "unity_fixture.h"
#include "pty_test.h"

TEST_GROUP(glob);

/************ Fixture ************/

typedef struct {
    char pathname[PATH_MAX];
    da_charp files;
} glob_dir;

#define GLOB_DIR "/tmp/glob_dir/"
#define MAX_DIRS 1000
#define CWD &dirs[0]

static char og_cwd[PATH_MAX];
static size_t dir_n;
static glob_dir dirs[MAX_DIRS];

glob_dir *dir_next(void) {
    if (dir_n >= MAX_DIRS - 1)
        TEST_FAIL_MESSAGE("ran out of directories");

    return &dirs[dir_n++];
}

void add_file(glob_dir *dir, const char *name) {
    if (strlen(name) >= PATH_MAX - 1)
        TEST_FAIL_MESSAGE("file name too large");

    xchdir(dir->pathname);

    xclose(xopen(name, O_CREAT | O_RDWR, 0600));
    *da_push(&dir->files) = xstrdup(name);

    xchdir(GLOB_DIR);
}

glob_dir *add_dir(const char *pathname) {
    if (strlen(pathname) >= PATH_MAX - 1)
        TEST_FAIL_MESSAGE("file name too large");

    xmkdir(pathname, 0700);

    glob_dir *dir = dir_next();
    snprintf(dir->pathname, PATH_MAX, "%s", pathname);
    dir->files = (da_charp){0};

    return dir;
}

void file_cleanup(da_charp *files) {
    for (size_t i = 0; i < files->size; ++i) {
        xunlink(files->data[i]);
        free(files->data[i]);
    }

    da_free(files);
}

void dir_cleanup(void) {
    for (int i = dir_n - 1; i >= 0; --i) {
        xchdir(dirs[i].pathname);
        file_cleanup(&dirs[i].files);
        xchdir(GLOB_DIR);

        xrmdir(dirs[i].pathname);
    }
}

TEST_SETUP(glob) {
    dir_n = 0;

    xgetcwd(og_cwd, PATH_MAX);
    add_dir(GLOB_DIR);
    xchdir(GLOB_DIR);

    new_shell();
}

TEST_TEAR_DOWN(glob) {
    delete_shell();
    dir_cleanup();
    xchdir(og_cwd);
}

/************ Tests ************/

TEST(glob, glob_star_only) {
    add_file(CWD, "a");

    pty_send("/bin/echo *\n");
    pty_ensure("./a\n");
    pty_ensure(PROMPT);
    pty_send("/bin/echo ./*\n");
    pty_ensure("./a\n");
}

TEST(glob, prefix_match) {
    add_file(CWD, "hello");

    add_file(CWD, "harold");
    add_file(CWD, "peal");

    pty_send("/bin/echo he*\n");
    pty_ensure("./hello\n");
    pty_ensure(PROMPT);
    pty_send("/bin/echo ./he*\n");
    pty_ensure("./hello\n");
}

TEST(glob, suffix_match) {
    add_file(CWD, "horn");

    add_file(CWD, "boon");
    add_file(CWD, "swear");

    pty_send("/bin/echo *rn\n");
    pty_ensure("./horn\n");
    pty_ensure(PROMPT);
    pty_send("/bin/echo ./*rn\n");
    pty_ensure("./horn\n");
}

TEST(glob, suffix_and_prefix_match) {
    add_file(CWD, "aba");

    add_file(CWD, "aca");
    add_file(CWD, "aaa");

    pty_send("/bin/echo *b*\n");
    pty_ensure("./aba\n");
    pty_ensure(PROMPT);
    pty_send("/bin/echo ./*b*\n");
    pty_ensure("./aba\n");
}

TEST(glob, single_substring_match) {
    add_file(CWD, "boofooboo");

    add_file(CWD, "boofoboo");
    add_file(CWD, "boobooboo");

    pty_send("/bin/echo *foo*\n");
    pty_ensure("./boofooboo\n");
    pty_ensure(PROMPT);
    pty_send("/bin/echo ./*foo*\n");
    pty_ensure("./boofooboo\n");
}

TEST(glob, multiple_substring_match) {
    add_file(CWD, "XfooXbarX");

    add_file(CWD, "XfoXbarX");
    add_file(CWD, "XfooXarX");

    pty_send("/bin/echo *foo*bar*\n");
    pty_ensure("./XfooXbarX\n");
    pty_ensure(PROMPT);
    pty_send("/bin/echo ./*foo*bar*\n");
    pty_ensure("./XfooXbarX\n");
}

TEST(glob, substring_and_prefix_match) {
    add_file(CWD, "fooXbarX");

    add_file(CWD, "fobar");
    add_file(CWD, "XbarX");
    add_file(CWD, "XfoobarXXX");

    pty_send("/bin/echo foo*bar*\n");
    pty_ensure("./fooXbarX\n");
    pty_ensure(PROMPT);
    pty_send("/bin/echo ./foo*bar*\n");
    pty_ensure("./fooXbarX\n");
}

TEST(glob, substring_and_suffix_match) {
    add_file(CWD, "XfooXbar");
    add_file(CWD, "fooba");
    add_file(CWD, "foXbar");
    add_file(CWD, "foobarX");

    pty_send("/bin/echo *foo*bar\n");
    pty_ensure("./XfooXbar\n");
    pty_ensure(PROMPT);
    pty_send("/bin/echo ./*foo*bar\n");
    pty_ensure("./XfooXbar\n");
}

TEST(glob, multiple_substring_prefix_suffix_match) {
    add_file(CWD, "fooXbarXbeeXzoo");

    add_file(CWD, "Xfoobarbeezoo");
    add_file(CWD, "foobarbeezooX");
    add_file(CWD, "XXXfoobarbeezoo");
    add_file(CWD, "foobarbeezooXXX");
    add_file(CWD, "XfooXbarXbeeXzooX");

    pty_send("/bin/echo foo*bar*bee*zoo\n");
    pty_ensure("./fooXbarXbeeXzooX\n");
    pty_ensure(PROMPT);
    pty_send("/bin/echo ./foo*bar*bee*zoo\n");
    pty_ensure("./fooXbarXbeeXzooX\n");
}

TEST(glob, nested_directory_match) {
    glob_dir *dir = add_dir("dir1");
    add_file(dir, "foo");

    pty_send("/bin/echo ./dir1/*\n");
    pty_ensure("./dir1/foo\n");
    pty_ensure(PROMPT);
}

TEST(glob, double_nested_directory_match) {
    add_dir("dir1");
    add_file(add_dir("dir1/dir2"), "foo");

    pty_send("/bin/echo ./dir1/dir2/*\n");
    pty_ensure("./dir1/dir2/foo\n");
    pty_ensure(PROMPT);
}

TEST(glob, directory_componenet_with_glob) {
    add_dir("d-a");
    add_file(add_dir("dir2"), "foo");
    add_file(add_dir("dir"), "foo");

    pty_send("/bin/echo ./dir*/foo\n");
    pty_ensure("./dir/foo ./dir2/foo\n");
    pty_ensure(PROMPT);
}

TEST(glob, directory_and_file_componenet_with_glob) {
    add_dir("d-a");
    add_file(add_dir("dir2"), "foo");
    add_file(add_dir("dir"), "foo");

    pty_send("/bin/echo ./dir*/*\n");
    pty_ensure("./dir/foo ./dir2/foo\n");
    pty_ensure(PROMPT);
}

TEST(glob, directory_glob_and_file_prefix_match) {
    add_dir("d-a");
    add_file(add_dir("dir2"), "foo");
    add_file(add_dir("dir"), "foo");

    pty_send("/bin/echo ./dir*/f*\n");
    pty_ensure("./dir/foo ./dir2/foo\n");
    pty_ensure(PROMPT);
}

TEST(glob, directory_glob_and_file_suffix_match) {
    add_dir("d-a");
    add_file(add_dir("dir2"), "foo");
    add_file(add_dir("dir"), "foo");

    pty_send("/bin/echo ./dir*/*o\n");
    pty_ensure("./dir/foo ./dir2/foo\n");
    pty_ensure(PROMPT);
}

TEST(glob, double_nested_directory_globs) {
    add_dir("abc");
    add_dir("a12");

    add_file(add_dir("abc/def"), "foo");
    add_file(add_dir("a12/d34"), "foo");

    pty_send("/bin/echo ./a*/d*/*\n");
    pty_ensure("./a12/d34/foo ./abc/def/foo\n");
    pty_ensure(PROMPT);
}

TEST(glob, multiple_stars_are_compressed) {
    add_file(add_dir("abc"), "foo");

    pty_send("/bin/echo ./***/*\n");
    pty_ensure("./abc/foo\n");
    pty_ensure(PROMPT);
}

TEST(glob, intermediary_component_with_complex_glob_pattern) {
    add_dir("a");
    add_file(add_dir("a/fooXbarXbeeXzoo"), "foo");

    pty_send("/bin/echo ./a/foo*bar*bee*zoo/*\n");
    pty_ensure("./a/fooXbarXbeeXzoo/foo\n");
    pty_ensure(PROMPT);
}

TEST(glob, relative_dirent_works) {
    add_file(add_dir("a"), "foo");
    add_dir("a/b");

    pty_send("/bin/echo a/b/../*\n");
    pty_ensure("./a/b/../b ./a/b/../foo\n");
    pty_ensure(PROMPT);
}

/************ Test runner ************/

TEST_GROUP_RUNNER(glob) {
    /* component matching */
    RUN_TEST_CASE(glob, glob_star_only);
    RUN_TEST_CASE(glob, prefix_match);
    RUN_TEST_CASE(glob, suffix_match);
    RUN_TEST_CASE(glob, suffix_and_prefix_match);
    RUN_TEST_CASE(glob, single_substring_match);
    RUN_TEST_CASE(glob, multiple_substring_match);
    RUN_TEST_CASE(glob, substring_and_prefix_match);
    RUN_TEST_CASE(glob, substring_and_suffix_match);
    RUN_TEST_CASE(glob, multiple_substring_match);
    RUN_TEST_CASE(glob, multiple_substring_match);

    /* directory traversal */
    RUN_TEST_CASE(glob, nested_directory_match);
    RUN_TEST_CASE(glob, double_nested_directory_match);
    RUN_TEST_CASE(glob, directory_componenet_with_glob);
    RUN_TEST_CASE(glob, directory_and_file_componenet_with_glob);
    RUN_TEST_CASE(glob, directory_glob_and_file_prefix_match);
    RUN_TEST_CASE(glob, directory_glob_and_file_suffix_match);
    RUN_TEST_CASE(glob, double_nested_directory_globs);
    RUN_TEST_CASE(glob, intermediary_component_with_complex_glob_pattern);
    RUN_TEST_CASE(glob, multiple_stars_are_compressed);
    RUN_TEST_CASE(glob, relative_dirent_works);
}
