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

#ifndef __FTPD_H
#define __FTPD_H

#include "constants.h"

#include "collection.h"
#include "vfs.h"
#include "users.h"
#include "secure.h"

#include "debug.h"
#if defined(DEBUG_FTPD)
# define FTPD_DBG(format, arg...) { _DEBUG_CONSOLE(format, ##arg) _DEBUG_FILE(format, ##arg) }
#else
# define FTPD_DBG(format, arg...)
#endif

#if defined(DEBUG_FTPD_DIALOG)
# define FTPD_DIALOG_DBG(format, arg...) { _DEBUG_CONSOLE(format, ##arg) _DEBUG_FILE(format, ##arg) }
#else
# define FTPD_DIALOG_DBG(format, arg...)
#endif

extern struct collection *clients;

/* ssl certificate */
extern X509 *ftpd_certificate_file;

/* ssl certificate's key */
extern EVP_PKEY *ftpd_certificate_key;

typedef enum {
	/* rfc-959 commands */
	CMD_USERNAME,
	CMD_PASSWORD,
	CMD_SYSTEM,
	CMD_CLIENT,
	CMD_PRINT_WORKING_DIRECTORY,
	CMD_CHANGE_WORKING_DIRECTORY,
	CMD_ACCOUNT,
	CMD_CHANGE_TO_PARENT_DIRECTORY,
	CMD_STRUCTURE_MOUNT,
	CMD_REINITIALIZE,
	CMD_DATA_PORT,
	CMD_PASSIVE,
	CMD_REPRESENTATION_TYPE,
	CMD_FILE_STRUCTURE,
	CMD_TRANSFER_MODE,
	CMD_RETRIEVE,
	CMD_STORE,
	CMD_STORE_UNIQUE,
	CMD_APPEND,
	CMD_ALLOCATE,
	CMD_RESTART,
	CMD_RENAME_FROM,
	CMD_RENAME_TO,
	CMD_ABORT,
	CMD_BROKEN_ABORT, /* "ÿôÿòÿABOR" sent by FlashFXP ... */
	CMD_DELETE,
	CMD_REMOVE_DIRECTORY,
	CMD_MAKE_DIRECTORY,
	CMD_LIST,
	CMD_NAME_LIST,
	CMD_SITE,
	CMD_STATUS,
	CMD_HELP,
	CMD_NO_OPERATION,
	CMD_QUIT,

	/* rfc-xxxx commands */
	CMD_FEATURES,

	/* mlst draft: http://www.ietf.org/internet-drafts/draft-ietf-ftpext-mlst-16.txt */
	CMD_SIZE_OF_FILE,

	/* distributed pasv: http://www.drftpd.org/index.php/Distributed_PASV */
	CMD_PRE_TRANSFER,
	
	/* auth ftp ssl draft: http://curl.haxx.se/rfc/draft-murray-auth-ftp-ssl-16.txt */
	CMD_AUTHENTICATE, /* AUTH SSL - AUTH TLS - AUTH TLS-C */
	CMD_PROTECTION, /* PROT */
	CMD_PROTECTION_BUFFER_SIZE, /* PBSZ */
	CMD_CLEAR_COMMAND_CHANNEL, /* CCC */
	
	/* http://www.raidenftpd.com/kb/kb000000037.htm */
	CMD_SET_SECURE_CLIENT_NEGOTIATION,

	/* other commands */
	CMD_NONE,
	CMD_UNKNOWN
} ftpd_command;

typedef enum {
	FTPD_TYPE_ASCII,
	FTPD_TYPE_IMAGE
} ftpd_type;

typedef enum {
	FTPD_STRUCTURE_FILE,
	FTPD_STRUCTURE_RECORD/*,
	FTPD_STRUCTURE_PAGE*/
} ftpd_structure;

typedef enum {
	FTPD_MODE_STREAM,
	FTPD_MODE_BLOCK,
	FTPD_MODE_COMPRESSED
} ftpd_mode;

typedef enum {
	FTPD_PROTECTION_PRIVATE,
	FTPD_PROTECTION_CLEAR,
} ftpd_protection;

typedef enum {
	FTPD_AUTH_NONE,
	FTPD_AUTH_SSL_OR_TLS,
} ftpd_auth;

struct ftpd_collectible_line {
	struct obj o;
	struct collectible c;
	
	char line[];
} __attribute__((packed));

struct ftpd_collectible_line *ftpd_line_new(char *line);
void ftpd_line_destroy(struct ftpd_collectible_line *l);

struct slave_listen_request {
	unsigned long long int xfer_uid; /* shared with the transfer request */
} __attribute__((packed));

struct slave_transfer_request {
	unsigned long long int xfer_uid; /* shared with the listen request */
	
	unsigned int ip; /* needed for active connections */
	unsigned short port; /* needed for active connections */
	char upload; /* 0 on downloads, 1 on uploads */
	char passive; /* 0 for active transfers, 1 for passive transfers */
	unsigned long long int restart; /* restart point */
	
