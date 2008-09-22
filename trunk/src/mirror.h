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

#ifndef __MIRROR_H
#define __MIRROR_H

#include "constants.h"
#include "obj.h"

#ifndef NO_FTPD_DEBUG
#  define DEBUG_MIRROR
#endif

#ifdef DEBUG_MIRROR
# ifdef FTPD_DEBUG_TO_CONSOLE
#  define MIRROR_DBG(format, arg...) printf("["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg)
# else
#  define MIRROR_DBG(format, arg...) logging_write("debug.log", "["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg)
# endif
#else
#  define MIRROR_DBG(format, arg...)
#endif

extern struct collection *mirrors; /* collection of struct mirror_ctx */

typedef void* mirror_param;

struct mirror_lua_ctx {
	char *func_name;
	void *param;
} __attribute__((packed));

typedef struct mirror_side mirror_side;
struct mirror_side {
	char finished;
	char pasv;

	/* transfer checksum */
	unsigned int checksum;
	unsigned long long int xfered; /* current size that was transfered, updated by the stats */
	unsigned long long int last_alive; /* last time it was reported by the stats */

	struct slave_connection *cnx; /* slave connection */
	struct vfs_element *file; /* transfered file */
	struct slave_asynch_command *cmd; /* asynch command */
} __attribute__((packed));

typedef struct mirror_ctx mirror_ctx;
struct mirror_ctx {
	struct obj o;
	struct collectible c;

	/* volatile config file, not backed up on disk */
	struct config_file *volatile_config;

	unsigned long long int uid; /* unique id shared with both slaves */
	unsigned long long int timestamp; /* transfer start */

	/* TODO? */
	//unsigned long long int restart;

	int (*callback)(struct mirror_ctx *mirror, int success, void *param);
	void *callback_param;

	/* success of the mirror operation  */
	char success;

	struct mirror_side source;
	struct mirror_side target;
} __attribute__((packed));

int mirror_init();
void mirror_free();

unsigned int mirror_cancel(struct mirror_ctx *mirror);
struct mirror_ctx *mirror_new(
	struct slave_connection *src_cnx,
	struct vfs_element *src_file,
	struct slave_connection *dest_cnx,
	struct vfs_element *dest_file,
	int (*callback)(struct mirror_ctx *mirror, int success, void *param),
	void *param
);

struct mirror_ctx *mirror_lua_new(
	struct slave_connection *src_cnx,
	struct vfs_element *src_file,
	struct slave_connection *dest_cnx,
	struct vfs_element *dest_file,
	char *func_name,
	void *param
);

#endif /* __MIRROR_H */
