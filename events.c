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

#include "logging.h"
#include "ftpd.h"
#include "constants.h"
#include "events.h"
#include "collection.h"
#include "config.h"
#include "luainit.h"
#include "time.h"
#include "users.h"
#include "mirror.h"

/*
	Holds all signals.
*/
static struct collection *event_signals = NULL; /* signal_ctx */

/*
	Holds all event callbacks.
*/
static struct collection *event_group = NULL; /* signal_callback */

/*
	All signal callbacks are raised with an object
	of type event_object and the parameter passed
	to the callback reference an event_ctx structure
	in wich there is a collection of all LUA callbacks
	to be called. This is the most efficient way we
	can do this.
*/
static struct collection *events = NULL; /* event_ctx */

static unsigned int event_get_matcher(struct collection *c, struct event_ctx *ctx, char *name) {

	return !stricmp(ctx->name, name);
}

struct event_ctx *event_get(const char *name) {

	if(!name) return NULL;

	return collection_match(events, (collection_f)event_get_matcher, (void *)name);
}

struct event_ctx *event_create(const char *name) {
	struct event_ctx *ctx;

	if(!name) return NULL;

	ctx = malloc(sizeof(struct event_ctx));
	if(!ctx) {
		EVENTS_DBG("Memory error");
		return NULL;
	}

	ctx->callbacks = collection_new();
	ctx->name = strdup(name);
	if(!ctx->name) {
		EVENTS_DBG("Memory error");
		free(ctx);
		return NULL;
	}

	if(!collection_add(events, ctx)) {
		EVENTS_DBG("Collection error");
		free(ctx->name);
		free(ctx);
		return NULL;
	}

	return ctx;
}

static unsigned int event_get_callback_matcher(struct collection *c, struct event_callback *cb, char *function) {

	return !stricmp(cb->function, function);
}

struct event_callback *event_get_callback(struct event_ctx *ctx, const char *function, int create) {
	struct event_callback *cb;

	if(!function) return NULL;

	cb = collection_match(ctx->callbacks, (collection_f)event_get_callback_matcher, (void *)function);
	if(!cb && create) {

		cb = malloc(sizeof(struct event_callback));
		if(!cb) {
			EVENTS_DBG("Memory error");
			return NULL;
		}

		cb->function = strdup(function);
		if(!cb->function) {
			EVENTS_DBG("Memory error");
			free(ctx);
			return NULL;
		}

		if(!collection_add(ctx->callbacks, cb)) {
			EVENTS_DBG("Collection error");
			free(cb->function);
			free(cb);
			return NULL;
		}
	}

	return cb;
}

