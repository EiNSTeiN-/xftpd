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

#ifdef WIN32
#include <windows.h>
#endif


/* Manage the SITE commands issued from the ftpd clients */

#include "tree.h"
#include "collection.h"
#include "time.h"
#include "luainit.h"
#include "logging.h"
#include "constants.h"
#include "ftpd.h"
#include "site.h"

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

static unsigned int call_site_handlers(struct collection *c, struct site_handler *handler, void *param) {
	struct {
		struct ftpd_client_ctx *client;
		char *args;
		int error;
	} *ctx = param;
	lua_State *L = handler->script->L;
	
	lua_pushcfunction(L, luainit_traceback);
	
	luainit_tget(L, SITE_REFTABLE, handler->handler_index);
	if(lua_isfunction(L, -1)) {
		int err;
		
		/* push the parameters */
		tolua_pushusertype(L, ctx->client, "ftpd_client");
		
		/* push extra args */
		if(ctx->args) {
			lua_pushstring(L, ctx->args);
		} else {
			lua_pushnil(L);
		}
		
		/* call the function with two params and one return */
		err = lua_pcall(L, 2, 1, -4);
		if(err) {
			unsigned int i;
			char *errmsg;
			
			/* do something with the error ... ? */
			luainit_error(L, "(calling site callback)", err);
			
			errmsg = strdup(lua_tostring(L, -1));
			
			for(i=0;i<strlen(errmsg);i++) {
				if(errmsg[i] == '\r' || errmsg[i] == '\n') {
					errmsg[i] = 0;
					break;
				}
			}
			
			if(strchr(errmsg, ' ')) {
				errmsg = strchr(errmsg, ' ')+1;
			}
			
			/* reply an error to the client */
			ftpd_message(ctx->client, "500-%s\n", errmsg);
			
			ctx->error = 1;
			
			free(errmsg);
		} else {
			/* do nothing */
		}
		
		/* pops the error message or the return value */
		lua_pop(L, 1);
	} else {
		/* pops the thing we just pushed that is not a function */
		lua_pop(L, 1);
	}
	lua_pop(L, 1); /* pops the errfunc */
	
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
	
	return ctx.error ? 0 : 1;
}
