#include "linc/string.h"
#include "linc/xsys.h"
#include "input.h"
#include "state.h"
#include "utils.h"
#include "shell.h"
#include "config.h"

int main(int argc, char *const *argv) {
    xatexit(seashell_shutdown);
    env_init(argc, argv);

    if (opts[OPT_SCRIPT].found)
        exit(shell_run(opts[OPT_SCRIPT].val_str));

    if (script_file)
        exit(shell_run_file(script_file, false));

    while (true) {
        char *line;
        linc_string prompt = LINC_STRING_INIT;

        config_get_prompt(&prompt);

        if (!(line = input_get_line(prompt.cstr)))
            fatal("input_get_line: EOF/Error");

        shell_run(line);
        ASSERT(!env.subsh);
        process_signals();

        free(line);
        linc_string_free(&prompt);
    }
}