static int event_signal_lua_callback(struct collection *c, struct event_callback *cb, void *param) {
	struct {
		struct event_object *object;
		int status;
	} *ctx = param;

	int stacktop = lua_gettop(L);
	char *errval, *errmsg;
	unsigned int i, err;
	lua_Number n;

	//unsigned int gc_count = lua_getgccount(L);

	lua_pushstring(L, cb->function);
	lua_gettable(L, LUA_GLOBALSINDEX);

	/* make sure the function has been found */
	if(!lua_isfunction(L, -1)) {
		EVENTS_DBG("%s is not a function", cb->function);
		lua_pop(L, 1);
		if(stacktop != lua_gettop(L)) {
			EVENTS_DBG("ERROR: prev stack top mismatch current stack top (%d, was %d)!", lua_gettop(L), stacktop);
		}
		lua_setgcthreshold(L, 0);
		/*if(lua_getgccount(L) > gc_count) {
			EVENTS_DBG("WARNING: in function %s: some data cannot be garbage collected (now %u Kbytes).",
				cb->function, lua_getgccount(L));
		}*/
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

		EVENTS_DBG("error catched:");
		EVENTS_DBG("  --> function: %s", cb->function);
		EVENTS_DBG("  --> error value: %s", errval);
		EVENTS_DBG("  --> error message: %s", errmsg);
	} else {

		/* make sure the return value is a number */
		if(!lua_isnumber(L, -1)) {
			EVENTS_DBG("[%s] returned non-number type, dropping.", cb->function);
			lua_pop(L, 1);
			if(stacktop != lua_gettop(L)) {
				EVENTS_DBG("ERROR: prev stack top mismatch current stack top (%d, was %d)!", lua_gettop(L), stacktop);
			}
			lua_setgcthreshold(L, 0);
			/*if(lua_getgccount(L) > gc_count) {
				EVENTS_DBG("WARNING: in function %s: some data cannot be garbage collected (now %u Kbytes).",
					cb->function, lua_getgccount(L));
			}*/
			return 1;
		}

		n = lua_tonumber(L, -1);

		if(!(int)n) {
			ctx->status = 0;
			lua_pop(L, 1); /* pops the return value */
			if(stacktop != lua_gettop(L)) {
				EVENTS_DBG("ERROR: prev stack top mismatch current stack top (%d, was %d)!", lua_gettop(L), stacktop);
			}
			lua_setgcthreshold(L, 0);
			/*if(lua_getgccount(L) > gc_count) {
				EVENTS_DBG("WARNING: in function %s: some data cannot be garbage collected (now %u Kbytes).",
					cb->function, lua_getgccount(L));
			}*/
			return 0;
		}
	}

	lua_pop(L, 1); /* pops the return value */
	if(stacktop != lua_gettop(L)) {
		EVENTS_DBG("ERROR: prev stack top mismatch current stack top (%d, was %d)!", lua_gettop(L), stacktop);
	}
	lua_setgcthreshold(L, 0);
	/*if(lua_getgccount(L) > gc_count) {
		EVENTS_DBG("WARNING: in function %s: some data cannot be garbage collected (now %u Kbytes).",
			cb->function, lua_getgccount(L));
	}*/

	return 1;
}

int event_signal_callback(struct event_object *object, struct event_ctx *event) {
	struct {
		struct event_object *object;
		int status;
	} ctx = { object, 1 };

	if(object->param_count && !object->params) {
		EVENTS_DBG("%s could not be raised, param error.", event->name);
		return 1;
	}

	collection_iterate(event->callbacks, (collection_f)event_signal_lua_callback, &ctx);
	object->ret.success = ctx.status;

	/*if(lua_gcmonitor()) {
		EVENTS_DBG("%u Kbytes for LUA with %u threshold.", current_gc_count, current_gc_threshold);
	}*/

	return 1;
}

struct signal_callback *event_signal_add(char *name, int (*callback)(void *obj, void *param), void *param) {

	if(!event_signals) {
		event_signals = collection_new();
	}
	if(!event_group) {
		event_group = collection_new();
	}

	/* register this callback so we'll have it called on this event */
	return signal_add(event_signals, event_group, name, callback, param);
}

int event_register(char *name, char *function) {
	struct event_ctx *ctx;

	if(!name || !function) return 0;

	if(!event_signals) {
		event_signals = collection_new();
	}
	if(!event_group) {
		event_group = collection_new();
	}
	if(!events) {
		events = collection_new();
	}

	ctx = event_get(name);
	if(!ctx) {
		ctx = event_create(name);
		if(!ctx) {
			EVENTS_DBG("Memory error");
			return 0;
		}

		/* register this callback so we'll have it called on this event */
		event_signal_add(name, (signal_f)event_signal_callback, ctx);
	}

	/* add the callback to the event context */
	event_get_callback(ctx, function, 1);

	return 1;
}

/*
	Return an already existing or create a new signal context with the given name.
*/
struct signal_ctx *event_signal_get(const char *name, int create) {

	if(!event_signals) {
		event_signals = collection_new();
	}

	return signal_get(event_signals, name, create);
}

