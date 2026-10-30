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
        {NULL, NULL}
    };

    void register_math(lua_State *L) {
        luaL_newlib(L, math_functions);
        lua_setglobal(L, "native");
    }
*/

lua_State *L;

int l_run(lua_State *state) {
    const char *s = luaL_checkstring(state, 1);
    int status = shell_run(s);
    lua_pushinteger(state, status);

    return 1;
}

static const luaL_Reg core_funcs[] = {
    { "run", l_run },
    { NULL, NULL }
};

void luart_init(void) {
    L = luaL_newstate();
    if (!L)
        xfatal("luaL_newstate");

    luaL_openlibs(L);

    luaL_newlib(L, core_funcs);
    lua_setglobal(L, "sh");
}
