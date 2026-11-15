#include "linc/string.h"
#include "linc/xsys.h"
#include "input.h"
#include "utils.h"
#include "shell.h"
#include "luart.h"

int main(int argc, char *const *argv) {
    xatexit(seashell_shutdown);
    env_init(argc, argv);

    if (opts[OPT_SCRIPT].found) {
        int status = shell_run(opts[OPT_SCRIPT].val_str);
        exit(status);
    }

    while (true) {
        char *line;
        linc_string prompt = shell_get_prompt();

        if (!(line = input_get_line(prompt.cstr)))
            fatal("input_get_line: EOF/Error");

        if (env.lua) {
            luart_run(line);
        } else {
            shell_run(line);
            ASSERT(env.subsh == 0); /* shouldn't be possible */
            process_signals();
        }

        free(line);
        linc_string_free(&prompt);
    }
}
