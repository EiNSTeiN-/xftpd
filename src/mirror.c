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
#include "mirror.h"
#include "slaves.h"
#include "io.h"
#include "ftpd.h"
#include "time.h"
#include "sfv.h"
#include "mirror.h"
#include "events.h"
#include "main.h"
#include "luainit.h"
#include "asynch.h"
#include "obj.h"
#include "config.h"


struct collection *mirrors = NULL;

int mirror_init() {
	
	MIRROR_DBG("Loading ...");

	mirrors = collection_new(C_CASCADE);
	
	return 1;
}

void mirror_free() {

	MIRROR_DBG("Unloading ...");

	/* TODO! */

	if(mirrors) {
		collection_destroy(mirrors);
	}

	return;
}

/* p is NULL on timeout and on read error */
static unsigned int mirror_slave_transfer_query_callback(struct slave_connection *cnx, struct slave_asynch_command *cmd, struct packet *p) {
	struct mirror_ctx *mirror = (struct mirror_ctx *)cmd->param;
	struct slave_transfer_reply *reply;
	struct mirror_side *side;
	unsigned int source; /* 1 if this callback is from the source */

	MIRROR_DBG("" LLU ": Mirror transfer response received", cmd->uid);

	if(mirror->source.cnx == cnx) {
		side = &mirror->source;
		source = 1;
	} else if(mirror->target.cnx == cnx) {
		side = &mirror->target;
		source = 0;
	} else {
		MIRROR_DBG("Wierd...");
		mirror_cancel(mirror);
		return 1;
	}

	/* Pretty important */
	side->cmd = NULL;

	if(!p || (p->type == IO_FAILURE)) {
		MIRROR_DBG("" LLU ": Mirror failed", cmd->uid);
		mirror_cancel(mirror);
		return 1;
	}

	reply = (struct slave_transfer_reply *)&p->data;

	if(p->type == IO_SLAVE_TRANSFERED) {
		if((p->size - sizeof(struct packet)) < sizeof(struct slave_transfer_reply)) {
			/* protocol error */
			MIRROR_DBG("Protocol error");
			mirror_cancel(mirror);
			return 0;
		}

		/* update new size in vfs only if it's not already done */
		if(!source && !collection_size(side->file->available_from) && !collection_size(side->file->offline_from)) {
			
			vfs_set_size(side->file, reply->filesize);
			vfs_modify(side->file, time_now());
			vfs_set_checksum(side->file, reply->checksum);
		
			/* if the file was .sfv then request its infos */
			if((strlen(side->file->name) > 4) &&
				!strcasecmp(&side->file->name[strlen(side->file->name)-4], ".sfv")) {
				if(!make_sfv_query(side->cnx, side->file)) {
					MIRROR_DBG("Could not make sfv query for %s", side->file->name);
				}
			}
		}

		side->xfered = reply->xfersize;
		side->checksum = reply->checksum;

		if(!source) {
			/* link this file to the slave's available_from collection */
			slave_mark_online_from(cnx, side->file);
		}

		/* mark this side as finished */
		side->finished = 1;

		/*
			if both sides are finished, then it's time to free
			this mirror context
		*/
		if(mirror->source.finished && mirror->target.finished) {
			mirror->success = 1;
			mirror_cancel(mirror);
		}

		return 1;
	}

	/* cleanup data connection */
	mirror_cancel(mirror);
	return 0;
}

