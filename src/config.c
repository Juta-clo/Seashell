#include <asm-generic/errno-base.h>
#include <errno.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "shell.h"
#include "config.h"
#include "utils.h"
#include "input.h"

int config_create(void) {
    char c;
    char *line[2];

    printf("config.lua not found. Would you like to create one? (y/n): ");
    // getline(&p, 2, 
}

int config_open(void) {
    const char *home = getenv("HOME");
    if (!home)
        return -1;

    int fd;
    char path[PATH_MAX];
    int n = snprintf(path, PATH_MAX - 1,
            "%s/.config/seashell/config.lua", home);

    if (n < 0 || n >= PATH_MAX - 1)
        return -1;

    if ((fd = open(path, O_RDONLY)) == -1) {
        if (errno != ENOENT)
            return -1;

        config_create();
    }
}

void config_init(shell_env *_) {
    // env->L = luaL_newstate();
    // if (env->L)
    //     xfatal("luaL_newstate");
    //
    //  luaL_openlibs(env->L);
    //  config_open();
}
