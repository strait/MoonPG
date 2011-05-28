#include "geotypes.h"

int
makePoint (lua_State *L)
{
    lua_pushfstring(L, "(%f,%f)", luaL_checknumber(L, 1), luaL_checknumber(L, 2));
    return 1;
}
 
static int
makeSegment (lua_State *L, const char *geotype)
{
    if (lua_istable(L, 1)) {
        lua_rawgeti(L, 1, 1);
        lua_rawgeti(L, 1, 2);
        if (lua_istable(L, 2) && lua_istable(L, 3)) {
            lua_rawgeti(L, 2, 1);
            lua_rawgeti(L, 2, 2);
            lua_rawgeti(L, 3, 1);
            lua_rawgeti(L, 3, 2);
            if (lua_isnumber(L, -4) && lua_isnumber(L, -3) &&
                    lua_isnumber(L, -2) && lua_isnumber(L, -1)) {
                lua_pushfstring(L, "((%f,%f),(%f,%f))", lua_tonumber(L, -4), lua_tonumber(L, -3),
                    lua_tonumber(L, -2), lua_tonumber(L, -1));
            }
            else {
                return luaL_error(L, "Element of %s is not a number.", geotype);
            }
        }
        else {
            return luaL_error(L, "Incorrect table argument passed to %s.", geotype);
        }
    }
    else {
        lua_pushfstring(L, "((%f,%f),(%f,%f))", luaL_checknumber(L, 1), luaL_checknumber(L, 2),
            luaL_checknumber(L, 3), luaL_checknumber(L, 4));
    }
    return 1;
}

int
makeLine (lua_State *L)
{
    return makeSegment(L, "Line");
}

int
makeBox (lua_State *L)
{
    return makeSegment(L, "Box");
}

int
makeCircle (lua_State *L)
{
    lua_pushfstring(L, "<(%f,%f),%f>", luaL_checknumber(L, 1), luaL_checknumber(L, 2),
        luaL_checknumber(L, 3));
    return 1;
}

// Push the contents of the first argument table as a path string.
static int
pushPathTable (lua_State *L)
{
    if (lua_istable(L, 1)) {
        luaL_Buffer path;
        luaL_buffinit(L, &path);
        int tabLen = lua_objlen(L, 1);
        for (int i = 1; i <= tabLen; i++) {
            if (i > 1) {
                luaL_addchar(&path, ',');
            }
            lua_rawgeti(L, 1, i);
            if (lua_istable(L, -1)) {
                lua_rawgeti(L, -1, 1);
                lua_rawgeti(L, -2, 2);
                if (lua_isnumber(L, -2) && lua_isnumber(L, -1)) {
                    lua_pushfstring(L, "(%f,%f)", lua_tonumber(L, -2), lua_tonumber(L, -1));
                }
                else {
                    return luaL_error(L, "An element of a table value is not a number.");
                }
                // Remove the aux values from the stack.
                for (int j = 0; j < 3; j++) {
                    lua_remove(L, -2);
                }
                luaL_addvalue(&path);
            }
            else {
                return luaL_error(L, "An inner table value is expected at position %i.", i  );
            }
        }
        luaL_pushresult(&path);
    }
    else {
        return luaL_argerror(L, 1, "Expecting a table.");
    }
    return 0;
}

int
makePath (lua_State *L)
{
    int closed = 0;
    if (lua_isboolean(L, 2)) {
        closed = lua_toboolean(L, 2);
    }
    pushPathTable(L);
    if (closed) {
        lua_pushliteral(L, ")");
        lua_pushliteral(L, "(");
    }
    else {
        lua_pushliteral(L, "]");
        lua_pushliteral(L, "[");
    }
    // Insert the initial paren/bracket into it's proper place.
    lua_insert(L, -3);
    lua_concat(L, 3);
    return 1;
}
    
int
makePolygon (lua_State *L)
{
    pushPathTable(L);
    lua_pushliteral(L, ")");
    lua_pushliteral(L, "(");
    lua_insert(L, -3);
    lua_concat(L, 3);
    return 1;
}

// Put into the buffer the next point structure. The value passed should be right at the start of a
// structure.
// Return the character position after the separator, or null if this is the last structure.
static char *
nextPoint (char *value, char *buf) {
    int len;
    // Get the middle comma
    char *sep = strchr(value, ',');
    sep = strchr(sep + 1, ',');
    if (sep) {
        len = sep - value;
        sep++;
    }
    else {
        len = strlen(value) - 1;
    }
    strncpy(buf, value, len);
    buf[len] = '\0';
    return sep;
}

// Returns with a table on the stack with x and y fields, representing the value of the point.
static void
pointIntoTable (lua_State *L, char *value)
{
    char buf[20];
    lua_createtable(L, 0, 2);
    value++;
    char *sep = strchr(value, ',');
    int len = sep - value;
    strncpy(buf, value, len);
    buf[len] = '\0';
    lua_pushnumber(L, strtod(buf, NULL));
    lua_setfield(L, -2, "x");
    char *start = sep + 1;
    len = strlen(value) - (start - value + 1);
    strncpy(buf, start, len);
    buf[len] = '\0';
    lua_pushnumber(L, strtod(buf, NULL));
    lua_setfield(L, -2, "y");
}

void
pushGeoLine (lua_State *L, char *value)
{
    char buf[43];
    lua_createtable(L, 0, 2);
    char *sep = nextPoint(value + 1, buf);
    pointIntoTable(L, buf);
    lua_setfield(L, -2, "a");
    nextPoint(sep, buf);
    pointIntoTable(L, buf);
    lua_setfield(L, -2, "b");
}

void
pushGeoBox (lua_State *L, char *value)
{
    char buf[43];
    lua_createtable(L, 0, 2);
    char *sep = nextPoint(value, buf);
    pointIntoTable(L, buf);
    lua_setfield(L, -2, "ur");
    strcpy(buf, sep);
    pointIntoTable(L, buf);
    lua_setfield(L, -2, "ll");
} 

void
pushGeoPolygon (lua_State *L, char *value)
{
    char buf[43];
    lua_newtable(L);
    char *sep = value + 1;
    int n = 1;
    while ((sep = nextPoint(sep, buf))) {
        pointIntoTable(L, buf);
        lua_rawseti(L, -2, n++);
    }
    pointIntoTable(L, buf);
    lua_rawseti(L, -2, n);
}

void
pushGeoPath (lua_State *L, char *value)
{
    // Closed property.
    int closed = value[0] == '(' ? 1 : 0;
    // Reuse the polygon implementation since they have similar structure.
    pushGeoPolygon(L, value);
    lua_pushboolean(L, closed);
    lua_setfield(L, -2, "closed");
}

void
pushGeoCircle (lua_State *L, char *value)
{
    char buf[43];
    lua_createtable(L, 0, 2);
    char *sep = nextPoint(value + 1, buf);
    pointIntoTable(L, buf);
    lua_setfield(L, -2, "center");
    int len = strlen(sep) - 1;
    strncpy(buf, sep, len);
    buf[len] = '\0';
    lua_pushnumber(L, strtod(buf, NULL));
    lua_setfield(L, -2, "radius");
}

void
pushGeoPoint (lua_State *L, char *value)
{
    pointIntoTable(L, value);
}

