#include "session.h"
#include "geotypes.h"

static int
close (lua_State *L)
{
	DBSession *s = luaL_checkudata(L, 1, SES_REGNAME);
	if (s->conn) {
		PQfinish(s->conn);
		s->conn = NULL;
	}
	if (s->typeMapString) {
		free(s->typeMapString);
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

// Parse the array value items. The string position is initially on the opening brace.
// Return a pointer to the current character position in value. (for recursive calls)
static char *
pushArray (lua_State *L, int typeOID, char *value)
{
	char *point; // Beginning of element value.
	int inQuote = 0;
	int index = 1;
	int itemLen;
	
	point = ++value; // Scoot past the initial open brace.
	lua_newtable(L);
	while (*value != '\0') {
		// Item end check
		if (((*value == ',' || *value == '}') && !inQuote) || (*value == '"' && inQuote)) {
			itemLen = value - point;
			// Convert a true NULL value to Lua nil
			if (!inQuote && itemLen == 4 && stringNamedNull(point)) {
				lua_pushnil(L);
			}
			else { 
				lua_pushlstring(L, point, itemLen);
				const char *v = lua_tostring(L, -1);
				switch (typeOID) {
					case intA2OID:
					case intA4OID:
					case intA8OID:  
						lua_pushnumber(L, atoi(v));
						break;
					case floatA4OID:
					case floatA8OID:
						lua_pushnumber(L, strtod(v, NULL));
						break;
					case boolAOID:
						lua_pushboolean(L, strcmp(v, "t") == 0 ? 1 : 0);
						break;
				}
				// If an accurate typed value was pushed, replace the string value.
				if (lua_type(L, -1) != LUA_TSTRING) {
					lua_replace(L, -2);
				}
			}
			lua_rawseti(L, -2, index++);
			if (inQuote) {  // Move past the ending double quote.
				inQuote = 0;
				value++;
			}
			if (*value == '}') {
				return value;
			}
			point = ++value;
		}
		else if (inQuote) {
			// Handle the escapes
			if (*value == '\\') {
				memmove(value, value + 1, strlen(value));
			}
			value++;
		}
		else if (*value == '{') {   // An inner array.
			value = pushArray(L, typeOID, value);
			lua_rawseti(L, -2, index++);
			value++;
			if (*value == ',') {
				point = ++value;
			}
			else if (*value == '}') {
				return value;
			}
		}
		// Beginning quote, reset point.
		else if (*value == '"') {
			inQuote = 1;
			point = ++value;
		}
		else {
			value++;
		}
	}
	return value;
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
		preps->getbyarray = 0;
		preps->typeMapString = NULL;
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
setTypeMap (lua_State *L)
{
	DBSession *s = luaL_checkudata(L, 1, SES_REGNAME);
	const char *str = luaL_checkstring(L, 2);
	s->typeMapString = malloc(strlen(str));
	strcpy(s->typeMapString, str);
	return 0;
}

// Push the value of tuple, field.
static void
pushValue (lua_State *L, PGresult *result, int tuple, int field, PGtype columnType, char *paramType)
{
	char *value;
	if (PQgetisnull(result, tuple, field)) {
		lua_pushnil(L);
	}
	else {
		value = PQgetvalue(result, tuple, field);
		if (paramType) {
			// To explicitly keep as a string, numeric values that may overflow.
			if (strcmp(paramType, "String") == 0) {
				lua_pushstring(L, value);
			}
			// A special type is designated for this column.
			else if (strcmp(paramType, "Array") == 0) {
				pushArray(L, columnType, value);
			}
			// Geometric types
			else if (strcmp(paramType, "Point") == 0) {
				pushGeoPoint(L, value);
			}
			else if (strcmp(paramType, "Line") == 0) {
				pushGeoLine(L, value);
			}
			else if (strcmp(paramType, "Box") == 0) {
				pushGeoBox(L, value);
			}
			else if (strcmp(paramType, "Path") == 0) {
				pushGeoPath(L, value);
			}
			else if (strcmp(paramType, "Polygon") == 0) {
				pushGeoPolygon(L, value);
			}
			else if (strcmp(paramType, "Circle") == 0) {
				pushGeoCircle(L, value);
			}
			else {
				lua_pushstring(L, value);
			}
		}
		else {
			switch (columnType) {
				case int2OID:
				case int4OID:
				case int8OID:
					lua_pushnumber(L, atoi(value));
					break;
				case float4OID:
				case float8OID:
				case numericOID:
					lua_pushnumber(L, strtod(value, NULL));
					break;
				case boolOID:
					lua_pushboolean(L, strcmp(value, "t") == 0 ? 1 : 0);
					break;
				default:
					lua_pushstring(L, value);
			}
		}
	}
}

static void
clearTypeMap (char **ptypes, int length)
{
	char *t;
	for (int i = 0; i < length; i++) {
		t = ptypes[i];
		if (t) {
			free(t);
		}
	}
}

// Helper function to parse the option between the inner separator into field_name/Type option.
static void
innerOption (const char *b, int nfields, char **cnames, char **ptypes)
{
	char *sep = strchr(b, ':');
	if (sep) {
		// If the field name matches a results field, then the results field position in the array
		// gets the type option value.
		for (int i = 0; i < nfields; i++) {
			if (strncmp(b, cnames[i], sep - b) == 0) {
				char **spadd = ptypes + i;
				*spadd = malloc(strlen(sep));
				strcpy(*spadd, sep + 1);
			}
		}
	}
}

static void
parseTypeMap(const char *typeMapString, int nfields, char **cnames, char **ptypes)
{
	char *sep;
	char buf[100];
	int span, moreOptions = 1;
	// Parsing on special named fields.
	while (moreOptions) {
		sep = strchr(typeMapString, ',');
		if (sep) {
			span = sep - typeMapString;
			strncpy(buf, typeMapString, span);
			buf[span] = '\0';
			typeMapString += span + 1;
			innerOption(buf, nfields, cnames, ptypes);
		}
		else {
			// At the end.
			innerOption(typeMapString, nfields, cnames, ptypes);
			moreOptions = 0;
		}
	}
}
	

static int
processResultStatus (lua_State *L, PGresult *result, ExecStatusType status, DBSession *s)
{
	int ret = 1;
	if (status == PGRES_COMMAND_OK) {
		// Returns the number of rows affected.
		lua_pushnumber(L, atoi(PQcmdTuples(result)));
		PQclear(result);
	}
	else if (status == PGRES_TUPLES_OK) {
		// Create a table with all the result data
		int nt = PQntuples(result);
		int nf = PQnfields(result);
		PGtype columnTypes[nf];
		char *columnNames[nf];
		char *paramTypes[nf];

		lua_createtable(L, nt, 1); // Result table
		lua_createtable(L, nf, 0); // Field names table

		for(int i = 0; i < nf; i++) {
			char *fname = PQfname(result, i);
			columnTypes[i] = PQftype(result, i);
			columnNames[i] = fname;
			paramTypes[i] = 0; // To initialize
			lua_pushstring(L, fname);
			lua_rawseti(L, -2, i+1);
		}

		if (s->typeMapString) {
			parseTypeMap(s->typeMapString, nf, columnNames, paramTypes);
		}

		// Insert the fieldNames table into the result table.
		lua_pushstring(L, "fields");
		lua_insert(L, -2);
		lua_rawset(L, -3);
		// Inset the tuples into the result table.
		for (int i = 0; i < nt; i++) {
			if (!s->getbyarray) {
				lua_createtable(L, 0, nf);
				for (int j = 0; j < nf; j++) {
					lua_pushstring(L, columnNames[j]);
					pushValue(L, result, i, j, columnTypes[j], paramTypes[j]); 
					lua_rawset(L, -3);
				}
			}
			else {
				lua_createtable(L, nf, 0);
				for (int j = 0; j < nf; j++) {
					pushValue(L, result, i, j, columnTypes[j], paramTypes[j]); 
					lua_rawseti(L, -2, j+1);
				}
			}
			lua_rawseti(L, -2, i+1);
		}

		if (s->typeMapString) {
			clearTypeMap(paramTypes, nf);
			free(s->typeMapString);
			s->typeMapString = NULL;
		}
	}
	else {
		// Else an error condition.
		lua_pushboolean(L, 0);
		lua_pushstring(L, PQerrorMessage(s->conn));
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
		return processResultStatus(L, result, PQresultStatus(result), s);
	}
	// Null pointer status indicates no more results.
	else {
		lua_pushnil(L);
		return 1;
	}
}

static int
processResult (lua_State *L, PGresult *result, DBSession *s)
{
	ExecStatusType status = 0;

	if (result) {
		status = PQresultStatus(result);
	}
	return processResultStatus(L, result, status, s);
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
			ret = processResult(L, PQexec(s->conn, command), s);
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
				s);
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
			s);
	}
	else {
		ret = processReturn(L,
			PQsendQueryPrepared(s->conn, sname, pc, pValues, NULL, NULL, 0),
			s->conn);
	}
	free(pValues);
	return ret;
}

// Have the results tuple keyed by array indices instead of hash names.
static int
arrayKeys (lua_State *L)
{
	DBSession *s = luaL_checkudata(L, 1, SES_REGNAME);
	s->getbyarray = lua_toboolean(L, 2);
	return 0;
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
	return processResult(L, res, s);
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


static const struct luaL_Reg methods [] = {
	{"run", run},
	{"arrayKeys", arrayKeys},
	{"setTypeMap", setTypeMap},
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
	
