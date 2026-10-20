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

    if (chdir(dir->pathname) == -1)
        xtest_fatal("chdir: %m");

    if (close(open(name, O_CREAT | O_RDWR, 0600)) == -1)
        xtest_fatal("close: %m");

    char *p = strdup(name);
    if (!p)
        xtest_fatal("strdup: %m");

    *da_push(&dir->files) = p;

    if (chdir(GLOB_DIR) == -1)
        xtest_fatal("chdir: %m");
}

glob_dir *add_dir(const char *pathname) {
    if (strlen(pathname) >= PATH_MAX - 1)
        TEST_FAIL_MESSAGE("file name too large");

    if (mkdir(pathname, 0700) == -1)
        xtest_fatal("mkdir: %m");

    glob_dir *dir = dir_next();
    snprintf(dir->pathname, PATH_MAX, "%s", pathname);
    dir->files = (da_charp){0};

    return dir;
}

void file_cleanup(da_charp *files) {
    for (size_t i = 0; i < files->size; ++i) {
        if (unlink(files->data[i]) == -1)
            xtest_fatal("unlink: %m");
        free(files->data[i]);
    }

    da_free(files);
}

void dir_cleanup(void) {
    for (int i = dir_n - 1; i >= 0; --i) {
        if (chdir(dirs[i].pathname) == -1)
            xtest_fatal("chdir: %m");

        file_cleanup(&dirs[i].files);

        if (chdir(GLOB_DIR) == -1)
            xtest_fatal("chdir: %m");

        if (rmdir(dirs[i].pathname) == -1)
            xtest_fatal("rmdir: %m");
    }
}

TEST_SETUP(glob) {
    dir_n = 0;

    if (!getcwd(og_cwd, PATH_MAX))
        xtest_fatal("getcwd");

    add_dir(GLOB_DIR);

    if (chdir(GLOB_DIR) == -1)
        xtest_fatal("chdir: %m");

    test_setup();
}

TEST_TEAR_DOWN(glob) {
    test_teardown();
    dir_cleanup();
    if (chdir(og_cwd) == -1)
        xtest_fatal("chdir: %m");
}

/************ Tests ************/

TEST(glob, glob_star_only) {
    add_file(CWD, "a");

    vt_send_str("/bin/echo *\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "/bin/echo *",
        "./a",
        PROMPT
    );

    vt_send_str("/bin/echo ./*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "/bin/echo ./*",
        "./a",
        PROMPT
    );
}

TEST(glob, prefix_match) {
    add_file(CWD, "hello");

    add_file(CWD, "harold");
    add_file(CWD, "peal");

    vt_send_str("/bin/echo he*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "/bin/echo he*",
        "./hello",
        PROMPT
    );
}

TEST(glob, suffix_match) {
    add_file(CWD, "horn");
    add_file(CWD, "boon");
    add_file(CWD, "swear");

    vt_send_str("/bin/echo *rn\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "/bin/echo *rn",
        "./horn",
        PROMPT
    );
}

TEST(glob, suffix_and_prefix_match) {
    add_file(CWD, "aba");

    add_file(CWD, "aca");
    add_file(CWD, "aaa");

    vt_send_str("/bin/echo *b*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "/bin/echo *b*",
        "./aba",
        PROMPT
    );
}

TEST(glob, single_substring_match) {
    add_file(CWD, "boofooboo");
    add_file(CWD, "boofoboo");
    add_file(CWD, "boobooboo");

    vt_send_str("/bin/echo *foo*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "/bin/echo *foo*",
        "./boofooboo",
        PROMPT
    );
}

TEST(glob, multiple_substring_match) {
    add_file(CWD, "XfooXbarX");

    add_file(CWD, "XfoXbarX");
    add_file(CWD, "XfooXarX");

    vt_send_str("/bin/echo *foo*bar*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "/bin/echo *foo*bar*",
        "./XfooXbarX",
        PROMPT
    );
}

TEST(glob, substring_and_prefix_match) {
    add_file(CWD, "fooXbarX");
    add_file(CWD, "fobar");
    add_file(CWD, "XbarX");
    add_file(CWD, "XfoobarXXX");

    vt_send_str("/bin/echo foo*bar*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "/bin/echo foo*bar*",
        "./fooXbarX",
        PROMPT
    );
}

TEST(glob, substring_and_suffix_match) {
    add_file(CWD, "XfooXbar");
    add_file(CWD, "fooba");
    add_file(CWD, "foXbar");
    add_file(CWD, "foobarX");

    vt_send_str("/bin/echo *foo*bar\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "/bin/echo *foo*bar",
        "./XfooXbar",
        PROMPT
    );
}

TEST(glob, multiple_substring_prefix_suffix_match) {
    add_file(CWD, "fooXbarXbeeXzoo");
    add_file(CWD, "Xfoobarbeezoo");
    add_file(CWD, "foobarbeezooX");
    add_file(CWD, "XXXfoobarbeezoo");
    add_file(CWD, "foobarbeezooXXX");
    add_file(CWD, "XfooXbarXbeeXzooX");

    vt_send_str("/bin/echo foo*bar*bee*zoo\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "/bin/echo foo*bar*bee*zoo",
        "./fooXbarXbeeXzoo",
        PROMPT
    );
}

TEST(glob, nested_directory_match) {
    glob_dir *dir = add_dir("dir1");
    add_file(dir, "foo");

    vt_send_str("/bin/echo ./dir1/*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "/bin/echo ./dir1/*",
        "./dir1/foo",
        PROMPT
    );
}

TEST(glob, double_nested_directory_match) {
    add_dir("dir1");
    add_file(add_dir("dir1/dir2"), "foo");

    vt_send_str("/bin/echo ./dir1/dir2/*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "/bin/echo ./dir1/dir2/*",
        "./dir1/dir2/foo",
        PROMPT
    );
}

TEST(glob, directory_componenet_with_glob) {
    add_dir("d-a");
    add_file(add_dir("dir2"), "foo");
    add_file(add_dir("dir"), "foo");

    vt_send_str("/bin/echo ./dir*/foo\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "/bin/echo ./dir*/foo",
        "./dir/foo ./dir2/foo",
        PROMPT
    );
}

