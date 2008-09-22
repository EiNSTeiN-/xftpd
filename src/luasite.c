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
#include "site.h"
#include "tree.h"

static int site_tree_cmp(struct site_handler *a, struct site_handler *b) {
	
	return 1; //stricmp(a->line, b->line);
}

static void site_handler_obj_destroy(struct site_handler *handler)
{
  lua_State *L = handler->script->L;
	
	collectible_destroy(handler);
	
	luainit_tremove(L, SITE_REFTABLE, handler->handler_index);
	handler->handler_index = -1;
	
	free(handler);
	
	return;
}

static int luasite_hook(lua_State *L) {
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
		struct collection *branches = (struct collection *)tolua_tousertype(L, 1, 0);
		const char *trigger = lua_tostring(L, 2);
		struct site_handler *handler;
		
		handler = malloc(sizeof(struct site_handler));
		if(!handler) {
			SITE_DBG("Memory error");
			return 0;
		}
		
		obj_init(&handler->o, handler, (obj_f)site_handler_obj_destroy);
		collectible_init(handler);
		
		handler->script = script_resolve(L);
		handler->handler_index = luainit_tinsert(L, SITE_REFTABLE, 3);
		
		if(!collection_add(handler->script->sitehandlers, handler)) {
			SITE_DBG("Collection error");
		  return 0;
		}
		
		if(!tree_add(branches, trigger, &handler->c, (tree_f)site_tree_cmp)) {
			SITE_DBG("tree_add failed.");
		  collection_delete(handler->script->sitehandlers, handler);
			return 0;
		}
		
		lua_pushboolean(L, 1);
	}
	return 1;
#ifndef TOLUA_RELEASE
tolua_lerror:
	tolua_error(L,"#ferror in function site_tree_add.",&tolua_err);
	return 0;
#endif
}

TOLUA_API int luaopen_xftpd_site(lua_State* L)
{
	luainit_tcreate(L, SITE_REFTABLE);

	tolua_module(L,NULL,0);
	tolua_beginmodule(L,NULL);
		tolua_module(L,"site",1);
		tolua_beginmodule(L,"site");
			tolua_function(L,"hook", luasite_hook);
		tolua_endmodule(L);
	tolua_endmodule(L);
	
	return 1;
}
