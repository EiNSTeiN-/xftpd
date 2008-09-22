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
#include "mirror.h"

static int luamirror_callback(struct mirror_ctx *mirror, int success, void *param) {
	struct mirror_lua_ctx *lua_ctx = param;
	lua_State *L = lua_ctx->script->L;
  
	lua_pushcfunction(L, luainit_traceback);
	
	luainit_tget(L, MIRRORS_REFTABLE, lua_ctx->function_index);
	if(lua_isfunction(L, -1)) {
		int err;
		
		/* mirror */
		tolua_pushusertype(L, mirror, "mirror_ctx");
		
		/* success? */
		lua_pushboolean(L, success);
		
		/* param */
		luainit_tget(L, MIRRORS_REFTABLE, lua_ctx->param_index);
		
		/* call the function with two params and one return */
		err = lua_pcall(L, 3, 1, -5);
		if(err) {
			/* do something with the error ... ? */
			luainit_error(L, "(calling mirror callback)", err);
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
	
	luainit_tremove(L, MIRRORS_REFTABLE, lua_ctx->function_index);
	luainit_tremove(L, MIRRORS_REFTABLE, lua_ctx->param_index);
	free(lua_ctx);
	
	return 1;
}

int luamirror_new(lua_State *L) {
#ifndef TOLUA_RELEASE
	tolua_Error tolua_err;
	if (
		!tolua_isusertype(L,1,"slave_connection",0,&tolua_err) ||
		!tolua_isusertype(L,2,"vfs_element",0,&tolua_err) ||
		!tolua_isusertype(L,3,"slave_connection",0,&tolua_err) ||
		!tolua_isusertype(L,4,"vfs_element",0,&tolua_err) ||
		!tolua_isfunction(L,5,0,&tolua_err) ||
		/* 6th param is not mandatory */
		!tolua_isnoobj(L,7,&tolua_err)
	) {
		goto tolua_lerror;
	} else
#endif
	{
		struct slave_connection *src_cnx = ((struct slave_connection*)tolua_tousertype(L,1,0));
		struct vfs_element *src_file = ((struct vfs_element*)tolua_tousertype(L,2,0));
		struct slave_connection *dest_cnx = ((struct slave_connection*)tolua_tousertype(L,3,0));
		struct vfs_element *dest_file = ((struct vfs_element*)tolua_tousertype(L,4,0));
		
		struct mirror_ctx *mirror;
		struct mirror_lua_ctx *lua_ctx;
		
		lua_ctx = malloc(sizeof(struct mirror_lua_ctx));
		if(!lua_ctx) {
			MIRROR_DBG("Memory error");
			return 0;
		}
		
		lua_ctx->script = script_resolve(L);
		
		/* set all infos in the lua ctx */
		lua_ctx->function_index = luainit_tinsert(L, MIRRORS_REFTABLE, 5);
		
		if(!lua_isnil(L, 6) && !lua_isnone(L, 6)) {
			lua_ctx->param_index = luainit_tinsert(L, MIRRORS_REFTABLE, 6);
		} else {
			lua_ctx->param_index = 0;
		}
		
		/* now create the mirror operation */
		mirror = mirror_new(src_cnx, src_file, dest_cnx, dest_file, luamirror_callback, lua_ctx);
		if(!mirror) {
			MIRROR_DBG("Memory error");
			luainit_tremove(L, MIRRORS_REFTABLE, lua_ctx->function_index);
			luainit_tremove(L, MIRRORS_REFTABLE, lua_ctx->param_index);
			free(lua_ctx);
			return 0;
		}
		
		if(!collection_add(lua_ctx->script->mirrors, mirror)) {
			MIRROR_DBG("Collection error");
			mirror_cancel(mirror);
			luainit_tremove(L, MIRRORS_REFTABLE, lua_ctx->function_index);
			luainit_tremove(L, MIRRORS_REFTABLE, lua_ctx->param_index);
		  free(lua_ctx);
		  return 0;
		}
		
		tolua_pushusertype(L, mirror, "mirror_ctx");
	}
	return 1;
#ifndef TOLUA_RELEASE
tolua_lerror:
	tolua_error(L,"#ferror in function luamirror_new.",&tolua_err);
	return 0;
#endif
}

TOLUA_API int luaopen_xftpd_mirror(lua_State* L)
{
	luainit_tcreate(L, MIRRORS_REFTABLE);

	tolua_module(L,NULL,0);
	tolua_beginmodule(L,NULL);
		tolua_module(L,"mirrors",1);
		tolua_beginmodule(L,"mirrors");
			tolua_function(L,"new", luamirror_new);
		tolua_endmodule(L);
	tolua_endmodule(L);
	
	return 1;
}
