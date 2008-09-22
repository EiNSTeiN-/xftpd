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

#include <windows.h>

#include "constants.h"
#include "nuke.h"
#include "vfs.h"
#include "collection.h"
#include "config.h"
#include "time.h"
#include "logging.h"

struct collection *nukes = NULL; /* nuke_ctx */

static void nukee_obj_destroy(struct nuke_nukee *nukee) {
	
	collectible_destroy(nukee);
	
	nukee->nuke = NULL;

	if(nukee->name) {
		free(nukee->name);
		nukee->name = NULL;
	}

	free(nukee);
	
	return;
}

static void nuke_destroy_nukee(struct nuke_nukee *nukee) {

	obj_destroy(&nukee->o);

	return;
}

static void nuke_obj_destroy(struct nuke_ctx *nuke) {

	if(nuke->nukees) {
		collection_destroy(nuke->nukees);
		nuke->nukees = NULL;
	}

	if(nuke->element) {
		nuke->element->nuke = NULL;
		nuke->element = NULL;
	}

	if(nuke->path) {
		free(nuke->path);
		nuke->path = NULL;
	}

	if(nuke->nuker) {
		free(nuke->nuker);
		nuke->nuker = NULL;
	}

	if(nuke->reason) {
		free(nuke->reason);
		nuke->reason = NULL;
	}

	free(nuke);
	
	return;
}

static void nuke_destroy(struct nuke_ctx *nuke) {

	collection_void(nuke->nukees);
	obj_destroy(&nuke->o);

	return;
}
static struct nuke_ctx *nuke_new(unsigned int multiplier, char *nuker, char *reason, unsigned long long int timestamp) {
	struct nuke_ctx *nuke;

	nuke = malloc(sizeof(struct nuke_ctx));
	if(!nuke) {
		NUKE_DBG("Memory error");
		return NULL;
	}
	
	obj_init(&nuke->o, nuke, (obj_f)nuke_obj_destroy);
	collectible_init(nuke);
	
	nuke->timestamp = timestamp;
	nuke->multiplier = multiplier;
	nuke->element = NULL;
	nuke->path = NULL;

	nuke->nuker = strdup(nuker);
	if(!nuke->nuker) {
		NUKE_DBG("Memory error");
		free(nuke);
		return NULL;
	}

	nuke->reason = strdup(reason);
	if(!nuke->reason) {
		NUKE_DBG("Memory error");
		free(nuke->nuker);
		free(nuke);
		return NULL;
	}

	nuke->nukees = collection_new(C_CASCADE);

	if(!collection_add(nukes, nuke)) {
		nuke_destroy(nuke);
		return NULL;
	}

	return nuke;
}

static struct nuke_ctx *nuke_new_from_element(struct vfs_element *element, unsigned int multiplier, char *nuker, char *reason, unsigned long long int timestamp) {
	struct nuke_ctx *nuke;

	if(element->nuke) {
		NUKE_DBG("Element is already nuked.");
		return element->nuke;
	}

	nuke = nuke_new(multiplier, nuker, reason, timestamp);
	if(!nuke) {
		NUKE_DBG("Could not create new nuke");
		return NULL;
	}

	nuke->element = element;
	element->nuke = nuke;
	
	nuke->path = vfs_get_relative_path(vfs_root, element);
	if(!nuke->path) {
		NUKE_DBG("Memory error");
		nuke_destroy(nuke);
		return NULL;
	}

	return nuke;
}

static struct nuke_ctx *nuke_new_from_path(char *path, unsigned int multiplier, char *nuker, char *reason, unsigned long long int timestamp) {
	struct nuke_ctx *nuke;

	nuke = nuke_new(multiplier, nuker, reason, timestamp);
	if(!nuke) {
		NUKE_DBG("Could not create new nuke");
		return NULL;
	}

	nuke->path = strdup(path);
	if(!nuke->path) {
		NUKE_DBG("Memory error");
		nuke_destroy(nuke);
		return NULL;
	}

	return nuke;
}

/* THIS is the way the scripts have to register thier nukes */
struct nuke_ctx *nuke_add(struct vfs_element *element, unsigned int multiplier, char *nuker, char *reason) {
	struct nuke_ctx *nuke;

	if(!element || !nuker || !reason) {
		NUKE_DBG("Param error");
		return NULL;
	}

	if(element->nuke) {
		NUKE_DBG("Element is already nuked.");
		return element->nuke;
	}

	nuke = nuke_new_from_element(element, multiplier, nuker, reason, time_now());
	if(!nuke) {
		NUKE_DBG("Could not create new nuke");
		return NULL;
	}