/* p is NULL on timeout and on read error */
static unsigned int mirror_slave_listen_query_callback(struct slave_connection *cnx, struct slave_asynch_command *cmd, struct packet *p) {
	struct slave_listen_reply *reply;
	struct mirror_ctx *mirror = (struct mirror_ctx *)cmd->param;
	struct slave_transfer_request *data;
	unsigned int length;
	struct mirror_side *side;
	unsigned int source; /* 1 if this callback is from the source */

	MIRROR_DBG("" LLU ": Mirror listen response received", cmd->uid);

	if(mirror->source.cnx == cnx) {
		side = &mirror->source;
		source = 1;
	} else if(mirror->target.cnx == cnx) {
		side = &mirror->target;
		source = 0;
	} else {
		MIRROR_DBG("Wierd...");
		mirror_cancel(mirror);
		return 1;
	}

	side->cmd = NULL;

	if(!p || p->type == IO_FAILURE) {
		MIRROR_DBG("" LLU ": Mirror failed", cmd->uid);
		mirror_cancel(mirror);
		return 1;
	}

	reply = (struct slave_listen_reply *)&p->data;

	if(p->type == IO_SLAVE_LISTENING) {
		/* the slave should have answered with
			the port/ip the client should connect to */
		if((p->size - sizeof(struct packet)) < sizeof(struct slave_listen_reply)) {
			/* protocol error */
			MIRROR_DBG("Protocol error");
			mirror_cancel(mirror);
			return 0;
		}

		/* send the transfer query for both slaves */
		data = ftpd_transfer(mirror->uid, mirror->source.cnx->slave->vroot, mirror->source.file,
					reply->ip, reply->port, mirror->source.pasv, 0 /* we're downloading from the source */, 0, &length);
		if(!data) {
			MIRROR_DBG("Could not build the (source) transfer data.");
			mirror_cancel(mirror);
			return 1;
		}
		mirror->source.cmd = asynch_new(
			mirror->source.cnx,
			IO_SLAVE_TRANSFER,
			-1,			/* transfer queries does not timeout because they come back only when the transfer succeed or fail */
			(void*)data,
			length,
			mirror_slave_transfer_query_callback,
			mirror
		);
		free(data);
		if(!mirror->source.cmd) {
			MIRROR_DBG("Could not make the (source) transfer query.");
			mirror_cancel(mirror);
			return 1;
		}

		data = ftpd_transfer(mirror->uid, mirror->target.cnx->slave->vroot, mirror->target.file,
					reply->ip, reply->port, mirror->target.pasv, 1 /* we're uploading to the target */, 0, &length);
		if(!data) {
			MIRROR_DBG("Could not build the (target) transfer data.");
			mirror_cancel(mirror);
			return 1;
		}
		mirror->target.cmd = asynch_new(
			mirror->target.cnx,
			IO_SLAVE_TRANSFER,
			-1,
			(void*)data,
			length,
			mirror_slave_transfer_query_callback,
			mirror
		);
		free(data);
		if(!mirror->target.cmd) {
			MIRROR_DBG("Could not make the (target) transfer query.");
			mirror_cancel(mirror);
			return 1;
		}
	
		return 1;
	}

	/* protocol error */
	mirror_cancel(mirror);
	return 0;
}

/* ask the slave to start listening for a client
	the callback will send 200 or 425 to the client */
static unsigned int mirror_make_slave_listen_query(struct mirror_ctx *mirror) {
	struct slave_listen_request data;
	struct mirror_side *side;

	side = mirror->source.pasv ? &mirror->source : &mirror->target;

	data.xfer_uid = mirror->uid;

	side->cmd = asynch_new(side->cnx, IO_SLAVE_LISTEN, MASTER_ASYNCH_TIMEOUT, (void*)&data, sizeof(data), mirror_slave_listen_query_callback, mirror);
	if(!side->cmd) return 0;

	MIRROR_DBG("" LLU ": Mirror listen query built", side->cmd->uid);

	return 1;
}

static unsigned int find_destination_slave(struct collection *c, void *item, void *param) {
	struct {
		struct slave_connection *cnx;
		struct vfs_element *file;
		unsigned int found;
	} *ctx = param;
	struct mirror_ctx *mirror = item;

	if((mirror->target.cnx == ctx->cnx) && (mirror->target.file == ctx->file)) {
		ctx->found = 1;
		return 0;
	}

	return 1;
}

