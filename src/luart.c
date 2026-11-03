#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "utils.h"
#include "shell.h"

/*
    luaL_dostring()
    luaL_dofile()
    luaL_register()
    luaL_is[type]()
    luaL_push[type]()

    static const luaL_Reg math_functions[] = {
        {"add", l_add},
        {"multiply", l_multiply},
        {nullptr, nullptr}
    };

    void register_math(lua_State *L) {
        luaL_newlib(L, math_functions);
        lua_setglobal(L, "native");
    }
*/

lua_State *L;

static int l_run(lua_State *state) {
    const char *s = luaL_checkstring(state, 1);
    int status = shell_run(s);
    lua_pushinteger(state, status);

    return 1;
}

static int l_lua(lua_State *_) {
    env.lua = false;

    return 0;
}

static const luaL_Reg core_funcs[] = {
    { "run", l_run },
    { "lua", l_lua },
    { nullptr, nullptr }
};

void luart_run(const char *s) {
    if (luaL_dostring(L, s) != LUA_OK) {
        fprintf(stderr, "lua: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return;
    }

    lua_settop(L, 0);
}

void luart_init(void) {
    L = luaL_newstate();
    if (!L)
        fatal("luaL_newstate");

    luaL_openlibs(L);

    luaL_newlib(L, core_funcs);
    lua_setglobal(L, "sh");
}
