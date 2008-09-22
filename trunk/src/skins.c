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
#else
#include <stdlib.h>
#include <string.h>
#endif


#include "constants.h"
#include "collection.h"
#include "logging.h"
#include "config.h"
#include "skins.h"
#include "asprintf.h"
#include "dir.h"

struct collection *skins = NULL; /* collection of struct config_file */

int skins_loadall() {
	char *skins;
	
	skins = config_raw_read(MASTER_CONFIG_FILE, "xftpd.skins.path", NULL);
	
	if(!skins) {
		SKINS_DBG("xftpd.skins.path is not set in "MASTER_CONFIG_FILE"... Using default " SKINS_PATH);
		skins_load_directory(SKINS_PATH);
	} else {
		skins_load_directory(skins);
		free(skins);
		skins = NULL;
	}
	
	return 1;
}

int skins_init() {

	SKINS_DBG("Loading ...");
	
	skins = collection_new(C_CASCADE);
	
	skins_loadall();
	
	return 1;
}

int skins_reload() {

	SKINS_DBG("Reloading ...");
	
	skins_loadall();
	
	return 1;
}

int skins_load_file(const char *filename) {
	struct config_file *config;
	
	if(skins_isloaded(filename)) {
		SKINS_DBG("Trying to load a skin that is already loaded: %s", filename);
		return 1;
	}
	
	SKINS_DBG("Loading %s", filename);
	
	config = config_open(filename);
	if(!config) {
		SKINS_DBG("Couldn't open skin file %s", filename);
		return 0;
	}
	
	if(!collection_add(skins, config)) {
		SKINS_DBG("Collection error");
		config_close(config);
		return 0;
	}
	
	return 1;
}

int skins_unload_matcher(struct collection *c, struct config_file *config, const char *filename) {
	
	return (!strcasecmp(config->filename, filename));
}

int skins_unload(const char *filename) {
	struct config_file *config;
	
	config = collection_match(skins, (collection_f)skins_unload_matcher, (void *)filename);
	if(config) {
		collection_delete(skins, config);
		config_close(config);
		return 1;
	}
	
	return 0;
}

int skins_isloaded_matcher(struct collection *c, struct config_file *config, const char *filename) {
	
	return (!strcasecmp(config->filename, filename));
}

int skins_isloaded(const char *filename) {
	
	return (collection_match(skins, (collection_f)skins_isloaded_matcher, (void *)filename) != NULL);
}

int skins_getline_iterator(struct collection *c, struct config_file *config, void *param) {
	struct {
		const char *line;
		char *value;
	} *ctx = param;
	
	ctx->value = config_read(config, ctx->line, NULL);
	
	return (ctx->value == NULL);
}

char *skins_getline(const char *line, const char *default_value)
{
	struct {
		const char *line;
		char *value;
	} ctx = { line, NULL };
	
	collection_iterate(skins, (collection_f)skins_getline_iterator, (void *)&ctx);
	
	if(ctx.value)
		return strdup(ctx.value);
	
	if(default_value)
		return strdup(default_value);
	
	return NULL;
}

#define ENDSWITH(a, b) \
  ((strlen(a) > strlen(b)) && !strcasecmp(&a[strlen(a)-strlen(b)], b))
  
int skins_load_directory(const char *directory)
{
	struct dir_ctx *dir;
	char *path;

	path = dir_fullpath(directory);
	if(!path) {
		SKINS_DBG("Memory error");
		return 0;
	}

	dir = dir_open(path, "*");
	if(!dir) {
		SKINS_DBG("Couldn't load \"%s\"", path);
	} else {
		do {
			char *searchpath;
			if(!strcmp(dir_name(dir), ".") || !strcmp(dir_name(dir), "..")) continue;
			
			if(dir_attrib(dir) & DIR_SUBDIR) {
				searchpath = bprintf("%s/%s", path, dir_name(dir));
				if(!searchpath)
					continue;
				
				skins_load_directory(searchpath);
				
				free(searchpath);
				searchpath = NULL;
			} else {
				if(!ENDSWITH(dir_name(dir), ".skin")) continue;
				
				searchpath = bprintf("%s/%s", path, dir_name(dir));
				if(!searchpath)
					continue;
				
				skins_load_file(searchpath);
				
				free(searchpath);
				searchpath = NULL;
			}
		} while (dir_next(dir));
		dir_close(dir);
	}
	free(path);

	return 1;
}

#undef ENDSWITH

void skins_free() {

	SKINS_DBG("Unloading ...");

	return;
}
