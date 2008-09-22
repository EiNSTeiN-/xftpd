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
#include "time.h"
#include "timer.h"
#include "config.h"

static void timer_obj_destroy(struct timer_ctx *to) {
  lua_State *L = to->script->L;
	
	collectible_destroy(to);
	
	luainit_tremove(L, TIMER_REFTABLE, to->function_index);
	free(to);
	
	return;
}

/* add a timer:
timer_ctx *add((*callback)(timer_ctx *), interval, start = time.now()) */
static int luatimer_add(lua_State *L) {
#ifndef TOLUA_RELEASE
	tolua_Error tolua_err;
	if (
		!tolua_isfunction(L,1,0,&tolua_err) ||
		!tolua_isnumber(L,2,0,&tolua_err) ||
		!tolua_isnumber(L,3,1,&tolua_err) ||
		!tolua_isnoobj(L,4,&tolua_err)
	) {
		goto tolua_lerror;
	} else
#endif
	{
		struct timer_ctx *to;
		unsigned long long int timeout = (unsigned long long int)lua_tonumber(L, 2);
		unsigned long long int timestamp = (lua_isnumber(L, 3) ? (unsigned long long int)lua_tonumber(L, 3) : time_now());
		
		to = malloc(sizeof(struct timer_ctx));
		if(!to) {
			TIMER_DBG("Memory error");
			return 0;
		}
		
		obj_init(&to->o, to, (obj_f)timer_obj_destroy);
		collectible_init(to);
		
		to->script = script_resolve(L);
		
		to->volatile_config = config_volatile();
		to->function_index = luainit_tinsert(L, TIMER_REFTABLE, 1);
		to->timeout = timeout;
		to->timestamp = timestamp;
		
		collection_add(timers, to);
		collection_add(to->script->timers, to);
		
		tolua_pushusertype(L,(void*)to,"timer_ctx");
	}
	return 1;
#ifndef TOLUA_RELEASE
tolua_lerror:
	tolua_error(L,"#ferror in function luatimer_add.",&tolua_err);
	return 0;
#endif
}

TOLUA_API int luaopen_xftpd_timer(lua_State* L)
{
	luainit_tcreate(L, TIMER_REFTABLE);
	
	tolua_module(L,NULL,0);
	tolua_beginmodule(L,NULL);
		tolua_module(L,"timer",1);
		tolua_beginmodule(L,"timer");
			tolua_function(L,"add", luatimer_add);
			//tolua_function(L,"delete", luatimer_delete);
		tolua_endmodule(L);
	tolua_endmodule(L);
	
	return 1;
}