int events_init() {
	
	EVENTS_DBG("Loading ...");

	if(!event_signals) {
		event_signals = collection_new();
	}
	if(!event_group) {
		event_group = collection_new();
	}
	if(!events) {
		events = collection_new();
	}

	return 1;
}

int events_clear() {

	EVENTS_DBG("Clearing events...");

	signal_clear(event_group);
	/* there should be no more signals ... */

	while(collection_size(events)) {
		struct event_ctx *ctx = collection_first(events);
		
		free(ctx->name);
		ctx->name = NULL;

		while(collection_size(ctx->callbacks)) {
			struct event_callback *cb = collection_first(ctx->callbacks);

			free(cb->function);
			cb->function = NULL;

			free(cb);
			collection_delete(ctx->callbacks, cb);
		}
		collection_destroy(ctx->callbacks);
		ctx->callbacks = NULL;

		free(ctx);
		collection_delete(events, ctx);
	}

	return 1;
}

void events_free() {

	EVENTS_DBG("Unloading...");

	events_clear();

	if(event_signals) {
		collection_destroy(event_signals);
		event_group = NULL;
	}
	if(event_group) {
		collection_destroy(event_group);
		event_group = NULL;
	}
	if(events) {
		collection_destroy(events);
		events = NULL;
	}
	
	return;
}

/*
static unsigned int event_raise_callback(struct collection *c, void *item, void *param) {


	return 1;
}
*/

// exported to lua
// call all events registered under the specified name
// return 0 if any event return 0
/*unsigned int event_raise(char *function, unsigned int param_count, struct event_parameter *params) {
	struct {
		char *function;
		unsigned int param_count;
		struct event_parameter *params;
		unsigned int status;
	} ctx = { function, param_count, params, 1 };

	if(!function) {
		EVENTS_DBG("Params error");
		return 0;
	}

	if(param_count && !params) {
		EVENTS_DBG("%s could not be raised, param error.", function);
		return 0;
	}

	collection_iterate(events_collection, event_raise_callback, &ctx);

	return ctx.status;
}*/

int event_raise(char *name, unsigned int param_count, struct event_parameter *params) {
	struct event_object o;

	o.param_count = param_count;
	o.params = params;
	o.ret.success = 1;
	
	signal_raise(event_signal_get(name, 0), &o);

	return o.ret.success;
}

// can be called from anywhere
unsigned int event_onReload() {

	EVENTS_CALLS_DBG("onReload");

	return event_raise("onReload", 0, NULL);
}

/* new user has been created */
void event_onNewUser(struct user_ctx *user) {
	struct event_parameter params[1];

	params[0].ptr = user;
	params[0].type = "user_ctx";
	
	EVENTS_CALLS_DBG("onNewUser");

	event_raise("onNewUser", 1, &params[0]);

	return;
}

/* user has been deleted */
void event_onDeleteUser(struct user_ctx *user) {
	struct event_parameter params[1];

	params[0].ptr = user;
	params[0].type = "user_ctx";
	
	EVENTS_CALLS_DBG("onDeleteUser");

	event_raise("onDeleteUser", 1, &params[0]);

	return;
}

/* data may be sent to the client from this point on
	return 0 to terminate the connection */
unsigned int event_onClientConnect(struct ftpd_client_ctx *client) {
	struct event_parameter params[1];

	params[0].ptr = client;
	params[0].type = "ftpd_client";
	
	EVENTS_CALLS_DBG("onClientConnect");

	return event_raise("onClientConnect", 1, &params[0]);
}

/* no data can be sent to the client at this point */
unsigned int event_onClientLoginSuccess(struct ftpd_client_ctx *client) {
	struct event_parameter params[1];

	params[0].ptr = client;
	params[0].type = "ftpd_client";
	
	EVENTS_CALLS_DBG("onClientLoginSuccess");

	return event_raise("onClientLoginSuccess", 1, &params[0]);
}

