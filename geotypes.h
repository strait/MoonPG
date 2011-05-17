#ifndef _GEOTYPES_H
#define _GEOTYPES_H

#include "common.h"

int
makePoint (lua_State *L);

int
makeLine (lua_State *L);

int
makeBox (lua_State *L);

int
makePath (lua_State *L);

int
makePolygon (lua_State *L);

int
makeCircle (lua_State *L);

void
pushGeoLine (lua_State *L, char *value);

void
pushGeoPoint (lua_State *L, char *value);

void
pushGeoBox (lua_State *L, char *value);

void
pushGeoPath (lua_State *L, char *value);

void
pushGeoPolygon (lua_State *L, char *value);

void
pushGeoCircle (lua_State *L, char *value);

#endif