	char use_secure; /* use ssl for data connection ? */
	char secure_server; /* is the slave the server end for ssl/tls negotiation ? */
	
	char filename[1];
} __attribute__((packed));

typedef struct xfer_ctx client_xfer;
struct xfer_ctx {
	unsigned long long int uid; /* unique id for this transfer, shared with the slave */
	unsigned long long int timestamp;

	unsigned long long int last_alive; /* last time it was reported in the stats */

	unsigned long long int restart; /* tells where to restart the RETR */

	char upload; /* 0 for download */
	//unsigned int busy; /* 1 while the slave is busy uploading/downloading */

	/* reference to the vfs element we are operating on */
	struct vfs_element *element;
	unsigned int checksum; /* checksum of the transfer. this is valid once
							the transfer is complete for both upload & download */

	/* reference the slave we are using for transfer */
	struct slave_connection *cnx;

	/* currently assigned asynch command */
	struct slave_asynch_command *cmd;
	unsigned long long int xfered; /* current ammount of data transfered, updated by stats.c */
} __attribute__((packed));

struct list_ctx {
	int fd;						/* connection socket */
	struct collection *group;

	struct collection *data;	/* collection of strings */
		
	char *resume_buffer;
	unsigned int resume_length;
	
	struct secure_ctx secure;
} __attribute__((packed));

typedef struct ftpd_client_ctx ftpd_client;
struct ftpd_client_ctx {
	struct obj o;
	struct collectible c;

	unsigned long long int timestamp; /* time at wich the client connected */
	unsigned long long int last_timestamp; /* time at wich the last command was sent */
	char connected;

	/* volatile config file, not backed up on disk */
	struct config_file *volatile_config;

	struct collection *group;
	int fd;						/* communication socket */

	char username[32];			/* username used to authenticate */
	struct user_ctx *user;		/* structure referencing the user for this connection */
	char logged;				/* true if the user is authenticated */

	struct vfs_element *working_directory;/* directory currently browsed by the user */
	
	ftpd_command last_command;	/* last command sent by the user */

	unsigned int buffersize;	/* max size */
	unsigned int filledsize;	/* filled length of the buffer */
	char *iobuf;				/* working buffer for the control socket */
	char readchr;				/* OpenSSL needs read/write to be retried
									with the exact same parameters each time,
									so we use this char and move it to the
									bigger buffer. please rewrite it, it's
									all but efficient. */

	struct collection *messages; /* messages to be sent to the client */

	/* data connection stuff */
	ftpd_type type;				/* data representation type */
	ftpd_structure structure;	/* data structure type */
	ftpd_mode mode;				/* data transfer mode */
	ftpd_auth auth;				/* authentication type */
	ftpd_protection protection;	/* data protection */
	
	int secure_server;
	
	int ssl_waiting;			/* if this is set, the next time the message queue is 
									empty a ssl negotiation will take place */

	char *resume_buffer;
	unsigned int resume_length;
	
	struct secure_ctx secure;
	
	char slave_xfer;			/* PRET has been issued for RETR/STOR */
	char passive;				/* passive or active socket */
	char ready;					/* ready to transfer or not */

	unsigned int ip;			/* ip for active socket */
	unsigned short port;		/* port for active socket */

	struct list_ctx data_ctx;	/* tracking for master transfers LIST/NLST */
	struct xfer_ctx xfer;		/* tracking for slave transfers:  */

} __attribute__((packed));

struct xfer_ctx *ftpd_lua_client_to_xfer(struct ftpd_client_ctx *ctx);

unsigned int ftpd_inject_listing(struct ftpd_client_ctx *ctx, char type, const char *name, const char *owner);
unsigned int ftpd_inject_symlink(struct ftpd_client_ctx *ctx, const char *target, const char *name, const char *owner);

unsigned int ftpd_wipe(struct vfs_element *element);
unsigned int ftpd_wipe_from(struct vfs_element *element, struct slave_ctx *slave);

unsigned long long int ftpd_next_xfer();

struct slave_transfer_request *ftpd_transfer(
	unsigned long long int uid,
	struct vfs_element *root,
	struct vfs_element *file,
	unsigned int ip,
	unsigned short port,
	char passive,
	char upload,
	unsigned long long int restart,
	unsigned int *length
);

int ftpd_init();
void ftpd_free();
int ftpd_reload();

ipaddress client_ipaddress(struct ftpd_client_ctx *ctx);
void ftpd_client_cleanup_data_connection(struct ftpd_client_ctx *ctx);

void ftpd_client_destroy(struct ftpd_client_ctx *client);

/* send a message to a client */
unsigned int ftpd_lua_message(struct ftpd_client_ctx *ctx, const char *msg);
unsigned int ftpd_message(struct ftpd_client_ctx *ctx, char *format, ...);

#endif /* __FTPD_H */
