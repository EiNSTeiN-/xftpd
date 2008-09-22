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


#include "constants.h"
#include "asprintf.h"

#include "socket.h"
#include "luainit.h"
#include "collection.h"
#include "events.h"
#include "slaveselection.h"
#include "timer.h"
#include "site.h"
#include "irccore.h"
#include "dir.h"

#include "luacollection.h"
#include "luaconfig.h"
#include "luaevents.h"
//#include "luaftpd.h" // nothing yet
#include "luairc.h"
#include "luamirror.h"
//#include "luanuke.h" // nothing yet
//#include "luasfv.h" // nothing yet
#include "luasite.h"
//#include "luaslaves.h" // nothing yet
//#include "luatime.h" // nothing yet
#include "luatimer.h"
//#include "luaupdate.h" // nothing yet
//#include "luausers.h" // nothing yet
#include "luavfs.h"
#include "luaskins.h"

//lua_State* L = NULL;

static int luainit_eq(lua_State* L)
{
	void *obj1 = tolua_tousertype(L,1,0);
	void *obj2 = tolua_tousertype(L,2,0);
	
	lua_pushboolean(L, (obj1 == obj2));
	
	return 1;
}

static int luainit_ipaddress_add(lua_State* L) {
	tolua_Error tolua_err;
	unsigned int left;
	unsigned int right;
	
	if(tolua_isusertype(L,1,"ipaddress",0,&tolua_err)) {
		left = *((unsigned int*)  tolua_tousertype(L,1,0));
	}
	else if(tolua_isnumber(L,1,0,&tolua_err)) {
		left = ((unsigned int)  tolua_tonumber(L,1,0));
	}
	else {
		lua_pushfstring(L, "unknown left operand in operation: type %s", lua_typename(L, lua_type(L, 1)));
		lua_error(L);
		return 0;
	}
	
	if(tolua_isusertype(L,2,"ipaddress",0,&tolua_err)) {
		right = *((unsigned int*)  tolua_tousertype(L,2,0));
	}
	else if(tolua_isnumber(L,2,0,&tolua_err)) {
		right = ((unsigned int)  tolua_tonumber(L,2,0));
	}
	else {
		lua_pushfstring(L, "unknown right operand in operation: type %s", lua_typename(L, lua_type(L, 2)));
		lua_error(L);
		return 0;
	}
	
	{
		unsigned int total = left + right;
		void* tolua_obj = tolua_copy(L,(void*)&total,sizeof(ipaddress));
		tolua_pushusertype_and_takeownership(L,tolua_obj,"ipaddress");
	}
	
	return 1;
}

static int luainit_ipaddress_sub(lua_State* L) {
	tolua_Error tolua_err;
	unsigned int left;
	unsigned int right;
	
	if(tolua_isusertype(L,1,"ipaddress",0,&tolua_err)) {
		left = *((unsigned int*)  tolua_tousertype(L,1,0));
	}
	else if(tolua_isnumber(L,1,0,&tolua_err)) {
		left = ((unsigned int)  tolua_tonumber(L,1,0));
	}
	else {
		lua_pushfstring(L, "unknown left operand in operation: type %s", lua_typename(L, lua_type(L, 1)));
		lua_error(L);
		return 0;
	}
	
	if(tolua_isusertype(L,2,"ipaddress",0,&tolua_err)) {
		right = *((unsigned int*)  tolua_tousertype(L,2,0));
	}
	else if(tolua_isnumber(L,2,0,&tolua_err)) {
		right = ((unsigned int)  tolua_tonumber(L,2,0));
	}
	else {
		lua_pushfstring(L, "unknown right operand in operation: type %s", lua_typename(L, lua_type(L, 2)));
		lua_error(L);
		return 0;
	}
	
	{
		unsigned int total = left - right;
		void* tolua_obj = tolua_copy(L,(void*)&total,sizeof(ipaddress));
		tolua_pushusertype_and_takeownership(L,tolua_obj,"ipaddress");
	}
	
	return 1;
}

