#include "session.h"
#include "result.h"

static int
close (lua_State *L)
{
	DBSession *s = luaL_checkudata(L, 1, SES_REGNAME);
	if (s->conn) {
		PQfinish(s->conn);
		s->conn = NULL;
	}
	return 0;
}

static int
connectionSocket (lua_State *L)
{
	DBSession *s = luaL_checkudata(L, 1, SES_REGNAME);
	lua_pushnumber(L, PQsocket(s->conn));
	return 1;
}

static int
doGC (lua_State *L)
{
	return close(L);
}

static int
toString (lua_State *L)
{
	DBSession *s = luaL_checkudata(L, 1, SES_REGNAME);
	PGconn *c = s->conn;
	lua_pushfstring(L, "DB: %s, HOST: %s, USER: %s", PQdb(c), PQhost(c), PQuser(c));
	return 1;
}

// Returns a new prepare object on success.
static int
processPrepareStatus (lua_State *L, ExecStatusType status, DBSession *sess)
{
	int ret = 1;
	if (status == PGRES_COMMAND_OK) {
		DBSession *preps = lua_newuserdata(L, sizeof *preps);
		preps->conn = sess->conn;
		preps->sid = sess->sid++;
		luaL_getmetatable(L, SESPREP_REGNAME);
		lua_setmetatable(L, -2);
	}
	else {
		// Else an error condition
		lua_pushboolean(L, 0);
		lua_pushstring(L, PQerrorMessage(sess->conn));
		ret = 2;
	}
	return ret;
}

static int
prepare (lua_State *L)
{
	DBSession *s = luaL_checkudata(L, 1, SES_REGNAME);
	const char *query = luaL_checkstring(L, 2);
	ExecStatusType status = 0;
	PGresult *result;

	// Convert the sid to a string 
	lua_pushnumber(L, s->sid);
	const char *sname = lua_tostring(L, -1);

	result = PQprepare(s->conn, sname, query, 0, NULL);
	
	if (result) {
		status = PQresultStatus(result);
		PQclear(result);
	}
	return processPrepareStatus(L, status, s);
}

static int
getPrepared (lua_State *L)
{
	DBSession *s = luaL_checkudata(L, 1, SES_REGNAME);
	ExecStatusType status = 0;

	PGresult *result = PQgetResult(s->conn);
	// A non-null result indicates a command result to be returned
	if (result) {
		status = PQresultStatus(result);
		PQclear(result);
		return processPrepareStatus(L, status, s);
	}
	// Null pointer status indicates no more results
	else {
		lua_pushnil(L);
		return 1;
	}
}  

static int
processResultStatus (lua_State *L, PGresult *result, ExecStatusType status, PGconn *conn)
{
	int ret = 1;
	if (status == PGRES_COMMAND_OK) {
		// Returns the number of rows affected.
		lua_pushnumber(L, atoi(PQcmdTuples(result)));
		PQclear(result);
	}
	else if (status == PGRES_TUPLES_OK) {
		Result *r = lua_newuserdata(L, sizeof *r);
		r->pgres = result;
		r->paramtypes = NULL;
		// Set up the columntypes array for future access.
		int nf = PQnfields(result);
		r->columntypes = malloc(nf * sizeof *r->columntypes);
		for (int i = 0; i < nf; i++) {
			r->columntypes[i] = PQftype(result, i);
		}
		luaL_getmetatable(L, RES_REGNAME);
		lua_setmetatable(L, -2);
	}
	else {
		// Else an error condition.
		lua_pushboolean(L, 0);
		lua_pushstring(L, PQerrorMessage(conn));
		ret = 2;
		if (result) PQclear(result);
	}
	return ret;
}

static int
getResult (lua_State *L)
{
	DBSession *s = luaL_checkudata(L, 1, SES_REGNAME);
	PGresult *result = PQgetResult(s->conn);
	// A non-null result indicates a command result to be returned.
	if (result) {
		return processResultStatus(L, result, PQresultStatus(result), s->conn);
	}
	// Null pointer status indicates no more results.
	else {
		lua_pushnil(L);
		return 1;
	}
}

static int
processResult (lua_State *L, PGresult *result, PGconn *conn)
{
	ExecStatusType status = 0;

	if (result) {
		status = PQresultStatus(result);
	}
	return processResultStatus(L, result, status, conn);
}

// Here, a return value of 1 indicates success and a return value of 0 indicates error.
static int
processReturn (lua_State *L, int rvalue, PGconn * conn)
{
	if (rvalue) {
		lua_pushboolean(L, 1);
		return 1;
	}
	else {
		lua_pushboolean(L, 0);
		lua_pushstring(L, PQerrorMessage(conn));
		return 2;
	}
}