/* no data can be sent to the client at this point */
void event_onClientLoginFail(struct ftpd_client_ctx *client) {
	struct event_parameter params[1];

	params[0].ptr = client;
	params[0].type = "ftpd_client";
	
	EVENTS_CALLS_DBG("onClientLoginFail");

	event_raise("onClientLoginFail", 1, &params[0]);

	return;
}

/* no data can be sent to the client at this point */
void event_onClientDisconnect(struct ftpd_client_ctx *client) {
	struct event_parameter params[1];

	params[0].ptr = client;
	params[0].type = "ftpd_client";
	
	EVENTS_CALLS_DBG("onClientDisconnect");

	event_raise("onClientDisconnect", 1, &params[0]);

	return;
}

/* return 0 to terminate the connection */
unsigned int event_onSlaveConnect(struct slave_connection *cnx) {
	struct event_parameter params[1];

	params[0].ptr = cnx;
	params[0].type = "slave_connection";
	
	EVENTS_CALLS_DBG("onSlaveConnect");

	return event_raise("onSlaveConnect", 1, &params[0]);
}

/* return 0 to terminate the connection */
unsigned int event_onSlaveIdentSuccess(struct slave_connection *cnx) {
	struct event_parameter params[1];
	unsigned int ret;

	params[0].ptr = cnx;
	params[0].type = "slave_connection";
	
	EVENTS_CALLS_DBG("onSlaveIdentSuccess");

	ret = event_raise("onSlaveIdentSuccess", 1, &params[0]);

	return ret;
}

unsigned int event_onSlaveIdentFail(struct slave_connection *cnx, struct slave_hello_data *hello) {
	struct event_parameter params[2];
	unsigned int ret;

	params[0].ptr = cnx;
	params[0].type = "slave_connection";

	params[1].ptr = hello;
	params[1].type = "hello_data";
	
	EVENTS_CALLS_DBG("onSlaveIdentFail");

	ret = event_raise("onSlaveIdentFail", 2, &params[0]);

	return ret;
}

void event_onSlaveDisconnect(struct slave_connection *cnx) {
	struct event_parameter params[1];

	params[0].ptr = cnx;
	params[0].type = "slave_connection";
	
	EVENTS_CALLS_DBG("onSlaveDisconnect");

	event_raise("onSlaveDisconnect", 1, &params[0]);

	return;
}

/* return 0 to reject the request */
unsigned int event_onPreDownload(struct ftpd_client_ctx *client, struct vfs_element *file) {
	struct event_parameter params[2];

	params[0].ptr = client;
	params[0].type = "ftpd_client";

	params[1].ptr = file;
	params[1].type = "vfs_element";

	EVENTS_CALLS_DBG("onPreDownload");

	return event_raise("onPreDownload", 2, &params[0]);
}

/* return 0 to reject the request */
unsigned int event_onPreUpload(struct ftpd_client_ctx *client, struct vfs_element *file) {
	struct event_parameter params[2];

	params[0].ptr = client;
	params[0].type = "ftpd_client";

	params[1].ptr = file;
	params[1].type = "vfs_element";

	EVENTS_CALLS_DBG("onPreUpload");

	return event_raise("onPreUpload", 2, &params[0]);
}

unsigned int event_onTransferSuccess(struct ftpd_client_ctx *client, struct xfer_ctx *xfer) {
	struct event_parameter params[2];

	params[0].ptr = client;
	params[0].type = "ftpd_client";

	params[1].ptr = xfer;
	params[1].type = "client_xfer";

	EVENTS_CALLS_DBG("onTransferSuccess");

	return event_raise("onTransferSuccess", 2, &params[0]);
}

void event_onTransferFail(struct ftpd_client_ctx *client, struct xfer_ctx *xfer) {
	struct event_parameter params[2];

	params[0].ptr = client;
	params[0].type = "ftpd_client";

	params[1].ptr = xfer;
	params[1].type = "client_xfer";

	EVENTS_CALLS_DBG("onTransferFail");

	event_raise("onTransferFail", 2, &params[0]);

	return;
}

