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

/* Manage the SITE commands issued from the ftpd clients */

#include "tree.h"
#include "collection.h"
#include "time.h"
#include "luainit.h"
#include "logging.h"
#include "constants.h"
#include "ftpd.h"
#include "site.h"
/*
	TODO: test scripts

	test crash (script error)
	test success (return 1, no error)
	test failure (return 0, no error)
	test return non-number
	test multiple scripts
*/

struct collection *site_hooks = NULL;

int site_init() {

	SITE_DBG("Loading ...");

	site_hooks = collection_new(C_CASCADE);

	return 1;
}

int site_reload() {

	SITE_DBG("Reloading ...");

	/* remove all handlers in the tree */
	collection_empty(site_hooks);

	return 1;
}

void site_free() {
	
	SITE_DBG("Unloading ...");

	/* remove all handlers in the tree */
	collection_destroy(site_hooks);
	site_hooks = NULL;

	return;
}

static unsigned int call_site_handlers(struct collection *c, struct ftpd_collectible_line *l, void *param) {
	struct {
		struct ftpd_client_ctx *client;
		char *args;
		int error;
	} *ctx = param;
	lua_Number n;
	unsigned int err, i;
	char *errval;
	char *errmsg;
	int stacktop = lua_gettop(L);
	char *handler = l->line;

	//unsigned int gc_count = lua_getgccount(L);

	lua_pushstring(L, handler);
	lua_gettable(L, LUA_GLOBALSINDEX);

	/* make sure the function has been found */
	if(!lua_isfunction(L, -1)) {
		SITE_DBG("%s is not a function", handler);
		ftpd_message(ctx->client, "500-Error calling \"%s\": not a function\n", handler);
		lua_pop(L, 1);
		ctx->error = 1;
		if(stacktop != lua_gettop(L)) {
			SITE_DBG("ERROR: prev stack top mismatch current stack top (%d, was %d)!", lua_gettop(L), stacktop);
		}
		//luainit_garbagecollect();
		return 1;
	}

	/* push the parameters */
	tolua_pushusertype(L, ctx->client, "ftpd_client");
	if(ctx->args) {
		lua_pushstring(L, ctx->args);
	} else {
		lua_pushnil(L);
	}

	err = lua_pcall(L, 2, 1, 0);
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

		SITE_DBG("error catched:");
		SITE_DBG("  --> function: %s", handler);
		SITE_DBG("  --> error value: %s", errval);
		SITE_DBG("  --> error message: %s", errmsg);

		for(i=0;i<strlen(errmsg);i++) {
			if(errmsg[i] == '\r' || errmsg[i] == '\n') errmsg[i] = '.';
		}

		// hardcoded error message for all irc scripts
		if(strchr(errmsg, ' ')) {
			errmsg = strchr(errmsg, ' ')+1;
		}

		/* reply an error to the client */
		ftpd_message(ctx->client, "500-%s\n", errmsg);

		ctx->error = 1;
	} else {
		/* make sure the return value is a number */
		if(!lua_isnumber(L, -1)) {
			ftpd_message(ctx->client, "500-warning: %s returned non-number type\n", handler);
			lua_pop(L, 1);
			if(stacktop != lua_gettop(L)) {
				SITE_DBG("ERROR: prev stack top mismatch current stack top (%d, was %d)!", lua_gettop(L), stacktop);
			}
			//luainit_garbagecollect();
			return 1;
		}

		n = lua_tonumber(L, -1);

		if(!(int)n) {
			ftpd_message(ctx->client, "500-warning: %s returned non-success\n", handler);
		}
	}

	lua_pop(L, 1); /* pops the return value */
	if(stacktop != lua_gettop(L)) {
		SITE_DBG("ERROR: prev stack top mismatch current stack top (%d, was %d)!", lua_gettop(L), stacktop);
	}
	
	//luainit_garbagecollect();

	return 1;
}

int site_tree_cmp(struct ftpd_collectible_line *a, struct ftpd_collectible_line *b) {
	
	return stricmp(a->line, b->line);
}

unsigned int site_tree_add(struct collection *branches, char *trigger, char *handler) {
	struct ftpd_collectible_line *l;
	
	l = ftpd_line_new(handler);
	if(l) {
		if(!tree_add(branches, trigger, &l->c, (tree_f)site_tree_cmp)) {
			return 0;
		}
	} else {
		return 0;
	}
	
	return 1;
}

unsigned int site_handle(struct ftpd_client_ctx *client, char *line) {
	struct {
		struct ftpd_client_ctx *client;
		char *args;
		int error;
	} ctx = { client, NULL, 0 };
	struct collection *handlers;

	handlers = tree_get(site_hooks, line, &ctx.args);
	if(!handlers || !collection_size(handlers)) {
		SITE_DBG("No SITE handler for \"%s\"", line);
		ftpd_message(client, "No SITE handler for \"%s\"", line);
		return 1;
	}

	/* call all handlers found for that command */
	collection_iterate(handlers, (collection_f)call_site_handlers, &ctx);

	if(ctx.args) {
		free(ctx.args);
	}

	/*if(lua_gcmonitor()) {
		SITE_DBG("%u Kbytes for LUA with %u threshold.", current_gc_count, current_gc_threshold);
	}*/

	return ctx.error ? 0 : 1;
}