	//nuke_dump_all();
	logging_write(NUKELOG_FILE, "nuke;%s;%u;%s;%s;%I64u\r\n", nuke->path, nuke->multiplier, nuke->nuker, nuke->reason, nuke->timestamp);

	return nuke;
}

static struct nuke_nukee *nuke_new_nukee(struct nuke_ctx *nuke, char *name, unsigned long long int ammount) {
	struct nuke_nukee *nukee;

	nukee = malloc(sizeof(struct nuke_nukee));
	if(!nukee) {
		NUKE_DBG("Memory error");
		return NULL;
	}
	
	obj_init(&nukee->o, nukee, (obj_f)nukee_obj_destroy);
	collectible_init(nukee);

	nukee->name = strdup(name);
	if(!nukee->name) {
		NUKE_DBG("Memory error");
		free(nukee);
		return NULL;
	}

	nukee->ammount = ammount;
	nukee->nuke = nuke;

	if(!collection_add(nuke->nukees, nukee)) {
		nuke_destroy_nukee(nukee);
		return NULL;
	}

	return nukee;
}

struct nuke_nukee *nukee_add(struct nuke_ctx *nuke, char *name, unsigned long long int ammount) {
	struct nuke_nukee *nukee;

	if(!nuke || !name) {
		NUKE_DBG("Param error");
		return NULL;
	}

	nukee = nuke_new_nukee(nuke, name, ammount);
	if(!nukee) {
		NUKE_DBG("Could not create new nukee");
		return NULL;
	}

	//nuke_dump_all();
	logging_write(NUKELOG_FILE, "nukee;%s;%s;%I64u\r\n", nukee->nuke->path, nukee->name, nukee->ammount);

	return nukee;
}

void nukee_del(struct nuke_nukee *nukee) {

	if(!nukee) {
		NUKE_DBG("Param error");
		return;
	}

	nuke_destroy_nukee(nukee);
	nuke_dump_all();

	return;
}

void nuke_del(struct nuke_ctx *nuke) {

	if(!nuke) {
		NUKE_DBG("Param error");
		return;
	}

	nuke_destroy(nuke);

	/* dump to file */
	nuke_dump_all();

	return;
}

static int nuke_get_matcher(struct collection *c, struct nuke_ctx *nuke, char *path) {

	return !stricmp(nuke->path, path);
}

/* lookup a nuke from its (normalized) path */
struct nuke_ctx *nuke_get(char *path) {

	if(!path) {
		NUKE_DBG("Param error");
		return NULL;
	}

	return collection_match(nukes, (collection_f)nuke_get_matcher, path);
}

static int nuke_check_matcher(struct collection *c, struct nuke_ctx *nuke, char *path) {

	if(nuke->element)
		return 0;

	if(!stricmp(nuke->path, path)) {
		return 1;
	}

	return 0;
}

/* we have an element and we want to match it with any nuke possible. */
struct nuke_ctx *nuke_check(struct vfs_element *element) {
	struct nuke_ctx *nuke;
	char *path;

	if(element->nuke) {
		NUKE_DBG("nuke_check on already nuked element!?");
		return element->nuke;
	}

	path = vfs_get_relative_path(vfs_root, element);
	if(!path) {
		NUKE_DBG("Memory error");
		return NULL;
	}

	nuke = collection_match(nukes, (collection_f)nuke_check_matcher, path);
	free(path);

	if(nuke) {
		nuke->element = element;
		element->nuke = nuke;
	}

	return nuke;
}

static int nuke_check_all_callback(struct collection *c, struct nuke_ctx *nuke, void *param) {
	struct vfs_element *element;

	if(nuke->element) {
		return 1;
	}

	element = vfs_find_element(vfs_root, nuke->path);
	if(!element) {
		/* element is not found */
		return 1;
	}

	element->nuke = nuke;
	nuke->element = element;

	return 1;
}

/* check all nukes and try to match them with files/folders on the vfs */
int nuke_check_all() {

	collection_iterate(nukes, (collection_f)nuke_check_all_callback, NULL);

	return 1;
}

int nukee_get_matcher(struct collection *c, struct nuke_nukee *nukee, char *name) {

	return !stricmp(nukee->name, name);
}

/* return the nukee structure from the nuke context matching the given name */
struct nuke_nukee *nukee_get(struct nuke_ctx *nuke, char *name) {

	if(!nuke || !name) {
		NUKE_DBG("Params error");
		return NULL;
	}

	return collection_match(nuke->nukees, (collection_f)nukee_get_matcher, name);
}

int nuke_dump_all_nukees_callback(struct collection *c, struct nuke_nukee *nukee, FILE *f) {

	logging_write_file(f, "nukee;%s;%s;%I64u\r\n", nukee->nuke->path, nukee->name, nukee->ammount);

	return 1;
}

