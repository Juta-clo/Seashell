#include <asm-generic/errno-base.h>
#include <errno.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <linc/xsys.h>
#include "config.h"
#include "utils.h"
#include "input.h"
#include "variable.h"

#define CONFIG_PATH "~/.seashell.lua"

#define CONFIG_PROMPT \
    "Config file not found. Create one at '" CONFIG_PATH "' (y/N): "

#define NO_REF -1

static lua_State *L;
static int ref_field_prompt = NO_REF;
char config_path[PATH_MAX] = "";

static int push_ctx_table(lua_State *state) {
    const char *cwd = lua_touserdata(state, 1);
    const char *home = lua_touserdata(state, 2);
    int last_status = *(int *)lua_touserdata(state, 3);
    const char *git_branch = lua_touserdata(state, 4);

    lua_newtable(state);

        lua_pushstring(state, cwd);
        lua_setfield(state, -2, "cwd");

        lua_pushstring(state, home);
        lua_setfield(state, -2, "home");

        lua_newtable(state);

            lua_pushnumber(state, last_status);
            lua_setfield(state, -2, "status");

        lua_setfield(state, -2, "last");

        if (!git_branch) {
            lua_pushnil(state);
            lua_setfield(state, -2, "git");
        } else {
            lua_newtable(state);

                lua_pushstring(state, git_branch);
                lua_setfield(state, -2, "branch");

            lua_setfield(state, -2, "git");
        }

    return 1;
}

static FILE *config_file_open(void) {
    int n;

    if (opts[CONFIG].found) {
        n = snprintf(config_path, PATH_MAX - 1, "%s", opts[CONFIG].val_str);
    } else {
        const char *home = getenv("HOME");
        if (!home)
            return NULL;

        n = snprintf(config_path, PATH_MAX - 1,
                "%s/.seashell.lua", home);
    }

    if (n < 0 || n >= PATH_MAX - 1)
        return NULL;

    int fd = open(config_path, O_RDONLY);

    if (fd == -1) {
        if (opts[CONFIG].found)
            xfatal("--config-file=%s: %m", opts[CONFIG].val_str);

        if (errno != ENOENT)
            return NULL;

        char *line = input_get_line(CONFIG_PROMPT);
        if (!line)
            return NULL;

        input_clear_history();

        if (strcmp(line, "y") == 0) {
            if ((fd = open(config_path, O_RDWR | O_CREAT, 0600)) == -1) {
                errno_msg("failed to create config file");
            } else {
                printf("Successfully created '%s'!\n", CONFIG_PATH);

                FILE *config = fdopen(fd, "r+");

                if (fprintf(config, "return {}\n") < 0)
                    xfatal("fprintf() falied()");

                if (fclose(config) == EOF)
                    err_exit_imp("fclose");
            }
        }

        free(line);
        return NULL;
    }

    FILE *config = fdopen(fd, "r");
    if (!config)
        return NULL;

    return config;
}

static void parse_config(lua_State *L) {
    ASSERT(lua_gettop(L) == 0);

    if (luaL_dofile(L, config_path) != LUA_OK)
        xfatal("Lua: %s\n", lua_tostring(L, -1));

    if (!lua_istable(L, -1))
        xfatal("lua_istable");

    if (lua_getfield(L, -1, "prompt") != LUA_TNIL) {
        if (!lua_isfunction(L, -1))
            xfatal("config.prompt not a function");

        ref_field_prompt = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        lua_pop(L, 1);
    }

    lua_pop(L, 1);
    ASSERT(lua_gettop(L) == 0);
}

char *get_git_branch(void) {
    FILE *git_pipe = popen("git branch --show-current 2>/dev/null", "r");
    if (!git_pipe)
        return NULL;

    char *branch = nullptr;
    size_t n = 0;

    ssize_t len = getline(&branch, &n, git_pipe);
    int status = pclose(git_pipe);

    if (len <= 0 || status != 0) {
        free(branch);
        return 0;
    }

    branch[len - 1] = '\0';

    return branch;
}

int config_field_prompt(linc_string *string) {
    if (ref_field_prompt == NO_REF)
        return -1;

    ASSERT(lua_gettop(L) == 0);

    if (lua_rawgeti(L, LUA_REGISTRYINDEX, ref_field_prompt) != LUA_TFUNCTION)
        xfatal("unexpected lua reference");

    char *cwd = xgetcwd(NULL, 0);
    char *home = getenv("HOME");
    if (!home)
        home = "";

    int last_status = 0;
    sh_var *var = lookup_var(&sh_env.vars, "?");
    if (var->value) {
        char *endptr;
        long l = strtol(var->value, &endptr, 10);
        if (*var->value != '\0' && *endptr == '\0')
            last_status = l;
    }

    char *git_branch = get_git_branch();

    lua_pushcfunction(L, push_ctx_table);
    lua_pushlightuserdata(L, cwd);
    lua_pushlightuserdata(L, home);
    lua_pushlightuserdata(L, &last_status);
    lua_pushlightuserdata(L, git_branch);

    if (lua_pcall(L, 4, 1, 0) != LUA_OK)
        xfatal("Lua: %s", lua_tostring(L, -1));

    if (lua_pcall(L, 1, 1, 0) != LUA_OK)
        xfatal("Lua: %s", lua_tostring(L, -1));

    if (lua_type(L, -1) != LUA_TSTRING)
        xfatal("config.prompt did not return a string");

    const char *prompt = lua_tostring(L, -1);
    linc_string_append(string, prompt);
    free(cwd);
    free(git_branch);

    lua_pop(L, 1);

    ASSERT(lua_gettop(L) == 0);
    return 0;
}

void config_init(void) {
    L = luaL_newstate();
    if (!L)
        xfatal("luaL_newstate");

    luaL_openlibs(L);

    FILE *config = config_file_open();

    if (!config)
        return;

    parse_config(L);

    if (fclose(config) == EOF)
        xfatal("fclose");
}
