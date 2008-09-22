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
#include "skins.h"

static int luaskins_getline(lua_State* tolua_S)
{
#ifndef TOLUA_RELEASE
	tolua_Error tolua_err;
	if (
		!tolua_isstring(tolua_S,1,0,&tolua_err) ||
		!tolua_isnoobj(tolua_S,2,&tolua_err)
	)
	goto tolua_lerror;
	else
#endif
	{
		char* line = ((char*)  tolua_tostring(tolua_S,1,0));
		char* tolua_ret = (char*)  skins_getline(line,NULL);
		if(tolua_ret) {
			tolua_pushstring(tolua_S,(const char*)tolua_ret);
			free(tolua_ret);
			return 1;
		}
	}
	return 0;
#ifndef TOLUA_RELEASE
	tolua_lerror:
	tolua_error(tolua_S,"#ferror in function 'getline'.",&tolua_err);
	return 0;
#endif
}

TOLUA_API int luaopen_xftpd_skins(lua_State* L)
{
	tolua_module(L,NULL,1);
	tolua_beginmodule(L,NULL);
		tolua_module(L,"skins",1);
		tolua_beginmodule(L,"skins");
			tolua_function(L,"getline", luaskins_getline);
		tolua_endmodule(L);
	tolua_endmodule(L);
	
	return 1;
}
