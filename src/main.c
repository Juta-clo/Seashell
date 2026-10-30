#include "linc/string.h"
#include "input.h"
#include "state.h"
#include "utils.h"
#include "shell.h"
#include "luart.h"

int main(int argc, char *const *argv) {
    env_setup(argc, argv);

    while (true) {
        char *line;
        linc_string prompt = shell_get_prompt();

        if (!(line = input_get_line(prompt.cstr)))
            xfatal("input_get_line: EOF/Error");

        if (sh_env.lua_mode) {
            luart_run_string(line);
        } else {
            shell_run(line);
        }

        free(line);
        linc_string_free(&prompt);
    }
}