static int luainit_ipaddress_eq(lua_State* L) {
	tolua_Error tolua_err;
	unsigned int left;
	unsigned int right;
	
	if(tolua_isusertype(L,1,"ipaddress",0,&tolua_err)) {
		left = *((unsigned int*)  tolua_tousertype(L,1,0));
	}
	else if(tolua_isnumber(L,1,0,&tolua_err)) {
		left = ((unsigned int)  tolua_tonumber(L,1,0));
	}
	else {
		lua_pushfstring(L, "unknown left operand in operation: type %s", lua_typename(L, lua_type(L, 1)));
		lua_error(L);
		return 0;
	}
	
	if(tolua_isusertype(L,2,"ipaddress",0,&tolua_err)) {
		right = *((unsigned int*)  tolua_tousertype(L,2,0));
	}
	else if(tolua_isnumber(L,2,0,&tolua_err)) {
		right = ((unsigned int)  tolua_tonumber(L,2,0));
	}
	else {
		lua_pushfstring(L, "unknown right operand in operation: type %s", lua_typename(L, lua_type(L, 2)));
		lua_error(L);
		return 0;
	}
	
	lua_pushboolean(L, (left == right));
	
	return 1;
}

static int luainit_ipaddress_lt(lua_State* L) {
	tolua_Error tolua_err;
	unsigned int left;
	unsigned int right;
	
	if(tolua_isusertype(L,1,"ipaddress",0,&tolua_err)) {
		left = *((unsigned int*)  tolua_tousertype(L,1,0));
	}
	else if(tolua_isnumber(L,1,0,&tolua_err)) {
		left = ((unsigned int)  tolua_tonumber(L,1,0));
	}
	else {
		lua_pushfstring(L, "unknown left operand in operation: type %s", lua_typename(L, lua_type(L, 1)));
		lua_error(L);
		return 0;
	}
	
	if(tolua_isusertype(L,2,"ipaddress",0,&tolua_err)) {
		right = *((unsigned int*)  tolua_tousertype(L,2,0));
	}
	else if(tolua_isnumber(L,2,0,&tolua_err)) {
		right = ((unsigned int)  tolua_tonumber(L,2,0));
	}
	else {
		lua_pushfstring(L, "unknown right operand in operation: type %s", lua_typename(L, lua_type(L, 2)));
		lua_error(L);
		return 0;
	}
	
	lua_pushboolean(L, (left < right));
	
	return 1;
}

static int luainit_ipaddress_tostring(lua_State* L) {
	tolua_Error tolua_err;
	ipaddress ip;
	char *str;
	
	if(!tolua_isusertype(L,1,"ipaddress",0,&tolua_err)) {
		lua_pushfstring(L, "unknown operand in operation: type %s", lua_typename(L, lua_type(L, 1)));
		lua_error(L);
		return 0;
	}
	
	ip = *(ipaddress*)tolua_tousertype(L,1,0);
	str = bprintf("%u.%u.%u.%u", ip.c1, ip.c2, ip.c3, ip.c4);
	if(!str) {
		lua_pushnil(L);
	}
	else {
		lua_pushstring(L, str);
		free(str);
	}
	
	return 1;
}

static int luainit_ipaddress_tonumber(lua_State* L) {
	tolua_Error tolua_err;
	
	if(!tolua_isusertype(L,1,"ipaddress",0,&tolua_err)) {
		lua_pushfstring(L, "unknown operand in operation: type %s", lua_typename(L, lua_type(L, 1)));
		lua_error(L);
		return 0;
	}
	
	lua_pushnumber(L, *(unsigned int *)tolua_tousertype(L,1,0));
	
	return 1;
}

int luainit_pushipaddress(lua_State* L, ipaddress ip)
{

	void* tolua_obj = tolua_copy(L,(void*)&ip,sizeof(ipaddress));
	tolua_pushusertype_and_takeownership(L,tolua_obj,"ipaddress");
	
	return 1;
}

static int luainit_toipaddress(lua_State* L) {
	tolua_Error tolua_err;
	unsigned int ip;
	
	if(!tolua_isnumber(L,1,0,&tolua_err)) {
		lua_pushfstring(L, "unknown operand in operation: type %s", lua_typename(L, lua_type(L, 1)));
		lua_error(L);
		return 0;
	}
	ip = (unsigned int)lua_tonumber(L, 1);
	luainit_pushipaddress(L, *(ipaddress *)&ip);
	
	return 1;
}

static int luainit_mkipaddress(lua_State* L)
{
#ifndef TOLUA_RELEASE
	tolua_Error tolua_err;
	if (
		!tolua_isnumber(L,1,0,&tolua_err) ||
		!tolua_isnumber(L,2,0,&tolua_err) ||
		!tolua_isnumber(L,3,0,&tolua_err) ||
		!tolua_isnumber(L,4,0,&tolua_err) ||
		!tolua_isnoobj(L,5,&tolua_err)
	)
		goto tolua_lerror;
	else
#endif
	{
		char c1 = ((char)  tolua_tonumber(L,1,0));
		char c2 = ((char)  tolua_tonumber(L,2,0));
		char c3 = ((char)  tolua_tonumber(L,3,0));
		char c4 = ((char)  tolua_tonumber(L,4,0));
		{
			ipaddress tolua_ret = (ipaddress)  mkipaddress(c1,c2,c3,c4);
			luainit_pushipaddress(L, tolua_ret);
		}
	}
	return 1;
#ifndef TOLUA_RELEASE
tolua_lerror:
	tolua_error(L,"#ferror in function 'mkipaddress'.",&tolua_err);
	return 0;
#endif
}

