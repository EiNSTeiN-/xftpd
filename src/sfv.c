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

#include <stdio.h>

#include "constants.h"
#include "logging.h"
#include "vfs.h"
#include "sfv.h"
#include "collection.h"
#include "slaves.h"
#include "asynch.h"
#include "events.h"

/* NOTE:
	These sets of functions will fill the sfv structure of a given folder.
	When the folder will get deleted, or any .sfv file in it, the sfv
	structure should get deleted. It is implied that no more than ONE
	.sfv file will be uploaded in a folder at any time, because it will
	not be possible to delete the entries attached to one specific .sfv file
 */

static void sfv_obj_destroy(struct sfv_ctx *sfv) {
	
	collectible_destroy(sfv);

	if(sfv->entries) {
		collection_destroy(sfv->entries);
		sfv->entries = NULL;
	}

	if(sfv->element) {
		sfv->element->sfv = NULL;
		sfv->element = NULL;
	}

	free(sfv);
	
	return;
}

struct sfv_ctx *sfv_new(struct vfs_element *folder) {
	struct sfv_ctx *sfv;
	
	sfv = malloc(sizeof(struct sfv_ctx));
	if(!sfv) {
		SFV_DBG("Memory error");
		return NULL;
	}
	
	obj_init(&sfv->o, sfv, (obj_f)sfv_obj_destroy);
	collectible_init(sfv);
	
	sfv->entries = collection_new(C_CASCADE);
	sfv->element = folder;
	folder->sfv = sfv;
	
	return sfv;
}

static unsigned int sfvlog_query_callback(struct slave_connection *cnx, struct slave_asynch_command *cmd, struct packet *p) {
	struct sfvlog_file *sfvfile;
	struct sfvlog_entry *sfventry;
	unsigned int length, file_current, i;
	struct vfs_element *file;
	struct vfs_element *parent;
		
	unsigned int total_sfv = 0, total_entries = 0;
	
	if(!p) {
		SFV_DBG("" LLU ": query failed", cmd->uid);
		return 0;
	}

	if(p->type != IO_SFVLOG) {
		SFV_DBG("" LLU ": returned non-IO_SFVLOG response.", cmd->uid);
		
		if(!event_onSlaveIdentSuccess(cnx)) {
			SLAVES_DBG("" LLU ": Slave connection rejected by onSlaveIdentSuccess", p->uid);
			return 0;
		}
		
		/* from now on, this slave can send and receive files */
		cnx->ready = 1;
		
		return 1;
	}
	
	length = packet_data_length(p);
	SFV_DBG("Sfv log size %u bytes", length);
	
	file_current = 0;
	sfvfile = (struct sfvlog_file *)((char *)&p->data[0]);
	while(1) {
		if(file_current == length) break;
		if(file_current > length) {
			SFV_DBG("File entry is past the end of the buffer of %u bytes", (file_current - length));
			break;
		}
		if((length - file_current) < sizeof(struct sfvlog_file)) {
			SFV_DBG("File entry size is too short (only %u bytes)", (length - file_current));
			break;
		}
		if(sfvfile->next < sizeof(struct sfvlog_file)) {
			SFV_DBG("Next file entry size overlap the end of the current entry (only %u bytes)", sfvfile->next);
			break;
		}
		
		/* get the local file for wich we received the sfv infos */
		file = vfs_find_element(cnx->slave->vroot, sfvfile->filename);
		if(file) {
			unsigned int entry_current;
			unsigned int entries_length;
			/* we will store the sfv info at the parent level */
			parent = file->parent;
			
			entries_length = sfvfile->next - (sizeof(struct sfvlog_file) + strlen(sfvfile->filename) + 1);
			
			//SFV_DBG("Adding entries at %u / next %u / size %u", file_current, file_current+sfvfile->next, entries_length);
			total_sfv++;
			
			if(!parent->sfv) {
				sfv_new(parent);
			}
			
			if(parent->sfv) {
				entry_current = 0;
				sfventry = (struct sfvlog_entry *)(&((char *)sfvfile)[sizeof(struct sfvlog_file) + strlen(sfvfile->filename) + 1]);
				while(1) {
					if(entry_current == entries_length) break;
					if(entry_current > entries_length) {
						SFV_DBG("Sfv entry is past the end of the buffer of %u bytes", (entry_current - entries_length));
						break;
					}
					if((entries_length - entry_current) < sizeof(struct sfvlog_entry)) {
						SFV_DBG("Sfv entry size is too short (only %u bytes)", (entries_length - entry_current));
						break;
					}
					
					//SFV_DBG("Adding entry %s / %08x at %u", sfventry->filename, sfventry->crc, file_current+entry_current);
					total_entries++;
					
					/* add the entry */
					if(!sfv_add_entry(parent->sfv, sfventry->filename, sfventry->crc)) {
						SFV_DBG("" LLU ": Failed to add %s crc (%08x)", cmd->uid, sfventry->filename, sfventry->crc);
					}
					
					i = sizeof(struct sfvlog_entry) + strlen(sfventry->filename) + 1;
					sfventry = (struct sfvlog_entry *)(&((char *)sfventry)[i]);
					entry_current += i;
				}
			} else {
				SFV_DBG("Could not create sfv structure");
			}
		} else {
			SFV_DBG("Could not find file %s", sfvfile->filename);
		}
		
		i = sfvfile->next;
		sfvfile = (struct sfvlog_file *)((char *)&p->data[file_current+i]);
		file_current += i;
	}
	
	SFV_DBG("Received %u sfv files from %s (%u entries total)", total_sfv, cnx->slave->name, total_entries);
	
	if(!event_onSlaveIdentSuccess(cnx)) {
		SLAVES_DBG("" LLU ": Slave connection rejected by onSlaveIdentSuccess", p->uid);
		return 0;
	}
	
	/* from now on, this slave can send and receive files */
	cnx->ready = 1;
	
	return 1;
}

