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
#include "collection.h"

static int luacollection_match_callback(struct collection *c, void *item, void *param) {
	struct {
		lua_State* L;
		int callback_index;
		const char *casttype;
	} *ctx = param;
	int err;
	int ret = 0; /* default to zero here (zero = no match) */
	
	lua_pushcfunction(ctx->L, luainit_traceback);
	
	tolua_pushvalue(ctx->L, ctx->callback_index);
	if(lua_isfunction(ctx->L, -1)) {
		
		/* push the descriptor */
		tolua_pushusertype(ctx->L,(void*)c,"collection");
		
		/* push the group */
		tolua_pushusertype(ctx->L,(void*)item,ctx->casttype);
		
		/* call the function with two params and one return */
		err = lua_pcall(ctx->L, 2, 1, -4);
		if(err) {
			/* do something with the error ... ? */
			luainit_error(ctx->L, "(calling collection matcher)", err);
		} else {
			ret = (int)tolua_toboolean(ctx->L, -1, ret);
		}
		
		/* pops the error message or the return value */
		lua_pop(ctx->L, 1);
	} else {
		/* pops the thing we just pushed that is not a function */
		lua_pop(ctx->L, 1);
	}
	
	lua_pop(ctx->L, 1); /* pops the errfunc */
	
	return ret;
}

//bool collection.match(collection *c, string cast, bool (*callback)(collection *c, void *item));
static int luacollection_match(lua_State* L)
{
#ifndef TOLUA_RELEASE
	tolua_Error tolua_err;
	if (
		!tolua_isusertype(L,1,"collection",0,&tolua_err) ||
		!tolua_isstring(L,2,0,&tolua_err) ||
		!tolua_isfunction(L,3,0,&tolua_err) ||
		!tolua_isnoobj(L,4,&tolua_err)
	) {
		goto tolua_lerror;
	} else
#endif
	{
		struct {
			lua_State* L;
			int callback_index;
			const char *casttype;
		} ctx = { L, 3, lua_tostring(L, 2) };
		struct collection* c = ((struct collection*)tolua_tousertype(L,1,0));
		void *ret;
		
		ret = collection_match(c, luacollection_match_callback, &ctx);
		if(ret)
			tolua_pushusertype(L,(void*)ret,ctx.casttype);
		else
			lua_pushnil(L);
	}
	
	return 1;
#ifndef TOLUA_RELEASE
tolua_lerror:
	tolua_error(L,"#ferror in function luacollection_match.",&tolua_err);
	return 0;
#endif
}

static int luacollection_iterate_callback(struct collection *c, void *item, void *param) {
	struct {
		lua_State* L;
		int callback_index;
		const char *casttype;
	} *ctx = param;
	int err;
	int ret = 1; /* default to one here (one = continue iterating) */
	
	lua_pushcfunction(ctx->L, luainit_traceback);
	
	tolua_pushvalue(ctx->L, ctx->callback_index);
	if(lua_isfunction(ctx->L, -1)) {
		
		/* push the descriptor */
		tolua_pushusertype(ctx->L,(void*)c,"collection");
		
		/* push the group */
		tolua_pushusertype(ctx->L,(void*)item,ctx->casttype);
		
		/* call the function with two params and one return */
		err = lua_pcall(ctx->L, 2, 1, -4);
		if(err) {
			/* do something with the error ... ? */
			luainit_error(ctx->L, "(calling collection iterator)", err);
		} else {
			ret = (int)tolua_toboolean(ctx->L, -1, ret);
		}
		
		/* pops the error message or the return value */
		lua_pop(ctx->L, 1);
	} else {
		/* pops the thing we just pushed that is not a function */
		lua_pop(ctx->L, 1);
	}
	
	lua_pop(ctx->L, 1); /* pops the errfunc */
	
	return ret;
}

//bool collection.iterate(collection *c, string cast, bool (*callback)(collection *c, void *item));
static int luacollection_iterate(lua_State* L)
{
#ifndef TOLUA_RELEASE
	tolua_Error tolua_err;
	if (
		!tolua_isusertype(L,1,"collection",0,&tolua_err) ||
		!tolua_isstring(L,2,0,&tolua_err) ||
		!tolua_isfunction(L,3,0,&tolua_err) ||
		!tolua_isnoobj(L,4,&tolua_err)
	) {
		goto tolua_lerror;
	} else
#endif
	{
		struct {
			lua_State* L;
			int callback_index;
			const char *casttype;
		} ctx = { L, 3, lua_tostring(L, 2) };
		struct collection* c = ((struct collection*)tolua_tousertype(L,1,0));
		int ret;
		
		ret = collection_iterate(c, luacollection_iterate_callback, &ctx);
		lua_pushboolean(L, ret);
	}
	
	return 1;
#ifndef TOLUA_RELEASE
tolua_lerror:
	tolua_error(L,"#ferror in function luacollection_iterate.",&tolua_err);
	return 0;
#endif
}

static int luacollection_destroy(lua_State* tolua_S)
{
#ifndef TOLUA_RELEASE
	tolua_Error tolua_err;
	if (
		!tolua_isusertype(tolua_S,1,"collection",0,&tolua_err) ||
		!tolua_isnoobj(tolua_S,2,&tolua_err)
	)
		goto tolua_lerror;
	else
#endif
	{
		collection* c = ((collection*)  tolua_tousertype(tolua_S,1,0));
		{
			collection_destroy(c);
		}
	}
	return 0;
#ifndef TOLUA_RELEASE
	tolua_lerror:
	tolua_error(tolua_S,"#ferror in function 'destroy'.",&tolua_err);
	return 0;
#endif
}

