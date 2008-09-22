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
#include "vfs.h"


static int luavfs_relative_path(lua_State* L)
{
#ifndef TOLUA_RELEASE
	tolua_Error tolua_err;
	if (
		!tolua_isusertype(L,1,"vfs_element",0,&tolua_err) ||
		!tolua_isusertype(L,2,"vfs_element",0,&tolua_err) ||
		!tolua_isnoobj(L,3,&tolua_err)
	)
		goto tolua_lerror;
	else
#endif
	{
		struct vfs_element *container = ((struct vfs_element*)tolua_tousertype(L,1,0));
		struct vfs_element *element = ((struct vfs_element*)tolua_tousertype(L,2,0));
		{
			char* tolua_ret = (char*)  vfs_get_relative_path(container, element);
			tolua_pushstring(L,(const char*)tolua_ret);
			free(tolua_ret);
		}
	}
	return 1;
#ifndef TOLUA_RELEASE
	tolua_lerror:
	tolua_error(L,"#ferror in function 'relative_path'.",&tolua_err);
	return 0;
#endif
}

TOLUA_API int luaopen_xftpd_vfs(lua_State* L)
{
	tolua_module(L,NULL,0);
	tolua_beginmodule(L,NULL);
		tolua_module(L,"vfs",1);
		tolua_beginmodule(L,"vfs");
			tolua_function(L,"relative_path", luavfs_relative_path);
		tolua_endmodule(L);
	tolua_endmodule(L);
	
	return 1;
}