static void mirror_obj_destroy(struct mirror_ctx *mirror) {
	
	collectible_destroy(mirror);

	MIRROR_DBG("Destroying");

	/* We call the callback whenever the mirror gets deleted */
	if(mirror->callback) {
		(*mirror->callback)(mirror, mirror->success, mirror->callback_param);
		mirror->callback = NULL;
		mirror->callback_param = NULL;
	}

	if(mirror->volatile_config) {
		config_close(mirror->volatile_config);
		mirror->volatile_config = NULL;
	}

	/* cancel both asynch event */
	if(mirror->source.cmd) {
		asynch_destroy(mirror->source.cmd, NULL);
		mirror->source.cmd = NULL;
	}
	if(mirror->target.cmd) {
		asynch_destroy(mirror->target.cmd, NULL);
		mirror->target.cmd = NULL;
	}

	/* TODO: send a cancel to both slaves for the xfer */
	
	/*
		if the file is no longer available from anywhere,
		then delete it from the vfs.
	*/
	if(mirror->target.file) {
		if (!collection_size(mirror->target.file->mirror_to) &&
			!collection_size(mirror->target.file->available_from) &&
			!collection_size(mirror->target.file->offline_from)) {
			/* wipe so we're sure the slave will have it deleted */
			ftpd_wipe(mirror->target.file);
		}
	} else {
		MIRROR_DBG("Deleting mirror where target file is NULL!");
	}

	/* unlink from both file's mirror collection */
	mirror->source.file = NULL;
	mirror->target.file = NULL;

	/* unlink from both slave's mirror collection */
	mirror->source.cnx = NULL;
	mirror->target.cnx = NULL;

	collection_delete(mirrors, mirror);
	free(mirror);

	return;
}

