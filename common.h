#ifndef _COMMON_H
#define _COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <libpq-fe.h>

#define MAX(a,b) ((a) > (b) ? (a) : (b))

int
stringNamedNull (char *);

void
arrayFromTable (lua_State *L, int index);

// A userdata for converting a Lua table value to a
// C string in the proper format of an SQL parameter.
typedef struct {
    int tref;
    void (*convert) (lua_State *L, int ref);
} ParamConvert;

// Error string constants.
#define ERROR_CONNECTION_FAILED   "Connection to database failed: %s"
#define ERROR_DB_UNAVAILABLE        "Database not available"
#define ERROR_EXECUTE_INVALID       "Execute called on a closed or invalid statement"
#define ERROR_EXECUTE_FAILED        "Execute failed %s"
#define ERROR_FETCH_INVALID     "Fetch called on a closed or invalid statement"
#define ERROR_FETCH_FAILED      "Fetch failed %s"
#define ERROR_PARAM_MISCOUNT        "Statement expected %d parameters but received %d"
#define ERROR_BINDING_PARAMS        "Error binding statement parameters: %s"
#define ERROR_BINDING_EXEC      "Error executing statement parameters: %s"
#define ERROR_FETCH_NO_EXECUTE    "Fetch called before execute"
#define ERROR_BINDING_RESULTS       "Error binding statement results: %s"
#define ERROR_UNKNOWN_PUSH      "Unknown push type in result set"
#define ERROR_ALLOC_STATEMENT       "Error allocating statement handle: %s"
#define ERROR_PREP_STATEMENT        "Error preparing statement handle: %s"
#define ERROR_INVALID_PORT      "Invalid port: %d"
#define ERROR_ALLOC_RESULT      "Error allocating result set: %s"
#define ERROR_DESC_RESULT       "Error describing result set: %s"
#define ERROR_BINDING_TYPE_ERR    "Unknown or unsupported type `%s'"
#define ERROR_INVALID_STATEMENT   "Invalid statement handle"
#define ERROR_NOT_IMPLEMENTED     "Method %s.%s is not implemented"
#define ERROR_QUOTING_STR         "Error quoting string: %s"

#endif
