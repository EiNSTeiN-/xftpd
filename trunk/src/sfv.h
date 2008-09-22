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

#ifndef __SFV_H
#define __SFV_H

#include "constants.h"

#include "debug.h"
#if defined(DEBUG_SFV)
# define SFV_DBG(format, arg...) { _DEBUG_CONSOLE(format, ##arg) _DEBUG_FILE(format, ##arg) }
#else
# define SFV_DBG(format, arg...)
#endif

#include "vfs.h"

typedef struct sfv_entry sfv_entry;
struct sfv_entry {
	struct obj o;
	struct collectible c;
	
	unsigned int crc;
	char filename[];
} __attribute__((packed));

typedef struct sfv_ctx sfv_ctx;
struct sfv_ctx {
	struct obj o;
	struct collectible c;
	
	struct collection *entries; /* collection of sfv_entry structures */
	struct vfs_element *element;
} __attribute__((packed));

struct sfvlog_file {
	unsigned short next; /* offset to the next file in the buffer, zero for the last */
	char filename[]; /* name of this sfv file */
} __attribute__((packed));

struct sfvlog_entry {
	unsigned int crc;
	char filename[];
} __attribute__((packed));

unsigned int make_sfv_query(struct slave_connection *cnx, struct vfs_element *file);
struct sfv_entry *sfv_add_entry(struct sfv_ctx *sfv, char *filename, unsigned int crc);
struct sfv_entry *sfv_get_entry(struct sfv_ctx *sfv, char *filename);
void sfv_delete(struct sfv_ctx *sfv);

int make_sfvlog_query(struct slave_connection *cnx, struct collection *sfvfiles);

#endif /* __SFV_H */