static int
asyncPrepare (lua_State *L)
{
	DBSession *s = luaL_checkudata(L, 1, SES_REGNAME);
	const char *query = luaL_checkstring(L, 2);
	const char *sName;

	// Convert the sid to a string.
	lua_pushnumber(L, s->sid);
	sName = lua_tostring(L, -1);

	return processReturn(L, PQsendPrepare(s->conn, sName, query, 0, NULL), s->conn);
}

// Get the string representation of the parameter at stack position pos.
// Check for return of NULL indicating non-string parameter.
static const char *
getPFS (lua_State *L, int pos)
{
	// If a special value embedded in userdata
	if (lua_isuserdata(L, pos)) {
		ParamConvert *pconv = lua_touserdata(L, pos);
		pconv->convert(L, pconv->tref);
		return lua_tostring(L, -1);
	}
	else {
		return lua_tostring(L, pos);
	}
}
	
static const char **
parametersFromStack (lua_State *L, int count, int offset)
{
	const char **pvals = malloc(count * sizeof *pvals);
	const char *s;
	// Gather all parameter arguments
	for (int i = 0; i < count; i++) {
		s = getPFS(L, i + offset);
		if (s) {
			pvals[i] = s;
		}
		else {
			luaL_error(L, "Not a valid parameter at position %i", i);
		}
	}
	return pvals;
}

static int
runG (lua_State *L, int type)
{
	DBSession *s = luaL_checkudata(L, 1, SES_REGNAME);
	const char *command = luaL_checkstring(L, 2);
	int nargs = lua_gettop(L);
	int ret;
	if (nargs == 2) {
		if (type == 1) {
			ret = processResult(L, PQexec(s->conn, command), s->conn);
		}
		else {
			ret = processReturn(L, PQsendQuery(s->conn, command), s->conn);
		}
	}
	else {
		int pc = nargs - 2;
		const char **pValues = parametersFromStack(L, pc, 3);
		if (type == 1) {
			ret = processResult(L,
				PQexecParams(s->conn, command, pc, NULL, pValues, NULL, NULL, 0),
				s->conn);
		}
		else {
			ret = processReturn(L,
				PQsendQueryParams(s->conn, command, pc, NULL, pValues, NULL, NULL, 0),
				s->conn);
		}
		free(pValues);
	}
	return ret;
}

static int
run (lua_State *L)
{
	return runG(L, 1);
}

static int
asyncRun (lua_State *L)
{
	return runG(L, 2);
}

static int
runGPrepared (lua_State *L, int type)
{
	DBSession *s = luaL_checkudata(L, 1, SESPREP_REGNAME);
	int pc = lua_gettop(L) - 1;
	const char **pValues = parametersFromStack(L, pc, 2);
	int ret;

	// Convert the sid to a string 
	lua_pushnumber(L, s->sid);
	const char *sname = lua_tostring(L, -1);

	if (type == 1) {
		ret = processResult(L,
			PQexecPrepared(s->conn, sname, pc, pValues, NULL, NULL, 0),
			s->conn);
	}
	else {
		ret = processReturn(L,
			PQsendQueryPrepared(s->conn, sname, pc, pValues, NULL, NULL, 0),
			s->conn);
	}
	free(pValues);
	return ret;
}

static int
deallocatePrepared (lua_State *L)
{
	DBSession *s = luaL_checkudata(L, 1, SESPREP_REGNAME);
	// Convert the sid to a string 
	lua_pushnumber(L, s->sid);
	const char *values[1];
	values[0] = lua_tostring(L, -1);

	PGresult *res = PQexecParams(s->conn, "DEALLOCATE $1", 1, NULL, values, NULL, NULL, 0);
	return processResult(L, res, s->conn);
}

static int
runPrepared (lua_State *L)
{
	return runGPrepared(L, 1);
}

static int
asyncRunPrepared (lua_State *L)
{
	return runGPrepared(L, 2);
}

static int
consumeInput (lua_State *L)
{
	DBSession *s = luaL_checkudata(L, 1, SES_REGNAME);
	return processReturn(L, PQconsumeInput(s->conn), s->conn);
}    

static int
isBusy (lua_State *L)
{
	DBSession *s = luaL_checkudata(L, 1, SES_REGNAME);
	lua_pushboolean(L, PQisBusy(s->conn));
	return 1;
}

/* true to set non-blocking status. false to make blocking.
 * Default without no argument is value of true */
static int
setNonBlocking (lua_State *L)
{
	DBSession *s = luaL_checkudata(L, 1, SES_REGNAME);
	int result;
	int set = (lua_gettop(L) < 2 || lua_toboolean(L, 2)) ? 1 : 0;
	result = PQsetnonblocking(s->conn, set);
	if (result == 0) {
		lua_pushboolean(L, 1);
		return 1;
	}
	else {
		// Else an error condition
		lua_pushboolean(L, 0);
		lua_pushstring(L, PQerrorMessage(s->conn));
		return 2;
	}
}

