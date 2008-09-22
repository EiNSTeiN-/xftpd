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
#include "collection.h"
#include "logging.h"
#include "config.h"
#include "scripts.h"
#include "luainit.h"
#include "asprintf.h"
#include "dir.h"

struct collection *scripts;

int scripts_loadall() {
	char *scripts;
	
	scripts = config_raw_read(MASTER_CONFIG_FILE, "xftpd.scripts.path", NULL);
	
	if(!scripts) {
		SCRIPTS_DBG("xftpd.scripts.path is not set in "MASTER_CONFIG_FILE"... Using default " SCRIPTS_PATH);
		scripts_load_directory(SCRIPTS_PATH);
	} else {
		scripts_load_directory(scripts);
		free(scripts);
		scripts = NULL;
	}
  
	return 1;
}

int scripts_init() {

	SCRIPTS_DBG("Loading ...");
	
	scripts = collection_new(C_CASCADE);
	if(!scripts) {
	  SCRIPTS_DBG("Memory error.");
	  return 0;
	}
	
	scripts_loadall();
	
	return 1;
}

int scripts_reload() {

	SCRIPTS_DBG("Reloading ...");
	
	scripts_loadall();
	
	return 1;
}

int script_resolve_matcher(struct collection *c, struct script_ctx *script, lua_State *L)
{
	return (script->L == L);
}

struct script_ctx *script_resolve(lua_State *L)
{
  
  return (struct script_ctx *)collection_match(scripts, (collection_f)script_resolve_matcher, L);
}

static void script_obj_destroy(struct script_ctx *script)
{

  collection_destroy(script->events);
  script->events = NULL;

  free(script->filename);

  luainit_freestate(script->L);
  script->L = NULL;
  
  free(script);

  return;
}

int scripts_load_file(const char *filename) {
	struct script_ctx *script;
	
	SCRIPTS_DBG("Loading %s", filename);
	
	script = malloc(sizeof(struct script_ctx));
	if(!script) {
	  SCRIPTS_DBG("Memory error");
	  return 0;
	}
	
	obj_init(&script->o, script, (obj_f)script_obj_destroy);
	collectible_init(script);
	
	script->filename = strdup(filename);
	if(!script->filename) {
		SCRIPTS_DBG("Memory error");
		free(script);
		return 0;
	}
	
	script->events = collection_new(C_CASCADE);
	if(!script->events) {
		SCRIPTS_DBG("Memory error");
		free(script->filename);
		free(script);
		return 0;
	}
	
	script->irchandlers = collection_new(C_CASCADE);
	if(!script->irchandlers) {
		SCRIPTS_DBG("Memory error");
		collection_destroy(script->events);
		free(script->filename);
		free(script);
		return 0;
	}
	
	script->mirrors = collection_new(C_CASCADE);
	if(!script->mirrors) {
		SCRIPTS_DBG("Memory error");
		collection_destroy(script->events);
		collection_destroy(script->irchandlers);
		free(script->filename);
		free(script);
		return 0;
	}
	
	script->sitehandlers = collection_new(C_CASCADE);
	if(!script->sitehandlers) {
		SCRIPTS_DBG("Memory error");
		collection_destroy(script->events);
		collection_destroy(script->irchandlers);
		collection_destroy(script->mirrors);
		free(script->filename);
		free(script);
		return 0;
	}
	
	script->timers = collection_new(C_CASCADE);
	if(!script->timers) {
		SCRIPTS_DBG("Memory error");
		collection_destroy(script->events);
		collection_destroy(script->irchandlers);
		collection_destroy(script->mirrors);
		collection_destroy(script->sitehandlers);
		free(script->filename);
		free(script);
		return 0;
	}
	
	script->L = luainit_newstate();
	if(!script->L) {
		SCRIPTS_DBG("Could not create lua state for %s", script->filename);
		collection_destroy(script->events);
		collection_destroy(script->irchandlers);
		collection_destroy(script->mirrors);
		collection_destroy(script->sitehandlers);
		collection_destroy(script->timers);
		free(script->filename);
		free(script);
		return 0;
	}
	
	if(!luainit_loadfile(script->L, script->filename)) {
		SCRIPTS_DBG("Could not load file %s", script->filename);
		luainit_freestate(script->L);
		collection_destroy(script->events);
		collection_destroy(script->irchandlers);
		collection_destroy(script->mirrors);
		collection_destroy(script->sitehandlers);
		collection_destroy(script->timers);
		free(script->filename);
		free(script);
		return 0;
	}
	
	if(!collection_add(scripts, script)) {
		luainit_freestate(script->L);
		collection_destroy(script->events);
		collection_destroy(script->irchandlers);
		collection_destroy(script->mirrors);
		collection_destroy(script->sitehandlers);
		collection_destroy(script->timers);
		free(script->filename);
		free(script);
		return 0;
	}
	
	if(!luainit_call(script->L, "init"))
		SCRIPTS_DBG("Couldn't call init() on %s", script->filename);
  
	return 1;
}

#define ENDSWITH(a, b) \
  ((strlen(a) > strlen(b)) && !strcasecmp(&a[strlen(a)-strlen(b)], b))
  
int scripts_load_directory(const char *directory)
{
	struct dir_ctx *dir;
	char *path;

	path = dir_fullpath(directory);
	if(!path) {
		SCRIPTS_DBG("Can't resolve %s", path);
	  return 0;
	}
	
	dir = dir_open(path, "*");
	if(!dir) {
		SCRIPTS_DBG("Couldn't open \"%s\"", path);
	} else {
		do {
			char *searchpath;
			
			if(!strcmp(dir_name(dir), ".") || !strcmp(dir_name(dir), "..")) continue;
			
			if(dir_attrib(dir) & DIR_SUBDIR) {
				searchpath = bprintf("%s/%s", path,  dir_name(dir));
				if(!searchpath)
					continue;
				
				scripts_load_directory(searchpath);
				
				free(searchpath);
				searchpath = NULL;
			} else {
				if(!ENDSWITH(dir_name(dir), ".lua")) continue;
				
				searchpath = bprintf("%s/%s", path,  dir_name(dir));
				if(!searchpath)
					continue;
				
				scripts_load_file(searchpath);
				
				free(searchpath);
				searchpath = NULL;
			}
		} while(dir_next(dir));
		dir_close(dir);
	}
	free(path);

	return 1;
}

#undef ENDSWITH

void scripts_free() {

	SCRIPTS_DBG("Unloading ...");

	return;
}
