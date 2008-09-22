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
//#include "lua-5.1.1/src/lstate.h"

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
/*
unsigned int luainit_gcobjectcount(GCObject *gco) {
	unsigned int count = 0;
	GCObject *next;
	
	next = gco;
	while(next) {
		count++;
		next = next->gch.next;
	}
	
	return count;
}

int luainit_dumpstate(char *file) {
	struct lua_State *dL = (struct lua_State *)L;
	unsigned int i;
	
	logging_write(file, "dumping lua state ...\n");
	logging_write(file, "openupval: %u\n", luainit_gcobjectcount(dL->openupval));
	logging_write(file, "gclist: %u\n", luainit_gcobjectcount(dL->gclist));
	
	logging_write(file, "rootgc: %u\n", luainit_gcobjectcount(dL->l_G->rootgc));
	logging_write(file, "gray: %u\n", luainit_gcobjectcount(dL->l_G->gray));
	logging_write(file, "grayagain: %u\n", luainit_gcobjectcount(dL->l_G->grayagain));
	logging_write(file, "weak: %u\n", luainit_gcobjectcount(dL->l_G->weak));
	logging_write(file, "totalbytes: %u\n", dL->l_G->totalbytes);
	logging_write(file, "estimate: %u\n", dL->l_G->estimate);
	logging_write(file, "gcdept: %u\n", dL->l_G->gcdept);
	logging_write(file, "size_ci: %u\n", dL->size_ci);
	logging_write(file, "stacksize: %u\n", dL->stacksize);
	
	return 1;
}
*/