/*
	Mirror the content of one file from one
	slave to the same file or another file
	on another slave. Both files must already
	exist.
*/
struct mirror_ctx *mirror_new(
	struct slave_connection *src_cnx,
	struct vfs_element *src_file,
	struct slave_connection *dest_cnx,
	struct vfs_element *dest_file,
	int (*callback)(struct mirror_ctx *mirror, int success, void *param),
	void *param
) {
	struct mirror_ctx *mirror;
	struct {
		struct slave_connection *cnx;
		struct vfs_element *file;
		unsigned int found;
	} ctx = { dest_cnx, dest_file, 0 };

	if(!src_cnx || !src_file || !dest_cnx || !dest_file) {
		MIRROR_DBG("Parameter error");
		return NULL;
	}

	if(src_file->type != VFS_FILE) {
		MIRROR_DBG("Source file is not VFS_FILE-type");
		return NULL;
	}

	if(dest_file->type != VFS_FILE) {
		MIRROR_DBG("Destination file is not VFS_FILE-type");
		return NULL;
	}

	/* don't transfer a file if the dest file is bigger than the source file */
	//if(dest_file->size >= src_file->size) return NULL;

	/* don't transfer if the source file is not available on the source slave */
	if(!collection_find(src_file->available_from, src_cnx)) {
		MIRROR_DBG("Source file is not available from the source slave");
		return NULL;
	}

	/* don't transfer if the destination file is already on the destination slave */
	if(collection_find(dest_file->available_from, dest_cnx)) {
		MIRROR_DBG("Destination file is already on the destination slave");
		return NULL;
	}

	/* don't transfer if the source slave is the destination slave */
	if(src_cnx == dest_cnx) {
		MIRROR_DBG("Source slave IS the destination slave");
		return NULL;
	}

	/* don't transfer if the destination slave is already used as a target
		for the same destination file in any mirror operation */
	collection_iterate(mirrors, (collection_f)find_destination_slave, &ctx);
	if(ctx.found) {
		MIRROR_DBG("This file is already being mirrored to that slave.");
		return NULL;
	}

	/* Everything should be good for the transfer to begin. */

	mirror = malloc(sizeof(struct mirror_ctx));
	if(!mirror) {
		MIRROR_DBG("Memory error");
		return NULL;
	}

	obj_init(&mirror->o, mirror, (obj_f)mirror_obj_destroy);
	collectible_init(mirror);

	mirror->callback = callback;
	mirror->callback_param = param;

	mirror->success = 0;

	//mirror->canceled = 0;
	mirror->uid = ftpd_next_xfer();
	mirror->timestamp = time_now();

	/* TODO?: set the restart point */
	//mirror->restart = dest_file->size;

	mirror->source.finished = 0;
	mirror->target.finished = 0;
	
	mirror->source.xfered = 0;
	mirror->target.xfered = 0;
	
	mirror->source.last_alive = time_now();
	mirror->target.last_alive = time_now();

	/* by default the source is doing pasv unless
		there's a rule that says it's not possible */
	mirror->source.pasv = 1;
	mirror->target.pasv = 0;

	mirror->source.cnx = src_cnx;
	mirror->source.file = src_file;

	mirror->target.cnx = dest_cnx;
	mirror->target.file = dest_file;

	/* link to both file's mirror collection */
	if(!collection_add(src_file->mirror_from, mirror)) {
		MIRROR_DBG("Collection error");
		free(mirror);
		return NULL;
	}
	if(!collection_add(dest_file->mirror_to, mirror)) {
		MIRROR_DBG("Collection error");
		collection_delete(src_file->mirror_from, mirror);
		free(mirror);
		return NULL;
	}

	/* link to both slave's mirror collection */
	if(!collection_add(src_cnx->mirror_from, mirror)) {
		MIRROR_DBG("Collection error");
		collection_delete(dest_file->mirror_to, mirror);
		collection_delete(src_file->mirror_from, mirror);
		free(mirror);
		return NULL;
	}
	if(!collection_add(dest_cnx->mirror_to, mirror)) {
		MIRROR_DBG("Collection error");
		collection_delete(src_cnx->mirror_from, mirror);
		collection_delete(dest_file->mirror_to, mirror);
		collection_delete(src_file->mirror_from, mirror);
		free(mirror);
		return NULL;
	}

	if(!collection_add(mirrors, mirror)) {
		MIRROR_DBG("Collection error");
		collection_delete(dest_cnx->mirror_to, mirror);
		collection_delete(src_cnx->mirror_from, mirror);
		collection_delete(dest_file->mirror_to, mirror);
		collection_delete(src_file->mirror_from, mirror);
		free(mirror);
		return NULL;
	}

	mirror->volatile_config = config_volatile();
	if(!mirror->volatile_config) {
		MIRROR_DBG("Memory error");
		collection_delete(dest_cnx->mirror_to, mirror);
		collection_delete(src_cnx->mirror_from, mirror);
		collection_delete(dest_file->mirror_to, mirror);
		collection_delete(src_file->mirror_from, mirror);
		free(mirror);
		return NULL;
	}
	
	mirror->source.cmd = NULL;
	mirror->target.cmd = NULL;
	
	/*
		The first step of the mirror chain is to ask
		the first slave to listen
	*/
	if(!mirror_make_slave_listen_query(mirror)) {
		MIRROR_DBG("Could not make \"slave listen\" query.");
		config_close(mirror->volatile_config);
		collection_delete(mirrors, mirror);
		collection_delete(dest_cnx->mirror_to, mirror);
		collection_delete(src_cnx->mirror_from, mirror);
		collection_delete(dest_file->mirror_to, mirror);
		collection_delete(src_file->mirror_from, mirror);
		free(mirror);
		return NULL;
	}

	return mirror;
}

unsigned int mirror_cancel(struct mirror_ctx *mirror) {

	if(!mirror) {
		return 0;
	}

	obj_destroy(&mirror->o);
	
	return 1;
}

