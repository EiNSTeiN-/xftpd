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

#include "constants.h"
#include "vfs.h"
#include "collection.h"
#include "slaves.h"
#include "events.h"
#include "luainit.h"
#include "logging.h"
#include "slaveselection.h"

static struct signal_ctx *slaveselection_signal_up = NULL;
static struct signal_ctx *slaveselection_signal_down = NULL;

int slaveselection_init() {

	slaveselection_signal_up = event_signal_get("slaveselection_up", 1);
	signal_ref(slaveselection_signal_up);

	slaveselection_signal_down = event_signal_get("slaveselection_down", 1);
	signal_ref(slaveselection_signal_down);

	return 1;
}


void slaveselection_free() {

	signal_unref(slaveselection_signal_up);
	slaveselection_signal_up = NULL;

	signal_unref(slaveselection_signal_down);
	slaveselection_signal_down = NULL;

	return;
}


static int slaveselection_signal_lua_callback(struct collection *c, struct event_callback *cb, void *param) {
	struct {
		struct event_object *object;
		void *ptr;
	} *ctx = param;
	unsigned int i, err;
	char *errval, *errmsg;
	tolua_Error tlErr;
	int stacktop = lua_gettop(L);
	struct slave_connection *cnx;

	//unsigned int gc_count = lua_getgccount(L);

	lua_pushstring(L, cb->function);
	lua_gettable(L, LUA_GLOBALSINDEX);

	/* make sure the function has been found */
	if(!lua_isfunction(L, -1)) {
		SLAVESELECTION_DBG("%s is not a function", cb->function);
		lua_pop(L, 1);

		if(stacktop != lua_gettop(L)) {
			SLAVESELECTION_DBG("ERROR: prev stack top mismatch current stack top (%d, was %d)!", lua_gettop(L), stacktop);
		}
		//luainit_garbagecollect();
		return 1;
	}

	/* push the parameters */
	for(i=0;i<ctx->object->param_count;i++) {
		tolua_pushusertype(L, ctx->object->params[i].ptr, ctx->object->params[i].type);
	}

	err = lua_pcall(L, ctx->object->param_count, 1, 0);
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

		SLAVESELECTION_DBG("error catched:");
		SLAVESELECTION_DBG("  --> function: %s", cb->function);
		SLAVESELECTION_DBG("  --> error value: %s", errval);
		SLAVESELECTION_DBG("  --> error message: %s", errmsg);
	} else {

		if(lua_isnil(L, -1)) {
			/* nil is not an error */
			lua_pop(L, 1);
			if(stacktop != lua_gettop(L)) {
				SLAVESELECTION_DBG("ERROR: prev stack top mismatch current stack top (%d, was %d)!", lua_gettop(L), stacktop);
			}
			//luainit_garbagecollect();
			return 1;
		}

		/* make sure the return value is a number */
		if(!tolua_isusertype(L, 1, "slave_connection", 0, &tlErr)) {
			SLAVESELECTION_DBG("%s returned non-\"slave_connection\" type, dropping.", cb->function);
			lua_pop(L, 1);
			if(stacktop != lua_gettop(L)) {
				SLAVESELECTION_DBG("ERROR: prev stack top mismatch current stack top (%d, was %d)!", lua_gettop(L), stacktop);
			}
			//luainit_garbagecollect();
			return 1;
		}

		cnx = (struct slave_connection *)tolua_tousertype(L, 1, 0);

		if(cnx) {
			lua_pop(L, 1); /* pops the return value */
			if(stacktop != lua_gettop(L)) {
				SLAVESELECTION_DBG("ERROR: prev stack top mismatch current stack top (%d, was %d)!", lua_gettop(L), stacktop);
			}
			ctx->object->ret.ptr = ctx;
			//luainit_garbagecollect();
			return 0;
		}
	}

	lua_pop(L, 1); /* pops the return value */
	if(stacktop != lua_gettop(L)) {
		SLAVESELECTION_DBG("ERROR: prev stack top mismatch current stack top (%d, was %d)!", lua_gettop(L), stacktop);
	}
	//luainit_garbagecollect();

	return 1;
}

int slaveselection_signal_callback(struct event_object *object, struct event_ctx *event) {
	struct {
		struct event_object *object;
		void *ptr;
	} ctx = { object, NULL };

	if(object->param_count && !object->params) {
		SLAVESELECTION_DBG("%s could not be raised, param error.", event->name);
		return 0;
	}

	collection_iterate(event->callbacks, (collection_f)slaveselection_signal_lua_callback, &ctx);
	object->ret.ptr = ctx.ptr;

	/*if(lua_gcmonitor()) {
		SLAVESELECTION_DBG("%u Kbytes for LUA with %u threshold.", current_gc_count, current_gc_threshold);
	}*/

	return 1;
}

/* call all scripts for the given operation */
/*struct slave_connection *slaveselection_call(char *operation, unsigned int param_count, struct event_parameter *params) {
	struct {
		char *operation;
		unsigned int param_count;
		struct event_parameter *params;
		struct slave_connection *cnx;
	} ctx = { operation, param_count, params, NULL };

	collection_iterate(events_collection, slaveselection_call_callback, &ctx);

	return ctx.cnx;
}*/

/* call the slave selection scripts. */
struct slave_connection *call_selectiondown(struct collection *selection, struct vfs_element *file) {
	struct event_parameter params[2];
	struct event_object object;

	object.param_count = 2;
	object.params = &params[0];
	object.ret.ptr = NULL;

	params[0].ptr = file;
	params[0].type = "vfs_element";