static int luainit_loader(lua_State *L)
{
	char *filename;
	char *path, *funcname;
	unsigned int i;
	
	path = dir_fullpath(SCRIPTS_PATH);
	if(!path) {
		LUAINIT_DBG("Memory error");
		return 0;
	}
	
	filename = bprintf("%s/%s.module", path, lua_tostring(L, 1));
	free(path);
	if(!filename) {
		LUAINIT_DBG("Memory error");
		return 0;
	}
	
	//LUAINIT_DBG("Looking up module %s", filename);
	
	if(!luainit_loadfile(L, filename)) {
		lua_pushfstring(L, "\tno file: '%s'", filename);
		free(filename);
		return 1;
	}
	free(filename);
	
	funcname = bprintf("luaopen_%s", lua_tostring(L, 1));
	for(i=0;i<strlen(funcname);i++)
		if((funcname[i] == '/') || (funcname[i] == '\\')) funcname[i] = '_';
	
	lua_pushstring(L, funcname);
	lua_gettable(L, LUA_GLOBALSINDEX); // push func
	if(!lua_isfunction(L, -1)) {
		SCRIPTS_DBG("Could not find %s() (it doesn't exist or it's not a function)", funcname);
		lua_pop(L, 1); // pop func
		lua_pushfstring(L, "\tno function: '%s'", funcname);
		free(funcname);
		return 1;
	}
	
	return 1; // return func
}

#define EQ(type) \
	tolua_usertype(L,type); \
	tolua_cclass(L,type,type,"",NULL); \
	tolua_beginmodule(L,type); \
		luainit_metamethod(L, "__eq", luainit_eq); \
	tolua_endmodule(L);

lua_State *luainit_newstate() {
	TOLUA_API int luaopen_xftpd (lua_State* tolua_S);
	lua_State *L;
	
	/* create lua instance */
	L = lua_open();
	if(!L) {
		LUAINIT_DBG("Could not create lua state");
		return NULL;
	}
	
	/* load lua libraries */
	luaL_openlibs(L);
	
	/* bind xFTPd's lua functions */
	luaopen_xftpd(L);
	
	luaopen_xftpd_collection(L);
	luaopen_xftpd_config(L);
	luaopen_xftpd_events(L);
	//luaopen_xftpd_ftpd(L); // nothing yet
	luaopen_xftpd_irc(L);
	luaopen_xftpd_mirror(L);
	//luaopen_xftpd_nuke(L); // nothing yet
	//luaopen_xftpd_sfv(L); // nothing yet
	luaopen_xftpd_site(L);
	//luaopen_xftpd_slaves(L); // nothing yet
	//luaopen_xftpd_time(L); // nothing yet
	luaopen_xftpd_timer(L);
	//luaopen_xftpd_update(L); // nothing yet
	//luaopen_xftpd_users(L); // nothing yet
	luaopen_xftpd_vfs(L);
	luaopen_xftpd_skins(L);
	
	/* bind all extra functions that tolua cannot bind correctly on its own. */
	
	tolua_module(L, NULL,1);
	tolua_beginmodule(L, NULL);
		// the code below is to make the userdata objects 
		// comparable using the equal sign in lua
		EQ("collection");
		EQ("collectible");
		EQ("config_field");
		EQ("config_file");
		EQ("client_xfer");
		EQ("ftpd_client");
		EQ("irc_message");
		EQ("irc_channel");
		EQ("irc_server");
		EQ("irc_nick_change");
		EQ("irc_part_channel");
		EQ("irc_kick_user");
		EQ("irc_quit");
		EQ("mirror_side");
		EQ("mirror_ctx");
		EQ("nuke_nukee");
		EQ("nuke_ctx");
		EQ("sfv_entry");
		EQ("sfv_ctx");
		EQ("slave_connection");
		EQ("hello_data");
		EQ("slave_ctx");
		EQ("timer_ctx");
		EQ("user_ctx");
		EQ("vfs_section");
		EQ("vfs_element");
		
		// setup out custom package loader
		tolua_beginmodule(L, "package");
			tolua_beginmodule(L, "loaders");
				lua_pushcfunction(L, luainit_loader);
				lua_rawseti(L, -2, lua_objlen(L, -2)+1);
			tolua_endmodule(L);
		tolua_endmodule(L);
		
		tolua_function(L, "toipaddress", luainit_toipaddress); // take a number, returns an ipaddress object
		tolua_function(L, "mkipaddress", luainit_mkipaddress); // take four numbers, returns an ipaddress objects
		
		tolua_usertype(L, "ipaddress");
		tolua_cclass(L, "ipaddress","ipaddress","",NULL);
		tolua_beginmodule(L, "ipaddress");
			luainit_metamethod(L, "__add", luainit_ipaddress_add);
			luainit_metamethod(L, "__sub", luainit_ipaddress_sub);
			luainit_metamethod(L, "__eq", luainit_ipaddress_eq);
			luainit_metamethod(L, "__lt", luainit_ipaddress_lt);
			
			tolua_function(L, "tostring", luainit_ipaddress_tostring);
			tolua_function(L, "tonumber", luainit_ipaddress_tonumber);
		tolua_endmodule(L);
	tolua_endmodule(L);
	
	return L;
}
#undef EQ
/*
int luainit_script_load() {

	luainit_newstate();
	
	return 1;
}
*/
void luainit_freestate(lua_State *L) {

	/* destroy lua instance */
	lua_close(L);

	return;
}
/*
int luainit_reload() {
	LUAINIT_DBG("Reloading ...");
	
	// destroy lua instance
	lua_close(L);
	
	L = NULL;
	
	luainit_newstate();
	
	return 1;
}
*/

