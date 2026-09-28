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
    Printty *pt = printty_create();
    if (!pt)
        goto fail;

    PrinttyEvent pev;
    char *line = NULL;

    if (printty_cbreak(pt) == PRINTTY_ERR)
        goto fail;

    printty_set_suspend_mask(pt, sh_env.og_mask);

    while (true) {
        int rv = printty_get_event(pt, &pev);
        if (rv == PRINTTY_EOF) {
            LOG_INFO("eof, exiting");
            printty_destroy(pt);
            exit(EXIT_SUCCESS);

        } else if (rv == PRINTTY_ERR) {
            goto fail;

        } else if (rv == PRINTTY_SIG) {
            printty_destroy(pt);
            hds_free(line);
            return NULL;
        }

        if (pev.type == PRINTTY_UNICODE) {
            if (pev.codepoint >= 32 && pev.codepoint <= 126) {
                printf("%c", pev.codepoint);
                hds_append_n(line, &pev.utf8[0], 1);
            }

        } else if (pev.type == PRINTTY_KEY) {
            if (pev.key == PRINTTY_ENTER) {
                printf("\n");
                break;
            }
        }
    }

    printty_destroy(pt);
    return line;

fail:
    printty_destroy(pt);
    xfatal("failed parsing line");
}
