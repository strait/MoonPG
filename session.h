#include "common.h"

#define SES_REGNAME "luapg.session"
#define SESPREP_REGNAME "luapg.sessionprep"

typedef enum {
	boolOID = 16,
	int8OID = 20,
	int2OID = 21,
	int4OID = 23,
	float4OID = 700,
	float8OID = 701,
	numericOID = 1700,
	
	// Array types
	boolAOID = 1000,
	intA2OID = 1005,
	intA4OID = 1007,
	intA8OID = 1016,
	floatA4OID = 1021,
	floatA8OID = 1022,
	numericAOID = 1231
	
} PGtype;


typedef struct {
	PGconn *conn;
	unsigned int sid; // sequence for statement IDs.
	int getbyarray;
	char *typeMapString;
} DBSession;