int luainit_loadfile(lua_State *L, const char *filename)
{
  int err;
  
	lua_pushcfunction(L, luainit_traceback);
	
	err = luaL_loadfile(L, filename);
	if(err) {
		SCRIPTS_DBG("Could not load file %s", filename);
		luainit_error(L, filename, err);
		lua_pop(L, 2); /* pop the error message + the errfunc */
		return 0;
	}
	
	err = lua_pcall(L, 0 /* no args */, 0 /* no ret values */, -2);
	if(err) {
		luainit_error(L, filename, err);
		lua_pop(L, 2); /* pop the error message + the errfunc */
		return 0;
	}
	lua_pop(L, 1); /* pop the errfunc */

  return 1;
}

int luainit_call(lua_State *L, const char *funcname)
{
  int err;
  
	lua_pushcfunction(L, luainit_traceback); // push err
	
	/* call the init() function of this script */
	lua_pushstring(L, funcname);
	lua_gettable(L, LUA_GLOBALSINDEX); // push func
	if(!lua_isfunction(L, -1)) {
		/* this script have no function of that name */
		SCRIPTS_DBG("Could not call %s() (it doesn't exist or it's not a function)", funcname);
		lua_pop(L, 2); // pop err func
		return 0;
	}
	
	/* when pcall returns the function will already have
		been poped from the stack */
	err = lua_pcall(L, 0 /* no args */, 1 /* 1 ret value */, -2); // stack: err func
	if(err) {
	  char *msg;
	  
	  msg = bprintf("(calling function \"%s\")", funcname);
		luainit_error(L, msg, err);
		free(msg);
	} else {
		/*  */
		SCRIPTS_DBG("Success calling function %s().", funcname);
	}
	
	/* pops the error message or the return value + the err func */
	lua_pop(L, 2);
  
  return 1;
}

int luainit_metamethod(lua_State* L, const char *name, int (* func)(lua_State* L)) {
	lua_pushstring(L,name);
	lua_pushcfunction(L, func);
	lua_rawset(L,-3);
	return 1;
}

void luainit_tcreate(lua_State* L, const char *table)
{
    lua_pushlstring(L, table, strlen(table));    // exclude trailing \0
    lua_newtable(L);
    lua_rawset(L, LUA_REGISTRYINDEX);
	
	return;
}