static int
flush (lua_State *L)
{
	DBSession *s = luaL_checkudata(L, 1, SES_REGNAME);
	int result = PQflush(s->conn);
	if (result == 0) {
		lua_pushboolean(L, 1);
		return 1;
	}
	else if (result == 1) {
		// Else unable to send all data yet.
		lua_pushboolean(L, 0);
		return 1;
	}
	else {
		// Else an error condition.
		lua_pushboolean(L, 0);
		lua_pushstring(L, PQerrorMessage(s->conn));
		return 2;
	}
}

static int
checkNotifies (lua_State *L)
{
	DBSession *s = luaL_checkudata(L, 1, SES_REGNAME);
	PGnotify *notify = PQnotifies(s->conn);
	if (notify) {
		lua_createtable(L, 0, 2);
		lua_pushstring(L, notify->relname);
		lua_setfield(L, -2, "name");
		lua_pushnumber(L, notify->be_pid);
		lua_setfield(L, -2, "pid"); 
		PQfreemem(notify);
	}
	else {
		lua_pushboolean(L, 1);
	}
	return 1;
}

static int
backendPID (lua_State *L)
{
	DBSession *s = luaL_checkudata(L, 1, SES_REGNAME);
	lua_pushnumber(L, PQbackendPID(s->conn));
	return 1;
}

/*
static int
getSQLCommand (lua_State *L, int pos, const char **command, const char ***values)
{
	int plen = lua_objlen(L, -1);
	if (plen) {
		lua_rawgeti(L, pos, 1);
		*command = lua_tostring(L, -1);
		if (*command) {
			const char *val;
			const char **vals = *values = malloc((plen - 1) * sizeof **values);
			for (int i = 2; i <= plen; i++) {
				lua_rawgeti(L, pos, i);
				val = getPFS(L, -1);
				if (val) {
					vals[2 - i] = val;
				}
				else {
					luaL_error(L, "Not a valid parameter at position %i", 2 - i);
				}
			}
		}
		else {
			luaL_error(L, "Not a valid string for the SQL command.");
		}
	}
	else {
		luaL_error(L, "Update must be called with 2 command tables.");
	}
	return plen;
}

static int
preUpdate (DBSession *s, const char *command, int pc, const char **vals)
{
	char newCommand[strlen(command) + 6] = "BEGIN;"
	strcat(newCommand, command);
	PGresult *res = PQexecParams(s->conn, command, pc, NULL, vals, NULL, NULL, 0);
	if (PQresultStatus(res) != PGRES_TUPLES_OK) {
		
static int
update (lua_State *L)
{
	if (lua_isfunction(L, 2) && lua_istable(L, 3) && lua_istable(L, 4)) {
		DBSession *s = luaL_checkudata(L, 1, SES_REGNAME);
		const char *command1, *command2;
		const char **vals1 = NULL, **vals2 = NULL;
		int pc1 = getSQLCommand(L, 3, &command1, &vals1);
		int pc2 = getSQLCommand(L, 4, &command2, &vals2);
		// Given a get query to run.
		int nps = preUpdate(s, command1, pc1, vals1);
		lua_pushvalue(L, 2);
		lua_insert(L, -nps - 1);
		lua_call(L, nps, pc2);
		postUpdate(s, command2, pc2, vals2);
		
		free(vals1);
		free(vals2);
	}
	else {
		luaL_error(L, "Argument error.");
	}
	return 0;
}
			
	*/  
		

static const struct luaL_Reg methods [] = {
	{"run", run},
	{"prepare", prepare},
	{"asyncRun", asyncRun},
	{"asyncPrepare", asyncPrepare},
	{"getResult", getResult},
	{"getPrepared", getPrepared},
	{"connectionSocket", connectionSocket},
	{"consumeInput", consumeInput},
	{"isBusy", isBusy},
	{"setNonBlocking", setNonBlocking},
	{"checkNotifies", checkNotifies},
	{"backendPID", backendPID},
	{"flush", flush},
	{"close", close},
	{"__gc", doGC},
	{"__tostring", toString},
	{NULL, NULL}
};

void
registerSession (lua_State *L)
{
	luaL_newmetatable(L, SES_REGNAME);
	luaL_register(L, NULL, methods);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");

	luaL_newmetatable(L, SESPREP_REGNAME);
	lua_pushcfunction(L, runPrepared);
	lua_setfield(L, -2, "run");
	lua_pushcfunction(L, asyncRunPrepared);
	lua_setfield(L, -2, "asyncRun");
	lua_pushcfunction(L, deallocatePrepared);
	lua_setfield(L, -2, "deallocate");
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
}
	
