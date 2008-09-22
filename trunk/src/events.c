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
#include "luainit.h"

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

	return !strcasecmp(ctx->name, name);
}

struct event_ctx *event_get(const char *name) {

	if(!name) return NULL;

	return collection_match(events, (collection_f)event_get_matcher, (void *)name);
}

static void event_obj_destroy(struct event_ctx *ctx) {
	
	collectible_destroy(ctx);
	
	free(ctx->name);
	ctx->name = NULL;
	
	collection_destroy(ctx->callbacks);
	ctx->callbacks = NULL;

	free(ctx);
	
	return;
}

struct event_ctx *event_create(const char *name) {
	struct event_ctx *ctx;

	if(!name) return NULL;

	ctx = malloc(sizeof(struct event_ctx));
	if(!ctx) {
		EVENTS_DBG("Memory error");
		return NULL;
	}
	
	obj_init(&ctx->o, ctx, (obj_f)event_obj_destroy);
	collectible_init(ctx);

	ctx->callbacks = collection_new(C_CASCADE);
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

/*
static unsigned int event_get_callback_matcher(struct collection *c, struct event_callback *cb, char *function) {

	return !strcasecmp(cb->function, function);
}
*/

static void event_callback_obj_destroy(struct event_callback *cb) {
	
	collectible_destroy(cb);
	
	if(cb->function_index != -1 && cb->script && cb->script->L) {
		luainit_tremove(cb->script->L, EVENTS_REFTABLE, cb->function_index);
	} else {
		EVENTS_DBG("Can't free function_index %d", cb->function_index);
	}
	cb->function_index = -1;

	free(cb);
	
	return;
}

struct event_callback *event_add_callback(struct event_ctx *ctx, struct script_ctx *script, int function_index) {
	struct event_callback *cb;
	
	if(function_index == -1) return NULL;

	cb = malloc(sizeof(struct event_callback));
	if(!cb) {
		EVENTS_DBG("Memory error");
		return NULL;
	}
	
	obj_init(&cb->o, cb, (obj_f)event_callback_obj_destroy);
	collectible_init(cb);
	
	cb->script = script;
	cb->function_index = function_index;
	
	if(!collection_add(cb->script->events, cb)) {
		EVENTS_DBG("Collection error");
		cb->function_index = -1;
		free(cb);
		return NULL;
	}
	
	if(!collection_add(ctx->callbacks, cb)) {
		EVENTS_DBG("Collection error");
		collection_delete(cb->script->events, cb);
		cb->function_index = -1;
		free(cb);
		return NULL;
	}
	
	return cb;
}

struct signal_callback *event_signal_add(const char *name, int (*callback)(void *obj, void *param), void *param) {

	if(!event_signals) {
		event_signals = collection_new(C_CASCADE);
	}
	if(!event_group) {
		event_group = collection_new(C_CASCADE);
	}

	/* register this callback so we'll have it called on this event */
	return signal_add(event_signals, event_group, name, callback, param);
}

/*
	Return an already existing or create a new signal context with the given name.
*/
struct signal_ctx *event_signal_get(const char *name, int create) {

	if(!event_signals) {
		event_signals = collection_new(C_CASCADE);
	}

	return signal_get(event_signals, name, create);
}

int events_init() {
	
	EVENTS_DBG("Loading ...");

	if(!event_signals) {
		event_signals = collection_new(C_CASCADE);
	}
	if(!event_group) {
		event_group = collection_new(C_CASCADE);
	}
	if(!events) {
		events = collection_new(C_CASCADE);
	}

	return 1;
}

int events_clear() {

	EVENTS_DBG("Clearing events...");

	signal_clear(event_group);
	/* there should be no more signals ... */
	
	collection_empty(events);

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

int event_raise(char *name, unsigned int param_count, struct event_parameter *params) {
	struct event_object o;
	struct signal_ctx *s;

	o.param_count = param_count;
	o.params = params;
	o.ret.success = 1;
	
	s = event_signal_get(name, 0);
	if(s) {
		signal_raise(s, &o);
	}

	return o.ret.success;
}

unsigned int event_onPreReload() {

	EVENTS_CALLS_DBG("onPreReload");

	return event_raise("onPreReload", 0, NULL);
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
