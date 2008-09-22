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


#include "collection.h"
#include "time.h"
#include "constants.h"
#include "luainit.h"
#include "timer.h"
#include "logging.h"

struct collection *timers = NULL; /* collection of struct timer_ctx */

int timer_init() {

	TIMER_DBG("Loading ...");

	timers = collection_new(C_CASCADE);

	return 1;
}

void timer_free() {

	TIMER_DBG("Unloading ...");

	collection_destroy(timers);
	timers = NULL;

	return;
}

/* clean all timer */
void timer_clear() {

	collection_empty(timers);

	return;
}

static unsigned int timer_call_callback(struct collection *c, struct timer_ctx *to, void *param) {
	lua_State *L = to->script->L;
	
	if((time_now() >= to->timestamp) && (timer(to->timestamp) >= to->timeout)) {
		
		to->timestamp = time_now();
		
		lua_pushcfunction(L, luainit_traceback);
		
		tolua_pushusertype(L,(void*)to,"timer_ctx");
		
		luainit_tget(L, TIMER_REFTABLE, to->function_index);
		if(lua_isfunction(L, -1)) {
			int err;
			
			/* call the function with two params and one return */
			err = lua_pcall(L, 1, 1, -2);
			if(err) {
				/* do something with the error ... ? */
				luainit_error(L, "(calling timer callback)", err);
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
	}

	return 1;
}

/* call any timeout that need so */
unsigned int timer_poll() {
	
	collection_iterate(timers, (collection_f)timer_call_callback, NULL);
	
	return 1;
}

