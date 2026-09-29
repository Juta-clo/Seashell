#include <stdio.h>
#include <stdlib.h>
#include <linux/limits.h>
#include <string.h> // IWYU pragma: keep
#include <unistd.h>
#include <fcntl.h>

#include "linc_tools.h"
#include "input.h"
#include "shell_state.h"
#include "utils.h"
#include "printty.h"

void input_init(shell_env *env) {
    assert(env);

    if (!(env->pt = printty_create(NULL)))
        xfatal("printty_create");
    if (printty_cbreak(env->pt) == PRINTTY_ERR)
        xfatal("printty_cbreak");;
    printty_set_suspend_mask(env->pt, env->og_mask);
}

void input_deinit(shell_env *env) {
    assert(env);

    printty_destroy(env->pt);
    env->pt = NULL;
}

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
    PrinttyEvent pev;
    char *line = NULL;

    while (true) {
        int rv = printty_get_event(sh_env.pt, &pev);
        if (rv == PRINTTY_EOF) {
            LOG_INFO("eof, exiting");
            exit(EXIT_SUCCESS);

        } else if (rv == PRINTTY_ERR) {
            xfatal("failed parsing line");

        } else if (rv == PRINTTY_SIG) {
            hda_free(line);
            return NULL;
        }

        if (pev.type == PRINTTY_UNICODE) {
            if (pev.codepoint >= 32 && pev.codepoint <= 126) {
                printf("%c", pev.codepoint);
                hda_push(line, pev.utf8[0]);

            } else if (pev.codepoint == '\x15'/*<C-u>*/) {
                for (size_t i = 0; i < hda_size(line); ++i)
                    printf("\b \b");
                hda_delete(line, 0, hda_size(line));
            }

        } else if (pev.type == PRINTTY_KEY) {
            if (pev.key == PRINTTY_ENTER) {
                printf("\n");
                if (line == NULL)
                    return "";
                break;
            } else if (pev.key == PRINTTY_BACKSPACE) {
                if (hda_size(line) > 0) {
                    printf("\b \b");
                    hda_delete(line, hda_size(line) - 1, 1);
                }
            }
        }
    }

    hda_push(line, '\0');
    LOG_INFO("parsed line: %s", line);
    return line;
}