TEST(glob, directory_and_file_componenet_with_glob) {
    add_dir("d-a");
    add_file(add_dir("dir2"), "foo");
    add_file(add_dir("dir"), "foo");

    vt_send_str("/bin/echo ./dir*/*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "/bin/echo ./dir*/*",
        "./dir/foo ./dir2/foo",
        PROMPT
    );
}

TEST(glob, directory_glob_and_file_prefix_match) {
    add_dir("d-a");
    add_file(add_dir("dir2"), "foo");
    add_file(add_dir("dir"), "foo");

    vt_send_str("/bin/echo ./dir*/f*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "/bin/echo ./dir*/f*",
        "./dir/foo ./dir2/foo",
        PROMPT
    );
}

TEST(glob, directory_glob_and_file_suffix_match) {
    add_dir("d-a");
    add_file(add_dir("dir2"), "foo");
    add_file(add_dir("dir"), "foo");

    vt_send_str("/bin/echo ./dir*/*o\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "/bin/echo ./dir*/*o",
        "./dir/foo ./dir2/foo",
        PROMPT
    );
}

TEST(glob, double_nested_directory_globs) {
    add_dir("abc");
    add_dir("a12");

    add_file(add_dir("abc/def"), "foo");
    add_file(add_dir("a12/d34"), "foo");

    vt_send_str("/bin/echo ./a*/d*/*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "/bin/echo ./a*/d*/*",
        "./a12/d34/foo ./abc/def/foo",
        PROMPT
    );
}

TEST(glob, multiple_stars_are_compressed) {
    add_file(add_dir("abc"), "foo");

    vt_send_str("/bin/echo ./***/*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "/bin/echo ./***/*",
        "./abc/foo",
        PROMPT
    );
}

TEST(glob, intermediary_component_with_complex_glob_pattern) {
    add_dir("a");
    add_file(add_dir("a/fooXbarXbeeXzoo"), "foo");

    vt_send_str("/bin/echo ./a/foo*bar*bee*zoo/*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "/bin/echo ./a/foo*bar*bee*zoo/*",
        "./a/fooXbarXbeeXzoo/foo",
        PROMPT
    );
}

TEST(glob, relative_dirent_works) {
    add_file(add_dir("a"), "foo");
    add_dir("a/b");

    vt_send_str("/bin/echo a/b/../*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "/bin/echo a/b/../*",
        "./a/b/../b ./a/b/../foo",
        PROMPT
    );
}

TEST(glob, redirect_target_glob_non_ambiguous) {
    add_file(CWD, "foo");

    vt_send_str("echo hi > f*\n");
    assert_term(1, PROMPT_LEN,
        PROMPT "echo hi > f*",
        PROMPT
    );

    char buf[3];
    int fd = open("foo", O_RDONLY);
    if (fd == -1)
        xtest_fatal("open: %m");

    TEST_ASSERT_EQUAL_INT(2, read(fd, buf, 2));
    TEST_ASSERT_EQUAL_STRING_LEN("hi", buf, 2);
}

TEST(glob, redirect_target_glob_ambiguous) {
    add_file(CWD, "foo1");
    add_file(CWD, "foo2");

    vt_send_str("echo hi > f*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo hi > f*",
        "seashell: ambiguous redirect",
        PROMPT
    );
}

TEST(glob, glob_detected_in_empty_variable) {
    vt_send_str("set FOO \"\"\n");
    assert_term(1, PROMPT_LEN,
        PROMPT "set FOO \"\"",
        PROMPT
    );

    vt_send_str("echo $FOO*\n");
    assert_term(3, PROMPT_LEN,
        PROMPT "set FOO \"\"",
        PROMPT "echo $FOO*",
        "",
        PROMPT
    );
}

/************ Test runner ************/

TEST_GROUP_RUNNER(glob) {
    RUN_TEST_CASE(glob, glob_star_only);
    RUN_TEST_CASE(glob, prefix_match);
    RUN_TEST_CASE(glob, suffix_match);
    RUN_TEST_CASE(glob, suffix_and_prefix_match);
    RUN_TEST_CASE(glob, single_substring_match);
    RUN_TEST_CASE(glob, substring_and_prefix_match);
    RUN_TEST_CASE(glob, substring_and_suffix_match);
    RUN_TEST_CASE(glob, multiple_substring_match);
    RUN_TEST_CASE(glob, multiple_substring_prefix_suffix_match);
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
    RUN_TEST_CASE(glob, redirect_target_glob_non_ambiguous);
    RUN_TEST_CASE(glob, redirect_target_glob_ambiguous);
    RUN_TEST_CASE(glob, glob_detected_in_empty_variable);
}