int nuke_dump_all_callback(struct collection *c, struct nuke_ctx *nuke, FILE *f) {

	logging_write_file(f, "nuke;%s;%u;%s;%s;%I64u\r\n", nuke->path, nuke->multiplier, nuke->nuker, nuke->reason, nuke->timestamp);

	collection_iterate(nuke->nukees, (collection_f)nuke_dump_all_nukees_callback, f);

	return 1;
}

/* Dump the nukes to file. */
int nuke_dump_all() {
	FILE *f;

	f = fopen(NUKELOG_FILE, "w");
	if(!f) {
		NUKE_DBG("fopen failed on " NUKELOG_FILE);
		return 0;
	}

	collection_iterate(nukes, (collection_f)nuke_dump_all_callback, f);

	fclose(f);

	return 1;
}

int nuke_load_all() {
	unsigned int current, line;
	unsigned int length;
	char *ptr, *next;
	char *buffer;

	char *what;
	char *_path, *_multiplier, *_nuker, *_reason, *_timestamp;
	char *_name, *_ammount;

	struct nuke_ctx *nuke;

	buffer = config_load_file(NUKELOG_FILE, &length);
	if(!buffer) {
		NUKE_DBG("Could not open file " NUKELOG_FILE);
		
		/* no error, file may not exist */
		return 1;
	}
	
	current=0;
	next = buffer;
	while(current<length) {
		ptr = next;
		
		for(line=0;current+(line+1)<length;line++) {
			if((ptr[line] == '\r') || (ptr[line] == '\n')) {
				ptr[line] = 0;
				break;
			}
		}
		
		current += (line+1);
		next = ptr + (line+1);
		
		if(!line) {
			continue;
		}
		
		/* extract all infos from the file */
		
		what = ptr;
		ptr = strchr(ptr, ';');
		if(!ptr || !(*what)) continue;
		*ptr = 0; ptr++;
		
		_path = ptr;
		ptr = strchr(ptr, ';');
		if(!ptr || !(*_path)) continue;
		*ptr = 0; ptr++;
		
		if(!stricmp(what, "nuke")) {
			
			_multiplier = ptr;
			ptr = strchr(ptr, ';');
			if(!ptr || !(*_multiplier)) continue;
			*ptr = 0; ptr++;
			
			_nuker = ptr;
			ptr = strchr(ptr, ';');
			if(!ptr || !(*_nuker)) continue;
			*ptr = 0; ptr++;
			
			_reason = ptr;
			ptr = strchr(ptr, ';');
			if(!ptr || !(*_reason)) continue;
			*ptr = 0; ptr++;
			
			_timestamp = ptr;
			
			nuke = nuke_new_from_path(_path, atoi(_multiplier), _nuker, _reason, _atoi64(_timestamp));
			if(!nuke) {
				NUKE_DBG("Failed to add nuke for path %s", _path);
				continue;
			}
			
		} else if(!stricmp(what, "nukee")) {
			
			_name = ptr;
			ptr = strchr(ptr, ';');
			if(!ptr || !(*_name)) continue;
			*ptr = 0; ptr++;
			
			_ammount = ptr;
			
			nuke = nuke_get(_path);
			if(!nuke) {
				NUKE_DBG("Failed to get nuke for path %s, cannot add nukee (%s/%s)", _path, _name, _ammount);
				continue;
			}
			
			if(!nuke_new_nukee(nuke, _name, _atoi64(_ammount))) {
				NUKE_DBG("Add nukee failed for (%s/%s) on %s", _name, _ammount, _path);
				continue;
			}
		} else {
			NUKE_DBG("Nukelog corrupt? invalid \"what\": %s", what);
			continue;
		}
	}

	free(buffer);

	return 1;
}

int nuke_init() {

	NUKE_DBG("Loading ...");

	nukes = collection_new(C_CASCADE);

	/* Load all nukes from file */
	if(!nuke_load_all()) {
		NUKE_DBG("Could not load nukes from file.");
		return 0;
	}

	/* nuke_check_all() */
	nuke_check_all();

	return 1;
}

int nuke_reload() {

	NUKE_DBG("Reloading ...");

	/* Just do nuke_check_all() */
	nuke_check_all();

	nuke_dump_all();

	return 1;
}


void nuke_free() {

	NUKE_DBG("Unloading ...");

	/* Save all nukes to file */
	nuke_dump_all();

	/* destroy all nukes */
	if(nukes) {
		collection_destroy(nukes);
		nukes = NULL;
	}

	return;
}
