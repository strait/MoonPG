#include "common.h"
#include "session.h"
#include "result.h"
#include "geotypes.h"

static int
connect (lua_State *L)
{
	const char *cinfo = luaL_checkstring(L, 1);
	DBSession *sess = lua_newuserdata(L, sizeof(DBSession));
	sess->conn = PQconnectdb(cinfo);
	sess->sid = 1;
	sess->getbyarray = 0;

	if (PQstatus(sess->conn) != CONNECTION_OK) {
		lua_pushnil(L);
		lua_pushfstring(L, ERROR_CONNECTION_FAILED, PQerrorMessage(sess->conn));
		PQfinish(sess->conn);
		sess->conn = NULL;
		return 2;
	}

	// Set the metatable of this user data.
	luaL_getmetatable(L, SES_REGNAME);
	lua_setmetatable(L, -2);
	return 1;
}

static void
arrayFunc (lua_State *L, int ref)
{
	lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
	arrayFromTable(L, -1);
	luaL_unref(L, LUA_REGISTRYINDEX, ref);
}

static int
makeArray (lua_State *L)
{
	if (lua_istable(L, 1)) {
		int ref = luaL_ref(L, LUA_REGISTRYINDEX);
		ParamConvert *pc = lua_newuserdata(L, sizeof *pc);
		pc->tref = ref;
		pc->convert = arrayFunc;
		return 1;
	}
	else {
		return luaL_argerror(L, 1, "Expecting a table.");
	}
}
	
static const struct luaL_Reg funcs [] =
{
	{"connect", connect},
	{"Point", makePoint},
	{"Line", makeLine},
	{"Box", makeBox},
	{"Path", makePath},
	{"Polygon", makePolygon},
	{"Circle", makeCircle},
	{"Array", makeArray},
	{NULL, NULL}
};

int
luaopen_luapg (lua_State *L)
{
	void registerSession (lua_State *L);
	void registerResult (lua_State *L);

	registerSession(L);
	registerResult(L);
	luaL_register(L, "luapg", funcs);
	return 1;
}
