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

#include <tolua++.h>

#include "constants.h"

#include "luainit.h"
#include "events.h"
#include "slaveselection.h"

static int luaevents_event_signal_lua_callback(struct collection *c, struct event_callback *cb, void *param) {
	struct {
		struct event_object *object;
		int status;
	} *ctx = param;
	int ret = 1;
	lua_State *L = cb->script->L;
	
	lua_pushcfunction(L, luainit_traceback);
	
	luainit_tget(L, EVENTS_REFTABLE, cb->function_index);
	if(lua_isfunction(L, -1)) {
		unsigned int i;
		int err;
		
		/* push all params */
		for(i=0;i<ctx->object->param_count;i++) {
			tolua_pushusertype(L, ctx->object->params[i].ptr, ctx->object->params[i].type);
		}
		
		/* call the function with two params and one return */
		err = lua_pcall(L, ctx->object->param_count, 1, -(2+ctx->object->param_count));
		if(err) {
			/* do something with the error ... ? */
			luainit_error(L, "(calling event callback)", err);
		} else {
			ret = lua_isnumber(L, -1) ? (int)lua_tonumber(L, -1) : (int)tolua_toboolean(L, -1, ret);
			if(!ret) ctx->status = ret;
		}
		
		/* pops the error message or the return value */
		lua_pop(L, 1);
	} else {
		/* pops the thing we just pushed that is not a function */
		lua_pop(L, 1);
	}
	lua_pop(L, 1); /* pops the errfunc */
	
	return ret;
}

static int luaevents_event_signal_callback(struct event_object *object, struct event_ctx *event) {
	struct {
		struct event_object *object;
		int status;
	} ctx = { object, 1 };
	
	if(object->param_count && !object->params) {
		EVENTS_DBG("%s could not be raised, param error.", event->name);
		return 1;
	}
	
	collection_iterate(event->callbacks, (collection_f)luaevents_event_signal_lua_callback, &ctx);
	object->ret.success = ctx.status;
	
	return 1;
}

static int luaevents_event_add(lua_State* L)
{
#ifndef TOLUA_RELEASE
	tolua_Error tolua_err;
	if (
		!tolua_isstring(L,1,0,&tolua_err) ||
		!tolua_isfunction(L,2,0,&tolua_err) ||
		!tolua_isnoobj(L,3,&tolua_err)
	) {
		goto tolua_lerror;
	} else
#endif
	{
		const char *name = lua_tostring(L, 1);
		int function_index = luainit_tinsert(L, EVENTS_REFTABLE, 2);
		struct script_ctx *script = script_resolve(L);
		
		struct event_ctx *ctx;
		/*
		if(!event_signals) event_signals = collection_new(C_CASCADE);
		if(!event_group) event_group = collection_new(C_CASCADE);
		if(!events) events = collection_new(C_CASCADE);
		*/
		ctx = event_get(name);
		if(!ctx) {
			ctx = event_create(name);
			if(!ctx) {
				EVENTS_DBG("Memory error");
				return 0;
			}
			
			/* register this callback so we'll have it called on this event */
			event_signal_add(name, (signal_f)luaevents_event_signal_callback, ctx);
		}
		
		/* add the callback to the event context */
		event_add_callback(ctx, script, function_index);
	}
	
	return 0;
#ifndef TOLUA_RELEASE
tolua_lerror:
	tolua_error(L,"#ferror in function luaevents_event_add.",&tolua_err);
	return 0;
#endif
}

static int luaevents_slaveselection_signal_lua_callback(struct collection *c, struct event_callback *cb, void *param) {
	struct {
		struct event_object *object;
		void *ptr;
	} *ctx = param;
	int ret = 1;
	lua_State *L = cb->script->L;
	
	lua_pushcfunction(L, luainit_traceback);
	
	luainit_tget(L, SLAVESELECTION_REFTABLE, cb->function_index);
	if(lua_isfunction(L, -1)) {
		unsigned int i;
		int err;
		
		/* push the parameters */
		for(i=0;i<ctx->object->param_count;i++) {
			tolua_pushusertype(L, ctx->object->params[i].ptr, ctx->object->params[i].type);
		}
		
		/* call the function with two params and one return */
		err = lua_pcall(L, ctx->object->param_count, 1, -(2+ctx->object->param_count));
		if(err) {
			/* do something with the error ... ? */
			luainit_error(L, "(calling slaveselection callback)", err);
		} else {
			tolua_Error err;
			
			if(tolua_isusertype(L, 1, "slave_connection", 0, &err)) {
				ctx->object->ret.ptr = (struct slave_connection *)tolua_tousertype(L, 1, 0);
				
				ret = 0;
			}
		}
		
		/* pops the error message or the return value */
		lua_pop(L, 1);
	} else {
		/* pops the thing we just pushed that is not a function */
		lua_pop(L, 1);
	}
	lua_pop(L, 1); /* pops the errfunc */
	
	return ret;
}

static int luaevents_slaveselection_signal_callback(struct event_object *object, struct event_ctx *event) {
	struct {
		struct event_object *object;
		void *ptr;
	} ctx = { object, NULL };

	if(object->param_count && !object->params) {
		SLAVESELECTION_DBG("%s could not be raised, param error.", event->name);
		return 0;
	}

	collection_iterate(event->callbacks, (collection_f)luaevents_slaveselection_signal_lua_callback, &ctx);
	object->ret.ptr = ctx.ptr;

	return 1;
}

static int luaevents_slaveselection_add(lua_State *L) {
#ifndef TOLUA_RELEASE
	tolua_Error tolua_err;
	if (
		!tolua_isstring(L,1,0,&tolua_err) ||
		!tolua_isfunction(L,2,0,&tolua_err) ||
		!tolua_isnoobj(L,3,&tolua_err)
	) {
		goto tolua_lerror;
	} else
#endif
	{
		const char *name = lua_tostring(L, 1);
		int function_index = luainit_tinsert(L, SLAVESELECTION_REFTABLE, 2);
		struct script_ctx *script = script_resolve(L);
		
		struct event_ctx *ctx;
		
		ctx = event_get(name);
		if(!ctx) {
			ctx = event_create(name);
			if(!ctx) {
				EVENTS_DBG("Memory error");
				return 0;
			}
			
			/* register this callback so we'll have it called on this event */
			event_signal_add(name, (signal_f)luaevents_slaveselection_signal_callback, ctx);
		}
		
		/* add the callback to the event context */
		event_add_callback(ctx, script, function_index);
	}
	return 0;
#ifndef TOLUA_RELEASE
tolua_lerror:
	tolua_error(L,"#ferror in function luaevents_slaveselection_add.",&tolua_err);
	return 0;
#endif
	
}

TOLUA_API int luaopen_xftpd_events(lua_State* L)
{
	luainit_tcreate(L, EVENTS_REFTABLE);
	luainit_tcreate(L, SLAVESELECTION_REFTABLE);
	
	tolua_module(L,NULL,1);
	tolua_beginmodule(L,NULL);
		tolua_module(L,"event",1);
		tolua_beginmodule(L,"event");
			tolua_function(L,"add", luaevents_event_add);
		tolua_endmodule(L);
		
		tolua_module(L,"slaveselection",1);
		tolua_beginmodule(L,"slaveselection");
			tolua_function(L,"add", luaevents_slaveselection_add);
		tolua_endmodule(L);
	tolua_endmodule(L);
	
	return 1;
}
