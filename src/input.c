#include <stdio.h>
#include <stdlib.h>
#include <linux/limits.h>
#include <string.h> // IWYU pragma: keep
#include <unistd.h>
#include <fcntl.h>

#include "linc_tools.h"
#include "input.h"
#include "utils.h"

static char input_line[LINE_BUF];

void display_prompt(int mode) {
    dstr prompt;
    dstr_init(&prompt);

    if (mode == PROMPT_SIMPLE) {
        dstrcat(&prompt, "> ");

    } else if (mode == PROMPT_CWD) {
        static char cwd_str[PATH_MAX];

        char *cwd = xgetcwd(cwd_str, PATH_MAX);
        char *cwd_base = basename(cwd);

        dstrcat(&prompt, cwd_base);
        dstrcat(&prompt, "> ");
    } else
        xfatal("unknown prompt mode");

    printf("%s", prompt.c_str);

    dstr_free(&prompt);
}

char *get_line(void) {
    if (!fgets(input_line, LINE_BUF, stdin))
        xfatal("fgets");

    if (feof(stdout)) {
        LOG_INFO("eof, exiting");
        exit(EXIT_SUCCESS);
    }

    if (input_line[strlen(input_line) - 1] == '\n')
        input_line[strlen(input_line) - 1 ] = '\0';

    return input_line;
}
