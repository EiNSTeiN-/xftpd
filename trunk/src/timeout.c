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

#include "collection.h"
#include "time.h"
#include "constants.h"
#include "luainit.h"
#include "timeout.h"
#include "logging.h"

struct collection *timeouts = NULL; /* collection of struct timeout_ctx */

int timeout_init() {

	TIMEOUT_DBG("Loading ...");

	timeouts = collection_new(C_CASCADE);

	return 1;
}

void timeout_free() {

	TIMEOUT_DBG("Unloading ...");

	collection_destroy(timeouts);
	timeouts = NULL;

	return;
}

static void timeout_obj_destroy(struct timeout_ctx *to) {
	
	collectible_destroy(to);
	
	free(to->function);
	free(to);
	
	return;
}

/* add a timeout */
unsigned int timeout_add(char *function, unsigned long long int timeout, unsigned long long int timestamp) {
	struct timeout_ctx *to;

	to = malloc(sizeof(struct timeout_ctx));
	if(!to) {
		TIMEOUT_DBG("Memory error");
		return 0;
	}
	
	obj_init(&to->o, to, (obj_f)timeout_obj_destroy);
	collectible_init(to);

	to->function = strdup(function);
	if(!to->function) {
		TIMEOUT_DBG("Memory error");
		free(to);
		return 0;
	}

	to->timestamp = timestamp;
	to->timeout = timeout;

	collection_add(timeouts, to);
	
	return 1;
}

static int timeout_del_callback(struct collection *c, struct timeout_ctx *to, void *param) {
	struct {
		char *function;
		unsigned int success;
	} *ctx = param;

	if(!stricmp(to->function, ctx->function)) {
		obj_destroy(&to->o);
	}

	return 1;
}

/* delete a timeout */
unsigned int timeout_del(char *function) {
	struct {
		char *function;
		unsigned int success;
	} ctx = { function, 0 };

	collection_iterate(timeouts, (collection_f)timeout_del_callback, &ctx);

	return ctx.success;
}

/*static unsigned int timeout_clean_callback(struct collection *c, void *item, void *param) {
	struct timeout_ctx *to = item;

	collection_delete(timeouts, to);
	free(to->function);
	free(to);

	return 1;
}*/

/* clean all timeout */
void timeout_clear() {

	collection_empty(timeouts);

	return;
}

static unsigned int timeout_call_callback(struct collection *c, struct timeout_ctx *to, void *param) {
	lua_Number n;
	unsigned int err;
	char *errval, *errmsg;
	int stacktop = lua_gettop(L);

	//unsigned int gc_count = lua_getgccount(L);

	if((time_now() >= to->timestamp) && (timer(to->timestamp) >= to->timeout)) {
		// call a function with a C struct as parameter.

		to->timestamp = time_now();

		lua_pushstring(L, to->function);
		lua_gettable(L, LUA_GLOBALSINDEX);

		/* make sure the function has been found */
		if(!lua_isfunction(L, -1)) {
			TIMEOUT_DBG("%s is not a function", to->function);
			lua_pop(L, 1);
			if(stacktop != lua_gettop(L)) {
				TIMEOUT_DBG("ERROR: prev stack top mismatch current stack top (%d, was %d)!", lua_gettop(L), stacktop);
			}
			//luainit_garbagecollect();
			return 1;
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

			errmsg = (char*)lua_tostring(L, -1);

			TIMEOUT_DBG("error catched:");
			TIMEOUT_DBG("  --> function: %s", to->function);
			TIMEOUT_DBG("  --> error value: %s", errval);
			TIMEOUT_DBG("  --> error message: %s", errmsg);
		} else {
			/* make sure the return value is a number */
			if(!lua_isnumber(L, -1)) {
				TIMEOUT_DBG("%s returned non-number type, dropping.", to->function);
				lua_pop(L, 1);
				if(stacktop != lua_gettop(L)) {
					TIMEOUT_DBG("ERROR: prev stack top mismatch current stack top (%d, was %d)!", lua_gettop(L), stacktop);
				}
				//luainit_garbagecollect();
				return 1;
			}

			n = lua_tonumber(L, -1);

			if(!(int)n) {
				lua_pop(L, 1); /* pops the return value */
				if(stacktop != lua_gettop(L)) {
					TIMEOUT_DBG("ERROR: prev stack top mismatch current stack top (%d, was %d)!", lua_gettop(L), stacktop);
				}
				//luainit_garbagecollect();
				return 1;
			}
		}
		lua_pop(L, 1); /* pops the return value */
		if(stacktop != lua_gettop(L)) {
			TIMEOUT_DBG("ERROR: prev stack top mismatch current stack top (%d, was %d)!", lua_gettop(L), stacktop);
		}
	}

	//luainit_garbagecollect();

	return 1;
}

/* call any timeout that need so */
unsigned int timeout_poll() {
	
	collection_iterate(timeouts, (collection_f)timeout_call_callback, NULL);
	
	/*if(lua_gcmonitor()) {
		TIMEOUT_DBG("%u Kbytes for LUA with %u threshold.", current_gc_count, current_gc_threshold);
	}*/

	return 1;
}

