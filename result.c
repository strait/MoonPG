#include "result.h"
#include "geotypes.h"

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

// Push the value of tuple, field.
static void
pushValue (lua_State *L, Result *r, int tuple, int field)
{
	char *value;
	int nt = tuple - 1;
	if (PQgetisnull(r->pgres, nt, field)) {
		lua_pushnil(L);
	}
	else {
		value = PQgetvalue(r->pgres, nt, field);
		int tid = r->columntypes[field];
		char *sf;
		if (r->paramtypes && (sf = r->paramtypes[field])) {
			// To explicitly keep as a string, numeric values that may overflow.
			if (strcmp(sf, "String") == 0) {
				lua_pushstring(L, value);
			}
			// A special type is designated for this column.
			else if (strcmp(sf, "Array") == 0) {
				pushArray(L, tid, value);
			}
			// Geometric types
			else if (strcmp(sf, "Point") == 0) {
				pushGeoPoint(L, value);
			}
			else if (strcmp(sf, "Line") == 0) {
				pushGeoLine(L, value);
			}
			else if (strcmp(sf, "Box") == 0) {
				pushGeoBox(L, value);
			}
			else if (strcmp(sf, "Path") == 0) {
				pushGeoPath(L, value);
			}
			else if (strcmp(sf, "Polygon") == 0) {
				pushGeoPolygon(L, value);
			}
			else if (strcmp(sf, "Circle") == 0) {
				pushGeoCircle(L, value);
			}
			else {
				lua_pushstring(L, value);
			}
		}
		else {
			switch (tid) {
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
pushTupleObj (lua_State *L, Result *res, int ntuple)
{
	// Return a new userdata to work on a tuple.
	ResTuple *rt = lua_newuserdata(L, sizeof *rt);
	rt->result = res;
	rt->tuple = ntuple;
	luaL_getmetatable(L, RESTUPLE_REGNAME);
	lua_setmetatable(L, -2);
}

static int
tupleIter (lua_State *L)
{
	Result *r = lua_touserdata(L, lua_upvalueindex(1));
	int tuple = lua_tointeger(L, lua_upvalueindex(2));
	
	if (tuple <= PQntuples(r->pgres)) {
		pushTupleObj(L, r, tuple);
		lua_pushnumber(L, ++tuple);
		lua_replace(L, lua_upvalueindex(2));
	}
	// Else no more results
	else {
		lua_pushnil(L);
	}
	return 1;
}

static void
clearTypeMap (char **ptypes, int length)
{
	if (ptypes) {
		char *t;
		for (int i = 0; i < length; i++) {
			t = ptypes[i];
			if (t) {
				free(t);
			}
		}
		free(ptypes);
	}
}

static int
doGC (lua_State *L)
{
	Result *r = lua_touserdata(L, 1);
	free(r->columntypes);
	clearTypeMap(r->paramtypes, PQnfields(r->pgres));
	PQclear(r->pgres);
	return 0;
}

static int
toString (lua_State *L)
{
	Result *r = lua_touserdata(L, 1);
	lua_pushfstring(L, "Number of rows in result set: %d", PQntuples(r->pgres));
	return 1;
}

// Helper function to parse the option between the inner separator into field_name/Type option.
static void
innerOption (lua_State *L, const char *b, PGresult *res, char **ptypes)
{
	char *sep = strchr(b, ':');
	if (sep) {
		int nf = PQnfields(res);
		// If the field name matches a results field, then the results field position in the array
		// gets the type option value.
		for (int i = 0; i < nf; i++) {
			if (strncmp(b, PQfname(res, i), sep - b) == 0) {
				char **spadd = ptypes + i;
				*spadd = malloc(strlen(sep));
				strcpy(*spadd, sep + 1);
			}
		}
	}
}

// Return an iterator function that iterates through the results.
static int
tuples (lua_State *L)
{
	luaL_checkudata(L, 1, RES_REGNAME);
	lua_pushnumber(L, 1);
	lua_pushcclosure(L, tupleIter, 2);
	return 1;
}

// The number of tuples returned
static int
count (lua_State *L)
{
	Result *r = luaL_checkudata(L, 1, RES_REGNAME);
	lua_pushnumber(L, PQntuples(r->pgres));
	return 1;
}

// Return an array of field names for this result.
static int
fields (lua_State *L)
{
	Result *r = luaL_checkudata(L, 1, RES_REGNAME);
	int colCnt = PQnfields(r->pgres);
	lua_createtable(L, colCnt, 0);
	for (int i = 0; i < colCnt; i++) {
		lua_pushstring(L, PQfname(r->pgres, i));
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

// Calling without arguments simply clears the type map.
static int
setTypeMap (lua_State *L)
{
	Result *r = luaL_checkudata(L, 1, RES_REGNAME);
	int length = PQnfields(r->pgres);
	// Clear the type map.
	clearTypeMap(r->paramtypes, length);
	if (lua_gettop(L) == 1) {
		// Prevent a dangling pointer.
		r->paramtypes = NULL;
		return 0;
	}
	r->paramtypes = calloc(length, sizeof *r->paramtypes); 
	char *sep;
	const char *mapstr = luaL_checkstring(L, 2);
	char buf[100];
	int span, moreOptions = 1;
	// Parsing on special named fields.
	while (moreOptions) {
		sep = strchr(mapstr, ',');
		if (sep) {
			span = sep - mapstr;
			strncpy(buf, mapstr, span);
			buf[span] = '\0';
			mapstr += span + 1;
			innerOption(L, buf, r->pgres, r->paramtypes);
		}
		else {
			// At the end.
			innerOption(L, mapstr, r->pgres, r->paramtypes);
			moreOptions = 0;
		}
	}
	return 0;
}

static int
onFieldIndex (lua_State *L)
{
	ResTuple *rt = luaL_checkudata(L, 1, RESTUPLE_REGNAME);
	Result *r = rt->result;
	int field;
	int t = lua_type(L, 2);
	if (t == LUA_TNUMBER) {
		field = lua_tonumber(L, 2);
		pushValue(L, r, rt->tuple, field - 1);
	}
	else if (t == LUA_TSTRING) {
		const char *fname = lua_tostring(L, 2);
		field = PQfnumber(r->pgres, fname);
		if (field != -1) {
			pushValue(L, r, rt->tuple, field);
		}
		else {
			return luaL_error(L, "Field name not valid: %s", fname);
		}
	}
	else {
		return luaL_argerror(L, 1, "Pass a numeric column index or a string field value.");
	}
	return 1;
}

static int
onResultIndex (lua_State *L)
{
	Result *r = luaL_checkudata(L, 1, RES_REGNAME);
	if (lua_isnumber(L, 2)) {
		pushTupleObj(L, r, lua_tonumber(L, 2));
		return 1;
	}
	else {
		// Look for a method to return.
		int a = luaL_getmetafield(L, 1, luaL_checkstring(L, 2));
		if (a) {
			return 1;
		}
		else {
			return luaL_error(L, "Provide an integer index to access the result tuples.");
		}
	}
}

static const struct luaL_Reg methods [] = {
	{"tuples", tuples},
	{"fields", fields},
	{"setTypeMap", setTypeMap},
	{"__gc", doGC},
	{"__len", count},
	{"__tostring", toString},
	{NULL, NULL}
};

void
registerResult (lua_State *L)
{
	luaL_newmetatable(L, RES_REGNAME);
	lua_pushcfunction(L, onResultIndex);
	lua_setfield(L, -2, "__index");
	luaL_register(L, NULL, methods);
	// Define the meta-table for the tuple table.
	luaL_newmetatable(L, RESTUPLE_REGNAME);
	lua_pushcfunction(L, onFieldIndex);
	lua_setfield(L, -2, "__index");
	
}