	params[1].ptr = selection;
	params[1].type = "_collection";

	signal_raise(slaveselection_signal_down, &object);

	return object.ret.ptr;
}

/* call the slave selection scripts. */
struct slave_connection *call_selectionup(struct collection *selection, struct vfs_element *folder) {
	struct event_parameter params[2];
	struct event_object object;

	object.param_count = 2;
	object.params = &params[0];
	object.ret.ptr = NULL;

	params[0].ptr = folder;
	params[0].type = "vfs_element";

	params[1].ptr = selection;
	params[1].type = "_collection";

	signal_raise(slaveselection_signal_up, &object);

	return object.ret.ptr;
}

static int build_selectiondown_list(struct collection *c, void *item, void *param) {
	struct slave_connection *cnx = item;
	struct collection *selection = param;

	collection_add(selection, cnx);
	collection_movelast(selection, cnx);

	return 1;
}

static int build_selectionup_list(struct collection *c, void *item, void *param) {
	struct slave_ctx *slave = item;
	struct collection *selection = param;

	if(!slave->cnx) return 1;

	collection_add(selection, slave->cnx);
	collection_movelast(selection, slave->cnx);

	return 1;
}

static int get_less_busy_connection(struct collection *c, void *item, void *param) {
	struct slave_connection *cnx = item;
	struct {
		unsigned int connections;
		struct slave_connection *cnx;
	} *ctx = param;

	if(!ctx->cnx || (ctx->connections > collection_size(cnx->xfers))) {
		ctx->connections = collection_size(cnx->xfers);
		ctx->cnx = cnx;
	}

	return 1;
}

struct slave_connection *slaveselection_download(struct vfs_element *file) {
	struct collection *selection;
	struct {
		unsigned int connections; /* connection count for the selected slave- needed for tracking in the callback */
		struct slave_connection *cnx;
	} ctx = { 0, NULL };

	if(file->type != VFS_FILE) {
		/* can't download folders */
		SLAVESELECTION_DBG("Parameter error");
		return NULL;
	}

	if(!collection_size(file->available_from)) {
		/* file is unavailable */
		//SLAVESELECTION_DBG("Unavailable");
		return NULL;
	}

	selection = collection_new(C_NONE);

	/* transfer all available slaves into the collection */
	collection_iterate(file->available_from, build_selectiondown_list, selection);

	/* give a chance to select a slave from the scripts */
	ctx.cnx = call_selectiondown(selection, file);
	if(ctx.cnx) {
		collection_destroy(selection);
		collection_movelast(file->available_from, ctx.cnx);
		return ctx.cnx;
	}

	/* if the slaveselection list is empty after going thru all filters,
		fill it up again to choose a default slave */
	if(!collection_size(selection)) {
		SLAVESELECTION_DBG("selection-down list was emptied by scripts");
		/* transfer all available slaves into the collection */
		collection_iterate(file->available_from, build_selectiondown_list, selection);
	}
	
	/* try select the less busy slave */
	collection_iterate(selection, get_less_busy_connection, &ctx);
	collection_destroy(selection);

	if(ctx.cnx) {
		collection_movelast(file->available_from, ctx.cnx);
	}

	return ctx.cnx;
}

struct slave_connection *slaveselection_upload(struct vfs_element *folder) {
	struct collection *selection;
	struct {	
		unsigned int connections; /* connection count for the selected slave- needed for tracking in the callback */
		struct slave_connection *cnx;
	} ctx = { 0, NULL };
	struct vfs_section *section;

	if(folder->type != VFS_FOLDER) {
		SLAVESELECTION_DBG("Parameter error");
		/* can't upload in something that is not a folder */
		return NULL;
	}

	/* recursively search for a section mapped
		to this directory, or to an upper directory
		level */
	section = vfs_get_section(folder);
	if(!section) {
		/* can't upload if the folder doesn't have a section */
		SLAVESELECTION_DBG("No section mapped on folder %s", folder->name);
		return NULL;
	}
	
	if(!collection_size(section->slaves)) {
		/* No transfer-slave available ... */
		return NULL;
	}

	selection = collection_new(C_NONE);

	/* transfer all available slave connections into the collection */
	collection_iterate(section->slaves, build_selectionup_list, selection);

	/* give a chance to select a slave from the scripts */
	ctx.cnx = call_selectionup(selection, folder);
	if(ctx.cnx) {
		collection_destroy(selection);
		collection_movelast(section->slaves, ctx.cnx->slave);
		return ctx.cnx;
	}

	/* if the slaveselection list is empty after going thru all filters,
		fill it up again to choose a default slave */
	if(!collection_size(selection)) {
		SLAVESELECTION_DBG("selection-up list was emptied by scripts");
		/* transfer all available slaves into the collection */
		collection_iterate(section->slaves, build_selectionup_list, selection);
	}

	/* try select the less busy slave */
	collection_iterate(selection, get_less_busy_connection, &ctx);
	collection_destroy(selection);

	if(ctx.cnx) {
		collection_movelast(section->slaves, ctx.cnx->slave);
	}

	return ctx.cnx;
}

int slaveselection_register(char *name, const char *function) {
	struct event_ctx *ctx;

	if(!name || !function) return 0;

	ctx = event_get(name);
	if(!ctx) {
		ctx = event_create(name);
		if(!ctx) {
			EVENTS_DBG("Memory error");
			return 0;
		}

		/* register this callback so we'll have it called on this event */
		event_signal_add(name, (signal_f)slaveselection_signal_callback, ctx);
	}

	/* add the callback to the event context */
	event_get_callback(ctx, function, 1);

	return 1;
}
