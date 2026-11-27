#include <linc/xsys.h>
#include "config.h"
#include "shell.h"
#include "state.h"
#include "utils.h"

void config_get_prompt(linc_string *prompt) {
    void *prompt_func;
    if (mp_str_lookup(&env.funcs, "sea_prompt", &prompt_func) == -1) {
        linc_string_append(prompt, "> ");
        return;
    }

    env.func_level += 1;

    int rfd = shell_run_capture(&((ps_cmd *)prompt_func)->func.body, nullptr);

    env.returning = false;
    env.func_level -= 1;

    read_to_eof(rfd, prompt);
    trim_newlines(prompt);
    xclose(rfd);
}

void config_init(void) {
    env.func_level += 1;

    if (opts[OPT_CONFIG].found) {
        shell_run_file(opts[OPT_CONFIG].val_str, false);
    } else {
        char *home = getenv("HOME");

        if (home) {
            linc_string config = LINC_STRING_INIT;
            linc_string_appendf(&config, "%s/.searc", home);
            shell_run_file(config.cstr, true);
            linc_string_free(&config);

            LOG_INFO("sourced config: %s", config.cstr);
        }
    }

    env.func_level -= 1;
}
