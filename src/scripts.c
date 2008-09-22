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

#include "lualib.h"
#include "lauxlib.h"
#include "tolua.h"

#include "constants.h"
#include "collection.h"
#include "logging.h"
#include "config.h"
#include "scripts.h"
#include "luainit.h"

/* lua seems to not be able to compare pointers on its own ... */
int equals(void *a, void *b) {

	return (a == b);
}

int scripts_init() {
	int tolua_xFTPd_bind_open(lua_State*);

	SCRIPTS_DBG("Loading ...");

	/* bind exported xFTPd's lua functions */
	tolua_xFTPd_bind_open(L);

	scripts_loadall();

	return 1;
}

unsigned int scripts_loadall() {
	char buffer[128];
	unsigned int i;
	char *file;
	const char *errmsg;
	char *errval;
	unsigned int err;

	/* load all sections */
	for(i=1;;i++) {

		/* get the section name */
		sprintf(buffer, "xftpd.script(%u).file", i);
		file = config_raw_read(MASTER_CONFIG_FILE, buffer, NULL);
		if(!file) break;

		if(lua_dofile(L, file)) {
			SCRIPTS_DBG("Could not load file %s", file);
			free(file);
			continue;
		}

		/* call INIT of this script */
		lua_pushstring(L, "init");
		lua_gettable(L, LUA_GLOBALSINDEX);
		if(!lua_isfunction(L, -1)) {
			SCRIPTS_DBG("%s have no init() function", file);
			lua_pop(L, 1);
			free(file);
			continue;
		}

		err = lua_pcall(L, 0, 1, 0);
		if(err) {
			/*
			LUA_ERRRUN --- a runtime error. 
			LUA_ERRMEM --- memory allocation error. For such errors, Lua does not call the error handler function. 
			LUA_ERRERR --- error while running the error handler function. 
			*/
			if(err == LUA_ERRRUN) errval = "runtime error";
			else if(err == LUA_ERRMEM) errval = "memory allocation error";
			else if(err == LUA_ERRERR) errval = "error while running the error handler function";
			else errval = "unknown error";

			errmsg = lua_tostring(L, -1);

			SCRIPTS_DBG("error catched:");
			SCRIPTS_DBG("  --> function: %s", "init");
			SCRIPTS_DBG("  --> error value: %s", errval);
			SCRIPTS_DBG("  --> error message: %s", errmsg);
		} else {
			if(!lua_isnumber(L, -1))
				SCRIPTS_DBG("%s returned non-number type.", file);
			else {
				lua_Number n;
				n = lua_tonumber(L, -1);
				if(!(int)n) {
					SCRIPTS_DBG("%s failed to initialize itself.", file);
				} else {
					//SCRIPTS_DBG("%s successfully loaded.", file);
				}
			}
		}
		lua_pop(L, 1); /* pops the return value */

		free(file);
	}

	/*if(lua_gcmonitor()) {
		SCRIPTS_DBG("%u Kbytes for LUA with %u threshold.", current_gc_count, current_gc_threshold);
	}*/

	return 1;
}

void scripts_free() {

	SCRIPTS_DBG("Unloading ...");

	return;
}
