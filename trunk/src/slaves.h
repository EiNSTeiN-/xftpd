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

#ifndef __SLAVES_H
#define __SLAVES_H

#include "constants.h"

#ifndef NO_FTPD_DEBUG
#  define DEBUG_SLAVES
//#  define DEBUG_SLAVES_DIALOG /* mostly connection dialog (flood) */
#endif

#ifdef DEBUG_SLAVES
# ifdef FTPD_DEBUG_TO_CONSOLE
#  define SLAVES_DBG(format, arg...) printf("["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg)
# else
#  define SLAVES_DBG(format, arg...) logging_write("debug.log", "["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg)
# endif
#else
#  define SLAVES_DBG(format, arg...)
#endif

#ifdef DEBUG_SLAVES_DIALOG
# ifdef FTPD_DEBUG_TO_CONSOLE
#  define SLAVES_DIALOG_DBG(format, arg...) printf("["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg)
# else
#  define SLAVES_DIALOG_DBG(format, arg...) logging_write("debug.log", "["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg)
# endif
#else
#  define SLAVES_DIALOG_DBG(format, arg...)
#endif

#include "io.h"
#include "fsd.h"

extern struct collection *slaves; /* slave_ctx structures */
extern struct collection *connected_slaves;
extern struct collection *connecting_slaves;

typedef struct slave_hello_data hello_data;

struct vfs_element;

/* structure used to keep track of connected peers */
typedef struct slave_connection slave_connection;
struct slave_connection {
	struct obj o;
	struct collectible c;

	/* volatile config file, not backed up on disk */
	struct config_file *volatile_config;

	unsigned char platform; /* slave's platform */
	unsigned long long int rev; /* slave's revision number */
	
	char ready;
	struct slave_ctx *slave;			/* NULL for not-ready slaves */

	/* io things should not be accessed by lua */
	io_packet_type last_command;		/* last packet type received */
	struct io_context io;				/* secure communication context */
	struct collection *group;

	unsigned long long int timestamp;	/* connect time */
	unsigned long long int statstime;	/*  */

	/* utility information */
	unsigned long long int diskfree;
	unsigned long long int disktotal;

	/* difference of time between the the slave's time and the master's time */
	signed long long int timediff;

	/* operation tracking */
	struct collection *asynch_queries; /* array of to-send slave_asynch_command structures */
	struct collection *asynch_response; /* array of sent slave_asynch_command structures */
	unsigned long long int asynchtime; /* timestamp of the last asynch command sent */
	unsigned long long int lagtime; /* global lag time of the slave (actually this is the difference
											of time between asynchtime and the last query received) */

	struct collection *available_files; /* collection of struct vfs_element : files available from that slave */
	struct collection *xfers; /* collection of struct ftpd_client_ctx : currently xfering clients */
	
	/* tracking for mirror operations */
	struct collection *mirror_from; /* outgoing */
	struct collection *mirror_to; /* incoming */
} __attribute__((packed));

typedef struct slave_ctx slave_ctx;
struct slave_ctx {
	struct obj o;
	struct collectible c;
	
	char *name; /* of the slave */

	struct config_file *config;

	unsigned long long int lastonline;

	/* virtual root for this slave. */
	struct vfs_element *vroot;

	unsigned long long int fileslog_timestamp; /* last files dump timestamp */
	char *fileslog; /* on-disk fileslog filename */

	struct collection *offline_files; /* files that are currently offline from that slave */
	char *deletelog; /* on-disk deletelog filename */

	struct slave_connection *cnx; /* NULL if the slave is not connected */

	struct collection *sections; /* collection of vfs_section added to this slave */
} __attribute__((packed));

struct master_hello_data {
	unsigned int use_encryption;
	unsigned int use_compression;
	unsigned int compression_threshold;
} __attribute__((packed));

unsigned long long int slave_files_size(struct slave_connection *cnx);
const char *slave_address(struct slave_connection *cnx);

void slave_connection_destroy(struct slave_connection *cnx);

/* exported apis */
struct slave_ctx *slave_new(const char *name);
struct slave_ctx *slave_get(const char *name);
struct slave_ctx *slave_load(const char *filename);

unsigned int slave_dump(struct slave_ctx *slave);
unsigned int slave_delete(struct slave_ctx *slave);

int slave_set_virtual_root(struct slave_ctx *slave, struct vfs_element *vroot);

unsigned int slave_offline_delete(struct slave_ctx *slave, struct vfs_element *file, int log);
unsigned int slave_mark_online_from(struct slave_connection *cnx, struct vfs_element *file);
unsigned int slave_mark_offline_from(struct slave_ctx *slave, struct vfs_element *file);

unsigned int slave_delete_file(struct slave_connection *cnx, struct vfs_element *element);

unsigned int slave_usage_from(struct slave_connection *cnx, struct vfs_element *element);

int slaves_init();
void slaves_free();

/* Must be called periodically */
void slaves_dump_fileslog();

#endif /* __SLAVES_H */
