#ifndef LUART_H
#define LUART_H

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

extern lua_State *L;

void luart_init(void);

#endif
