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
#include <stdio.h>

#include "constants.h"
#include "logging.h"
#include "vfs.h"
#include "sfv.h"
#include "collection.h"
#include "slaves.h"
#include "asynch.h"

/* NOTE:
	These sets of functions will fill the sfv structure of a given folder.
	When the folder will get deleted, or any .sfv file in it, the sfv
	structure should get deleted. It is implied that no more than ONE
	.sfv file will be uploaded in a folder at any time, because it will
	not be possible to delete the entries attached to one specific .sfv file
 */

static unsigned int sfv_query_callback(struct slave_connection *cnx, struct slave_asynch_command *cmd, struct packet *p) {
	unsigned int length, i, current_length;
	struct vfs_element *file;
	struct vfs_element *parent;
	struct sfv_entry *entry;

	if(!p) {
		SFV_DBG("%I64u: query failed", cmd->uid);
		return 0;
	}

	if(p->type != IO_SFV) {
		SFV_DBG("%I64u: returned non-IO_SFV response.", cmd->uid);
		return 1;
	}

	/* get the local file for wich we received the sfv infos */
	file = vfs_find_element(cnx->slave->vroot, cmd->data);
	if(!file) {
		SFV_DBG("%I64u: file %s was not found in vfs", cmd->uid, cmd->data);
		return 1;
	}

	/* we will store the sfv info at the parent level */
	parent = file->parent;
	
	length = (p->size - sizeof(struct packet));
	if(!parent->sfv) {
		parent->sfv = malloc(sizeof(struct sfv_ctx));
		if(!parent->sfv) {
			SFV_DBG("Memory error");
			return 1;
		}

		parent->sfv->entries = collection_new();

		parent->sfv->element = parent;
	}

	for(i=0;i<length;) {
		if((length - i) < sizeof(struct sfv_entry)) break;
		entry = (struct sfv_entry *)&p->data[i];
		current_length = (sizeof(struct sfv_entry) + strlen(entry->filename) + 1);
		i += current_length;

		if(!sfv_add_entry(parent->sfv, entry->filename, entry->crc)) {
			SFV_DBG("%I64u: Failed to add %s crc (%08x)", cmd->uid, entry->filename, entry->crc);
			continue;
		}
	}

	return 1;
}

/* in the current implementation, this function override the
	current crc if an entry exists for the 'filename'.
TODO:
	 */
struct sfv_entry *sfv_add_entry(struct sfv_ctx *sfv, char *filename, unsigned int crc) {
	struct sfv_entry *entry;

	if(!sfv || !filename) return NULL;

	entry = sfv_get_entry(sfv, filename);
	if(!entry) {
		entry = malloc(sizeof(struct sfv_entry)+strlen(filename)+1);
		if(!entry) {
			SFV_DBG("Memory error");
			return 0;
		}

		entry->crc = crc;
		sprintf(entry->filename, filename);

		collection_add(sfv->entries, entry);
	} else
		if(entry->crc != crc) return NULL;

	return entry;
}

static unsigned int get_entry_callback(struct collection *c, void *item, void *param) {
	struct {
		struct sfv_entry *entry;
		char *filename;
	} *ctx = param;
	struct sfv_entry *entry = item;

	if(!stricmp(entry->filename, ctx->filename)) {
		ctx->entry = entry;
		return 0;
	}

	return 1;
}

/* this function does not check for duplicates, it returns the
	first entry matching the given filename */
struct sfv_entry *sfv_get_entry(struct sfv_ctx *sfv, char *filename) {
	struct {
		struct sfv_entry *entry;
		char *filename;
	} ctx = { NULL, filename };

	if(!sfv || !filename) return 0;

	collection_iterate(sfv->entries, get_entry_callback, &ctx);

	return ctx.entry;
}

/* delete the sfv structure and all its entries */
void sfv_delete(struct sfv_ctx *sfv) {

	if(!sfv) return;

	if(sfv->entries) {
		collection_void(sfv->entries);
		while(collection_size(sfv->entries)) {
			void *first = collection_first(sfv->entries);
			free(first);
			collection_delete(sfv->entries, first);
		}
		collection_destroy(sfv->entries);
		sfv->entries = NULL;
	}

	sfv->element->sfv = NULL;
	sfv->element = NULL;

	free(sfv);

	return;
}

/* ask the slave to send the infos from a sfv file */
unsigned int make_sfv_query(struct slave_connection *cnx, struct vfs_element *file) {
	struct slave_asynch_command *cmd;
	char *path;

	if(!cnx || !file) return 0;

	/* prepare the IO_SFV command data */
	path = vfs_get_relative_path(cnx->slave->vroot, file);
	if(!path) {
		SFV_DBG("Memory error");
		return 0;
	}

	/* we do not directly keep a pointer to the file here because
		by the time we receive the response, it may be deleted */
	cmd = asynch_new(cnx, IO_SFV, MASTER_ASYNCH_TIMEOUT, path, strlen(path)+1, sfv_query_callback, NULL);
	free(path);
	if(!cmd) return 0;

	return 1;
}