/* return 0 to deny the deletion */
unsigned int event_onPreDelete(struct ftpd_client_ctx *client, struct vfs_element *file) {
	struct event_parameter params[2];

	params[0].ptr = client;
	params[0].type = "ftpd_client";

	params[1].ptr = file;
	params[1].type = "vfs_element";

	EVENTS_CALLS_DBG("onPreDelete");

	return event_raise("onPreDelete", 2, &params[0]);
}

void event_onDelete(struct ftpd_client_ctx *client, struct vfs_element *file) {
	struct event_parameter params[2];

	params[0].ptr = client;
	params[0].type = "ftpd_client";

	params[1].ptr = file;
	params[1].type = "vfs_element";

	EVENTS_CALLS_DBG("onDelete");

	event_raise("onDelete", 2, &params[0]);

	return;
}

/* return 0 to deny the directory creation */
unsigned int event_onPreMakeDir(struct ftpd_client_ctx *client, struct vfs_element *directory) {
	struct event_parameter params[2];

	params[0].ptr = client;
	params[0].type = "ftpd_client";

	params[1].ptr = directory;
	params[1].type = "vfs_element";

	EVENTS_CALLS_DBG("onPreMakeDir");

	return event_raise("onPreMakeDir", 2, &params[0]);
}

void event_onMakeDir(struct ftpd_client_ctx *client, struct vfs_element *directory) {
	struct event_parameter params[2];

	params[0].ptr = client;
	params[0].type = "ftpd_client";

	params[1].ptr = directory;
	params[1].type = "vfs_element";

	EVENTS_CALLS_DBG("onMakeDir");

	event_raise("onMakeDir", 2, &params[0]);

	return;
}

/* return 0 to deny the directory deletion */
unsigned int event_onPreRemoveDir(struct ftpd_client_ctx *client, struct vfs_element *directory) {
	struct event_parameter params[2];

	params[0].ptr = client;
	params[0].type = "ftpd_client";

	params[1].ptr = directory;
	params[1].type = "vfs_element";

	EVENTS_CALLS_DBG("onPreRemoveDir");

	return event_raise("onPreRemoveDir", 2, &params[0]);
}

void event_onRemoveDir(struct ftpd_client_ctx *client, struct vfs_element *directory) {
	struct event_parameter params[2];

	params[0].ptr = client;
	params[0].type = "ftpd_client";

	params[1].ptr = directory;
	params[1].type = "vfs_element";

	EVENTS_CALLS_DBG("onRemoveDir");

	event_raise("onRemoveDir", 2, &params[0]);

	return;
}

/* return 0 to deny the directory change */
unsigned int event_onPreChangeDir(struct ftpd_client_ctx *client, struct vfs_element *directory) {
	struct event_parameter params[2];

	params[0].ptr = client;
	params[0].type = "ftpd_client";

	params[1].ptr = directory;
	params[1].type = "vfs_element";

	EVENTS_CALLS_DBG("onPreChangeDir");

	return event_raise("onPreChangeDir", 2, &params[0]);
}

void event_onChangeDir(struct ftpd_client_ctx *client, struct vfs_element *directory) {
	struct event_parameter params[2];

	params[0].ptr = client;
	params[0].type = "ftpd_client";

	params[1].ptr = directory;
	params[1].type = "vfs_element";

	EVENTS_CALLS_DBG("onChangeDir");

	event_raise("onChangeDir", 2, &params[0]);

	return;
}

/* data may be injected in the directory list here */
void event_onList(struct ftpd_client_ctx *client, struct vfs_element *directory) {
	struct event_parameter params[2];

	params[0].ptr = client;
	params[0].type = "ftpd_client";

	params[1].ptr = directory;
	params[1].type = "vfs_element";

	EVENTS_CALLS_DBG("onList");

	event_raise("onList", 2, &params[0]);

	return;
}
