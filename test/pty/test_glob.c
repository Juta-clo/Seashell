#include <assert.h>
#include <sys/stat.h>

#include "unity_fixture.h"
#include "pty_test.h"

TEST_GROUP(glob);

/************ Fixture ************/

#define GLOB_DIR "/tmp/glob_dir/"

#define xsystem(cmd) \
    do { \
        if (system(cmd) != 0) \
            xtest_fatal("system(" #cmd ")"); \
    } while (false)

typedef struct {
    char pathname[PATH_MAX];
    da_charp files;
} glob_dir;

static void add_file(const char *path) {
    dstr full_path;
    dstr_init(&full_path);

    dstrcat(&full_path, "touch ");
    dstrcat(&full_path, GLOB_DIR);
    dstrcat(&full_path, path);
    xsystem(full_path.c_str);
    dstr_free(&full_path);
}

static void add_dir(const char *path) {
    dstr full_path;
    dstr_init(&full_path);

    dstrcat(&full_path, "mkdir -p ");
    dstrcat(&full_path, GLOB_DIR);
    dstrcat(&full_path, path);
    xsystem(full_path.c_str);
    dstr_free(&full_path);
}

TEST_SETUP(glob) {
    test_setup();
    xsystem("mkdir -p " GLOB_DIR);

    vt_send_str("cd " GLOB_DIR "\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "cd " GLOB_DIR,
        PROMPT
    );
}

TEST_TEAR_DOWN(glob) {
    test_teardown();
    xsystem("rm -rf /tmp/glob_dir");
}

/************ Tests ************/

TEST(glob, glob_star_only) {
    add_file("a");

    vt_send_str("echo *\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "echo *",
        "./a",
        PROMPT
    );

    vt_send_str("echo ./*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo ./*",
        "./a",
        PROMPT
    );
}

TEST(glob, prefix_match) {
    add_file("hello");

    add_file("harold");
    add_file("peal");

    vt_send_str("echo he*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo he*",
        "./hello",
        PROMPT
    );
}

TEST(glob, suffix_match) {
    add_file("horn");
    add_file("boon");
    add_file("swear");

    vt_send_str("echo *rn\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo *rn",
        "./horn",
        PROMPT
    );
}

TEST(glob, suffix_and_prefix_match) {
    add_file("aba");

    add_file("aca");
    add_file("aaa");

    vt_send_str("echo *b*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo *b*",
        "./aba",
        PROMPT
    );
}

TEST(glob, single_substring_match) {
    add_file("boofooboo");
    add_file("boofoboo");
    add_file("boobooboo");

    vt_send_str("echo *foo*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo *foo*",
        "./boofooboo",
        PROMPT
    );
}

TEST(glob, multiple_substring_match) {
    add_file("XfooXbarX");

    add_file("XfoXbarX");
    add_file("XfooXarX");

    vt_send_str("echo *foo*bar*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo *foo*bar*",
        "./XfooXbarX",
        PROMPT
    );
}

TEST(glob, substring_and_prefix_match) {
    add_file("fooXbarX");
    add_file("fobar");
    add_file("XbarX");
    add_file("XfoobarXXX");

    vt_send_str("echo foo*bar*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo foo*bar*",
        "./fooXbarX",
        PROMPT
    );
}

TEST(glob, substring_and_suffix_match) {
    add_file("XfooXbar");
    add_file("fooba");
    add_file("foXbar");
    add_file("foobarX");

    vt_send_str("echo *foo*bar\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo *foo*bar",
        "./XfooXbar",
        PROMPT
    );
}

TEST(glob, multiple_substring_prefix_suffix_match) {
    add_file("fooXbarXbeeXzoo");
    add_file("Xfoobarbeezoo");
    add_file("foobarbeezooX");
    add_file("XXXfoobarbeezoo");
    add_file("foobarbeezooXXX");
    add_file("XfooXbarXbeeXzooX");

    vt_send_str("echo foo*bar*bee*zoo\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo foo*bar*bee*zoo",
        "./fooXbarXbeeXzoo",
        PROMPT
    );
}

TEST(glob, nested_directory_match) {
    add_dir("dir1");
    add_file("dir1/foo");

    vt_send_str("echo ./dir1/*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo ./dir1/*",
        "./dir1/foo",
        PROMPT
    );
}

TEST(glob, double_nested_directory_match) {
    add_dir("dir1/dir2");
    add_file("dir1/dir2/foo");

    vt_send_str("echo ./dir1/dir2/*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo ./dir1/dir2/*",
        "./dir1/dir2/foo",
        PROMPT
    );
}

TEST(glob, directory_componenet_with_glob) {
    add_dir("d-a");
    add_dir("dir2");
    add_dir("dir");
    add_file("dir2/foo");
    add_file("dir/foo");

    vt_send_str("echo ./dir*/foo\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo ./dir*/foo",
        "./dir/foo ./dir2/foo",
        PROMPT
    );
}

TEST(glob, directory_and_file_componenet_with_glob) {
    add_dir("d-a");
    add_dir("dir2");
    add_dir("dir");
    add_file("dir2/foo");
    add_file("dir/foo");

    vt_send_str("echo ./dir*/*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo ./dir*/*",
        "./dir/foo ./dir2/foo",
        PROMPT
    );
}

TEST(glob, directory_glob_and_file_prefix_match) {
    add_dir("d-a");
    add_dir("dir2");
    add_dir("dir");
    add_file("dir2/foo");
    add_file("dir/foo");

    vt_send_str("echo ./dir*/f*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo ./dir*/f*",
        "./dir/foo ./dir2/foo",
        PROMPT
    );
}

TEST(glob, directory_glob_and_file_suffix_match) {
    add_dir("d-a");
    add_dir("dir2");
    add_dir("dir");
    add_file("dir2/foo");
    add_file("dir/foo");

    vt_send_str("echo ./dir*/*o\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo ./dir*/*o",
        "./dir/foo ./dir2/foo",
        PROMPT
    );
}

TEST(glob, double_nested_directory_globs) {
    add_dir("abc/def");
    add_dir("a12/d34");
    add_file("abc/def/foo");
    add_file("a12/d34/foo");

    vt_send_str("echo ./a*/d*/*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo ./a*/d*/*",
        "./a12/d34/foo ./abc/def/foo",
        PROMPT
    );
}

TEST(glob, multiple_stars_are_compressed) {
    add_dir("abc");
    add_file("abc/foo");

    vt_send_str("echo ./***/*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo ./***/*",
        "./abc/foo",
        PROMPT
    );
}

TEST(glob, intermediary_component_with_complex_glob_pattern) {
    add_dir("a/fooXbarXbeeXzoo");
    add_file("a/fooXbarXbeeXzoo/foo");

    vt_send_str("echo ./a/foo*bar*bee*zoo/*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo ./a/foo*bar*bee*zoo/*",
        "./a/fooXbarXbeeXzoo/foo",
        PROMPT
    );
}

TEST(glob, relative_dirent_works) {
    add_dir("a/b");
    add_file("a/foo");

    vt_send_str("echo a/b/../*\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo a/b/../*",
        "./a/b/../b ./a/b/../foo",
        PROMPT
    );
}

TEST(glob, redirect_target_glob_non_ambiguous) {
    add_file("foo");

    vt_send_str("echo hi > f*\n");
    assert_term(1, PROMPT_LEN,
        PROMPT "echo hi > f*",
        PROMPT
    );

    char buf[3];
    int fd = open(GLOB_DIR "foo", O_RDONLY);
    if (fd == -1)
        xtest_fatal("open: %m");

    TEST_ASSERT_EQUAL_INT(2, read(fd, buf, 2));
    TEST_ASSERT_EQUAL_STRING_LEN("hi", buf, 2);
}

TEST(glob, redirect_target_glob_ambiguous) {
    add_file("foo1");
    add_file("foo2");

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