int luainit_tinsert(lua_State* L, const char *table, int stack_idx)
{
    int table_idx = 0;
    int nTop;

    if (lua_isnone(L, stack_idx)) {
        return LUA_REFNIL;
	}

    if (lua_isnil(L, stack_idx))
    {
        return LUA_REFNIL;
    }
	
    lua_pushvalue(L, stack_idx);                 // get value to store
    nTop = lua_gettop(L);
	
    lua_pushlstring(L, table, strlen(table));    // exclude trailing \0
    lua_rawget(L, LUA_REGISTRYINDEX);            // pop key, push result
	
    {
        lua_pushliteral(L, "n");                 // push 'n' as key
        lua_rawget(L, -2);                       // pop key, push result
        if (lua_isnil(L, -1)) {                  // table not used before?
            table_idx = 0;
        } else {
            table_idx = (int)lua_tonumber(L, -1); // get result value
		}
        lua_pop(L, 1);                           // pop result
		
        table_idx++;                             // next unallocated index
		
        lua_pushvalue(L, nTop);                  // push value to store
        lua_rawseti(L, -2, table_idx);           // store value, pop value
		
        lua_pushliteral(L, "n");                 // push key
        lua_pushnumber(L, table_idx);            // push value
        lua_rawset(L, -3);                       // pop key, value
    }

    lua_pop(L, 2);                               // restore stack

    return table_idx;
}

int luainit_tremove(lua_State* L, const char *table, int stack_idx)
{
    int  table_count;
    int ret = 0;                       // false indicates index out of range
	
    if (stack_idx == LUA_REFNIL) {
        return 1;
	}

    lua_pushlstring(L, table, strlen(table));    // exclude trailing \0
    lua_rawget(L, LUA_REGISTRYINDEX);            // pop key, push result

    lua_pushliteral(L, "n");                // push 'n' as key
    lua_rawget(L, -2);                      // pop key, push result
    table_count = (int)lua_tonumber(L, -1); // get result value
    lua_pop(L, 1);                          // pop result

    // ensure ref index in range of table
    if ((stack_idx > 0) && (stack_idx <= table_count))
    {
        lua_pushnil(L);                    // push nil as value
        lua_rawseti(L, -2, stack_idx);  // set table, pop value
        // add the now available index to use for the next tinsert
        //M_WXLSTATEDATA->m_wxlStateData->m_unusedReferenceIndexes.Add(wxlref_index);
        ret = 1;
    }
	
    lua_pop(L, 1);                         // clean up stack
    return ret;
}

int luainit_tget(lua_State *L, const char *table, int stack_idx)
{
    int  table_count;
    int ret = 0;                        // false indicates index out of range
	
    if (stack_idx == LUA_REFNIL)
    {
        lua_pushnil(L);
        return 1;
    }

    lua_pushlstring(L, table, strlen(table));    // exclude trailing \0
    lua_rawget(L, LUA_REGISTRYINDEX);            // pop key, push result

    lua_pushliteral(L, "n");                 // push 'n' as key
    lua_rawget(L, -2);                       // pop key, push result
    table_count = (int)lua_tonumber(L, -1);  // get result value
    lua_pop(L, 1);                           // pop result

    // ensure ref index in range of table
    if ((stack_idx > 0) && (stack_idx <= table_count))
    {
        lua_rawgeti(L, -1, stack_idx);    // push result
        ret = 1;
    }
    else
        lua_pushnil(L);                      // push result

    lua_remove(L, -2);                       // balance stack

    return ret;
}

/* ripped from wxLua, that ripped it from lua */
int luainit_traceback(lua_State *L) {
	lua_getfield(L, LUA_GLOBALSINDEX, "debug");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return 1;
	}
	lua_getfield(L, -1, "traceback");
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 2);
		return 1;
	}
	lua_pushvalue(L, 1);      /* pass error message */
	lua_pushinteger(L, 2);    /* skip this function and traceback */
	lua_call(L, 2, 1);        /* call debug.traceback */
	return 1;
}

void luainit_error(lua_State *L, const char *location, int err) {
	const char *errval, *errmsg;
	
	/*
		LUA_ERRRUN --- a runtime error. 
		LUA_ERRMEM --- memory allocation error. For such errors, Lua does not call the error handler function. 
		LUA_ERRERR --- error while running the error handler function. 
	*/
	if(err == LUA_ERRSYNTAX) errval = "syntax error during pre-compilation";
	else if(err == LUA_ERRMEM) errval = "memory allocation error";
	else if(err == LUA_ERRFILE) errval = "cannot open/read the file";
	else errval = "unknown error";
	
	errmsg = lua_tostring(L, -1);
	
	LUAINIT_DBG("error catched:");
	LUAINIT_DBG("  --> at location: %s", location);
	LUAINIT_DBG("  --> error value: %s", errval);
	LUAINIT_DBG("  --> error message: %s", errmsg);
	
	return;
}

/* not defined in normal build... */
int tolua_isfunction(lua_State* L, int lo, int def, tolua_Error* err)
{
	if (def || lua_isfunction(L,lo))
		return 1;
	err->index = lo;
	err->array = 0;
	err->type = "function";
	return 0;
}
