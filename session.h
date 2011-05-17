#include "common.h"

#define SES_REGNAME "luapg.session"
#define SESPREP_REGNAME "luapg.sessionprep"


typedef struct {
	PGconn *conn;
	unsigned int sid; // sequence for statement IDs.
} DBSession;