static int luacollection_size(lua_State* tolua_S)
{
#ifndef TOLUA_RELEASE
	tolua_Error tolua_err;
	if (
		!tolua_isusertype(tolua_S,1,"collection",0,&tolua_err) ||
		!tolua_isnoobj(tolua_S,2,&tolua_err)
	)
		goto tolua_lerror;
	else
#endif
	{
		collection* c = ((collection*)  tolua_tousertype(tolua_S,1,0));
		{
			unsigned int tolua_ret = (unsigned int)  collection_size(c);
			tolua_pushnumber(tolua_S,(lua_Number)tolua_ret);
		}
	}
	return 1;
#ifndef TOLUA_RELEASE
	tolua_lerror:
	tolua_error(tolua_S,"#ferror in function 'size'.",&tolua_err);
	return 0;
#endif
}

static int luacollection_add(lua_State* tolua_S)
{
#ifndef TOLUA_RELEASE
	tolua_Error tolua_err;
	if (
		!tolua_isusertype(tolua_S,1,"collection",0,&tolua_err) ||
		!tolua_isusertype(tolua_S,2,"collectible",0,&tolua_err) ||
		!tolua_isnoobj(tolua_S,3,&tolua_err)
	)
		goto tolua_lerror;
	else
#endif
	{
		collection* c = ((collection*)  tolua_tousertype(tolua_S,1,0));
		collectible* item = ((collectible*)  tolua_tousertype(tolua_S,2,0));
		{
			bool tolua_ret = (bool)  collection_c_add(c,item);
			tolua_pushboolean(tolua_S,(bool)tolua_ret);
		}
	}
	return 1;
#ifndef TOLUA_RELEASE
	tolua_lerror:
	tolua_error(tolua_S,"#ferror in function 'add'.",&tolua_err);
	return 0;
#endif
}

static int luacollection_delete(lua_State* tolua_S)
{
#ifndef TOLUA_RELEASE
	tolua_Error tolua_err;
	if (
		!tolua_isusertype(tolua_S,1,"collection",0,&tolua_err) ||
		!tolua_isusertype(tolua_S,2,"collectible",0,&tolua_err) ||
		!tolua_isnoobj(tolua_S,3,&tolua_err)
	)
		goto tolua_lerror;
	else
#endif
	{
		collection* c = ((collection*)  tolua_tousertype(tolua_S,1,0));
		collectible* item = ((collectible*)  tolua_tousertype(tolua_S,2,0));
		{
			bool tolua_ret = (bool)  collection_c_delete(c,item);
			tolua_pushboolean(tolua_S,(bool)tolua_ret);
		}
	}
	return 1;
#ifndef TOLUA_RELEASE
	tolua_lerror:
	tolua_error(tolua_S,"#ferror in function 'delete'.",&tolua_err);
	return 0;
#endif
}

static int luacollection_find(lua_State* tolua_S)
{
#ifndef TOLUA_RELEASE
	tolua_Error tolua_err;
	if (
		!tolua_isusertype(tolua_S,1,"collection",0,&tolua_err) ||
		!tolua_isusertype(tolua_S,2,"collectible",0,&tolua_err) ||
		!tolua_isnoobj(tolua_S,3,&tolua_err)
	)
		goto tolua_lerror;
	else
#endif
	{
		collection* c = ((collection*)  tolua_tousertype(tolua_S,1,0));
		collectible* item = ((collectible*)  tolua_tousertype(tolua_S,2,0));
		{
			bool tolua_ret = (bool)  collection_c_find(c,item);
			tolua_pushboolean(tolua_S,(bool)tolua_ret);
		}
	}
	return 1;
#ifndef TOLUA_RELEASE
	tolua_lerror:
	tolua_error(tolua_S,"#ferror in function 'find'.",&tolua_err);
	return 0;
#endif
}

TOLUA_API int luaopen_xftpd_collection(lua_State* L)
{
	tolua_module(L,NULL,1);
	tolua_beginmodule(L,NULL);
		tolua_module(L,"collection",1);
		tolua_beginmodule(L,"collection");
			tolua_function(L,"iterate", luacollection_iterate);
			tolua_function(L,"match", luacollection_match);
			tolua_function(L,"destroy", luacollection_destroy);
			tolua_function(L,"size", luacollection_size);
			tolua_function(L,"add", luacollection_add);
			tolua_function(L,"delete", luacollection_delete);
			tolua_function(L,"find", luacollection_find);
		tolua_endmodule(L);
		
		/* this is declared here instead of in luacollection.pkg because tolua 
			doesn't like it when a class and a typedef have the same name */
		tolua_usertype(L,"collection");
		tolua_cclass(L,"collection","collection","",NULL);
			tolua_function(L,"iterate", luacollection_iterate);
			tolua_function(L,"match", luacollection_match);
			tolua_function(L,"destroy", luacollection_destroy);
			tolua_function(L,"size", luacollection_size);
			tolua_function(L,"add", luacollection_add);
			tolua_function(L,"delete", luacollection_delete);
			tolua_function(L,"find", luacollection_find);
		tolua_endmodule(L);
	tolua_endmodule(L);
	
	return 1;
}
