#include <asm-generic/errno-base.h>
#include <errno.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <linc/xsys.h>
#include "state.h"
#include "config.h"
#include "utils.h"
#include "input.h"

#define CONFIG_PROMPT \
    "Config file not found. Create one at '~/seashell.lua' (y/N): "

FILE *config_file_open(void) {
    const char *home = getenv("HOME");
    if (!home)
        return NULL;

    int fd;
    char path[PATH_MAX];
    int n = snprintf(path, PATH_MAX - 1,
            "%s/config.lua", home);

    if (n < 0 || n >= PATH_MAX - 1)
        return NULL;

    if ((fd = open(path, O_RDONLY)) == -1) {
        if (errno != ENOENT)
            return NULL;

        // char *line = input_get_line(/*CONFIG_PROMPT*/);

        // if (strcmp(line, "y") == 0) {
        //     xopen(path, O_RDONLY | O_CREAT | O_EXCL, 0700);
        // }
        //
        // return NULL;
    }

    FILE *config = fdopen(fd, "r");
    if (!config)
        return NULL;

    return config;
}

void config_init(shell_env *_) {
    // env->L = luaL_newstate();
    // if (!env->L)
    //     xfatal("luaL_newstate");
    //
    //  luaL_openlibs(env->L);
    //
    // FILE *config;
    //
    // if (!(config = config_file_open())) {
    //     lua_close(env->L);
    //     return;
    // }

    /* USE CONFIG */
}
