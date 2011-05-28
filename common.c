#include "common.h"
#include "ctype.h"

int
stringNamedNull (char *str)
{
    if (tolower(str[0]) == 'n' && tolower(str[1]) == 'u' &&
            tolower(str[2]) == 'l' && tolower(str[3]) == 'l') {
        return 1;
    }
    else {
        return 0;
    }
}
 
// Turn the table value at index into a PostgreSQL array value.
void
arrayFromTable (lua_State *L, int index)
{
    int tabLen = lua_objlen(L, index);
    luaL_Buffer arrText;
    luaL_buffinit(L, &arrText);
    for (int i = 1; i <= tabLen; i++) {
        // Open brace on first iteration.
        if (i == 1) {
            luaL_addchar(&arrText, '{');
        }
        // A comma on all subsequent iterations.
        else {
            luaL_addchar(&arrText, ',');
        }
        lua_rawgeti(L, index, i);
        if (lua_istable(L, -1)) {
            arrayFromTable(L, lua_gettop(L));
            // Remove the processed table element off the stack.
            lua_remove(L, -2);
        }
        else {
            int escaped = 0, stringval = 0;
            if (lua_isnil(L, -1)) {
                lua_pushliteral(L, "NULL");
                lua_remove(L, -2);
            }
            else {
                stringval = 1;
                char *nextValue = (char *)luaL_checkstring(L, -1);
                // Escape double quote and backslash.
                if (strpbrk(nextValue, "\"\\")) {
                    luaL_Buffer itemText;
                    char *brk, *last = nextValue;
                    escaped = 1;
                    luaL_buffinit(L, &itemText);
                    while ((brk = strpbrk(last, "\"\\"))) {
                        luaL_addlstring(&itemText, last, brk - last);
                        luaL_addchar(&itemText, '\\');
                        luaL_addchar(&itemText, *brk);
                        last = brk + 1;
                    }
                    // Add the rest to the end.
                    luaL_addstring(&itemText, last);
                    luaL_pushresult(&itemText);
                    lua_remove(L, -2);
                }
            }
            // Surround in double quotes, if needed.
            char *st = (char *)lua_tostring(L, -1);
            if (strcmp(st, "") == 0 ||
                    escaped || strpbrk(st, "{},; ") || (stringNamedNull(st) && stringval)) {
                lua_pushliteral(L, "\"");
                lua_insert(L, -2);
                lua_pushliteral(L, "\"");
                lua_concat(L, 3);
            }
        }
        luaL_addvalue(&arrText);
    }
    luaL_addchar(&arrText, '}');
    // Push the array string on to the stack
    luaL_pushresult(&arrText);
}
            
