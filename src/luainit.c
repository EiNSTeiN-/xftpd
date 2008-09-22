/*
 * Copyright (c) 2007, The xFTPd Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 *     * Neither the name of the xFTPd Project nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *     * Redistributions of this project or parts of this project in any form
 *       must retain the following aknowledgment:
 *       "This product includes software developed by the xFTPd Project.
 *        http://www.xftpd.com/ - http://www.xftpd.org/"
 *
 * THIS SOFTWARE IS PROVIDED BY THE xFTPd PROJECT ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE xFTPd PROJECT BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <windows.h>

#include "luainit.h"
#include "collection.h"

lua_State* L = NULL;

int luainit_init() {
	LUALIB_API int luaopen_lsqlite3(lua_State *L);

	LUAINIT_DBG("Loading ...");
	
	/* create lua instance */
	L = lua_open();
	if(!L) {
		LUAINIT_DBG("Could not create lua state");
		return 0;
	}
	
	/* load lua libraries */
	luaL_openlibs(L);
	luaopen_lsqlite3(L);

	return 1;
}

void luainit_free() {

	LUAINIT_DBG("Unloading ...");

	/* destroy lua instance */
	lua_close(L);

	return;
}

int luainit_reload() {
	LUALIB_API int luaopen_lsqlite3(lua_State *L);

	LUAINIT_DBG("Reloading ...");
	
	/* destroy lua instance */
	lua_close(L);
	
	/* create lua instance */
	L = lua_open();
	if(!L) {
		LUAINIT_DBG("Could not create lua state");
		return 0;
	}
	
	/* load lua libraries */
	luaL_openlibs(L);
	luaopen_lsqlite3(L);
	
	return 1;
}

int luainit_garbagecollect() {
	
	//LUAINIT_DBG("luainit_garbagecollect (1): %u", (lua_gc(L, LUA_GCCOUNT, 0) * 1024) + lua_gc(L, LUA_GCCOUNTB, 0));
	lua_gc(L, LUA_GCCOLLECT, 0);
	//LUAINIT_DBG("luainit_garbagecollect (2): %u", (lua_gc(L, LUA_GCCOUNT, 0) * 1024) + lua_gc(L, LUA_GCCOUNTB, 0));
	//lua_gc(L, LUA_GCCOLLECT, 0);
	//LUAINIT_DBG("luainit_garbagecollect (3): %u", (lua_gc(L, LUA_GCCOUNT, 0) * 1024) + lua_gc(L, LUA_GCCOUNTB, 0));
	
	return 1;
}

/* function: collection_lua_next */
#ifdef TOLUA_DISABLE_tolua_xFTPd_bind_collection_iterate00
int tolua_xFTPd_bind_collection_iterate00(lua_State* tolua_S)
{
/*#ifndef TOLUA_RELEASE
 tolua_Error tolua_err;
 if (
     !tolua_isusertype(tolua_S,1,"_collection",0,&tolua_err) ||
     !tolua_isusertype(tolua_S,2,"collection_iterator",0,&tolua_err) ||
     !tolua_isstring(tolua_S,3,0,&tolua_err) ||
     !tolua_isnoobj(tolua_S,4,&tolua_err)
 )
  goto tolua_lerror;
 else
#endif*/
 {
  struct collection* c = ((struct collection*)  tolua_tousertype(tolua_S,1,0));
  struct collection_iterator* iter = ((struct collection_iterator*)  tolua_tousertype(tolua_S,2,0));
  const char* type = ((const char*)  tolua_tostring(tolua_S,3,0));
  {
   struct collectible* tolua_ret = (struct collectible*)  collection_next(c, iter);
   tolua_pushusertype(L,(void*)tolua_ret, type);
  }
 }
 return 1;
/*#ifndef TOLUA_RELEASE
 tolua_lerror:
 tolua_error(tolua_S,"#ferror in function 'iterate'.",&tolua_err);
 return 0;
#endif*/
}
#endif //#ifdef TOLUA_DISABLE