static unsigned int sfv_query_callback(struct slave_connection *cnx, struct slave_asynch_command *cmd, struct packet *p) {
	unsigned int length, i, current_length;
	struct vfs_element *file;
	struct vfs_element *parent;
	struct sfv_entry *entry;

	if(!p) {
		SFV_DBG("" LLU ": query failed", cmd->uid);
		return 0;
	}

	if(p->type != IO_SFV) {
		SFV_DBG("" LLU ": returned non-IO_SFV response.", cmd->uid);
		return 1;
	}

	/* get the local file for wich we received the sfv infos */
	file = vfs_find_element(cnx->slave->vroot, cmd->data);
	if(!file) {
		SFV_DBG("" LLU ": file %s was not found in vfs", cmd->uid, cmd->data);
		return 1;
	}

	/* we will store the sfv info at the parent level */
	parent = file->parent;
	
	length = (p->size - sizeof(struct packet));
	if(!parent->sfv) {
		if(!sfv_new(parent)) {
			SFV_DBG("Could not create new sfv file");
			return 1;
		}
	}

	for(i=0;i<length;) {
		if((length - i) < sizeof(struct sfv_entry)) break;
		entry = (struct sfv_entry *)&p->data[i];
		current_length = (sizeof(struct sfv_entry) + strlen(entry->filename) + 1);
		i += current_length;

		if(!sfv_add_entry(parent->sfv, entry->filename, entry->crc)) {
			SFV_DBG("" LLU ": Failed to add %s crc (%08x)", cmd->uid, entry->filename, entry->crc);
			continue;
		}
	}

	return 1;
}

static void sfv_entry_obj_destroy(struct sfv_entry *entry) {
	
	collectible_destroy(entry);
	
	free(entry);
	
	return;
}

/*
	in the current implementation, this function override the
	current crc if an entry exists for the 'filename'.
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
		
		obj_init(&entry->o, entry, (obj_f)sfv_entry_obj_destroy);
		collectible_init(entry);

		entry->crc = crc;
		sprintf(entry->filename, filename);

		collection_add(sfv->entries, entry);
	} else {
		if(entry->crc != crc) return NULL;
	}

	return entry;
}

static int get_entry_callback(struct collection *c, void *item, void *param) {
	struct {
		struct sfv_entry *entry;
		char *filename;
	} *ctx = param;
	struct sfv_entry *entry = item;

	if(!strcasecmp(entry->filename, ctx->filename)) {
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

	collection_iterate(sfv->entries, (collection_f)get_entry_callback, &ctx);

	return ctx.entry;
}

/* delete the sfv structure and all its entries */
void sfv_delete(struct sfv_ctx *sfv) {

	if(!sfv) return;

	collection_void(sfv->entries);
	
	obj_destroy(&sfv->o);

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
	cmd = asynch_new(cnx, IO_SFV, MASTER_ASYNCH_TIMEOUT, (unsigned char *)path, strlen(path)+1, sfv_query_callback, NULL);
	free(path);
	if(!cmd) return 0;

	return 1;
}

static int sfvlog_files_length(struct collection *c, struct vfs_element *file, void *param) {
	struct {
		struct slave_connection *cnx;
		unsigned int length;
	} *ctx = param;
	
	ctx->length += vfs_get_relative_path_length(ctx->cnx->slave->vroot, file) + 1;
	
	return 1;
}

static int sfvlog_append_files(struct collection *c, struct vfs_element *file, void *param) {
	struct {
		struct slave_connection *cnx;
		unsigned int current;
		char *buffer;
	} *ctx = param;
	char *path;
	unsigned int length;
	
	path = vfs_get_relative_path(ctx->cnx->slave->vroot, file);
	if(!path) {
		SFV_DBG("Memory error");
		return 1;
	}
	
	length = strlen(path);
	memcpy(&ctx->buffer[ctx->current], path, length+1);
	
	ctx->current += length+1;
	
	free(path);
	
	return 1;
}

int make_sfvlog_query(struct slave_connection *cnx, struct collection *sfvfiles) {
	struct slave_asynch_command *cmd;
	struct {
		struct slave_connection *cnx;
		unsigned int current;
		char *buffer;
	} append_ctx = { cnx, 0, NULL };
	
	struct {
		struct slave_connection *cnx;
		unsigned int length;
	} length_ctx = { cnx, 0 };

	/*
		Iterate all sfv files that we need and build a big buffer
		with all names that we'll send to the slave, then it'll
		return us a big buffer containing all sfv information
		we need requested.
	*/
	
	collection_iterate(sfvfiles, (collection_f)sfvlog_files_length, &length_ctx);

	append_ctx.buffer = malloc(length_ctx.length);
	if(!append_ctx.buffer) {
		SFV_DBG("Memory error (%u)", length_ctx.length);
		return 0;
	}
	
	collection_iterate(sfvfiles, (collection_f)sfvlog_append_files, &append_ctx);

	/* we do not directly keep a pointer to the file here because
		by the time we receive the response, it may be deleted */
	cmd = asynch_new(cnx, IO_SFVLOG, MASTER_ASYNCH_TIMEOUT, (unsigned char *)append_ctx.buffer, append_ctx.current, sfvlog_query_callback, NULL);
	free(append_ctx.buffer);
	if(!cmd) return 0;
	
	SFV_DBG("Queried %s for %u sfv files", cnx->slave->name, collection_size(sfvfiles));

	return 1;
}
