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

#ifndef __EVENTS_H
#define __EVENTS_H

#include "constants.h"
#include "luainit.h"

#include "debug.h"
#if defined(DEBUG_EVENTS)
# define EVENTS_DBG(format, arg...) { _DEBUG_CONSOLE(format, ##arg) _DEBUG_FILE(format, ##arg) }
#else
# define EVENTS_DBG(format, arg...)
#endif

#if defined(DEBUG_EVENTS_CALLS)
# define EVENTS_CALLS_DBG(format, arg...) { _DEBUG_CONSOLE(format, ##arg) _DEBUG_FILE(format, ##arg) }
#else
# define EVENTS_CALLS_DBG(format, arg...)
#endif

#include "ftpd.h"
#include "slaves.h"
#include "mirror.h"
#include "scripts.h"

int events_init();
void events_free();
int events_clear();

typedef struct event_parameter event_parameter;
struct event_parameter {
	void *ptr; // actual parameter
	char *type; // parameter type name
} __attribute__((packed));

struct event_object {
	unsigned int param_count;
	struct event_parameter *params;
	union {
		int success;
		void *ptr;
	} ret;
} __attribute__((packed));

struct event_ctx {
	struct obj o;
	struct collectible c;
	
	char *name;
	struct collection *callbacks; /* event_callback */
} __attribute__((packed));

struct event_callback {
	struct obj o;
	struct collectible c;
	
	struct script_ctx *script;
	
	int function_index; /* function index in the EVENTS_REFTABLE */
} __attribute__((packed));


struct event_ctx *event_get(const char *name);
struct event_ctx *event_create(const char *name);

//struct event_callback *event_get_callback(struct event_ctx *ctx, const char *function, int create);
struct event_callback *event_add_callback(struct event_ctx *ctx, struct script_ctx *script, int function_index);

struct signal_callback *event_signal_add(const char *name, int (*callback)(void *obj, void *param), void *param);
struct signal_ctx *event_signal_get(const char *name, int create);

int event_raise(char *name, unsigned int param_count, struct event_parameter *params);

unsigned int event_onReload();
unsigned int event_onPreReload();

/* user operations */
void event_onNewUser(struct user_ctx *user);
void event_onDeleteUser(struct user_ctx *user);

/* ftpd: client operations */
unsigned int event_onClientConnect(struct ftpd_client_ctx *client);
unsigned int event_onClientLoginSuccess(struct ftpd_client_ctx *client);
void event_onClientLoginFail(struct ftpd_client_ctx *client);
void event_onClientDisconnect(struct ftpd_client_ctx *client);

/* slave: connections operations */
unsigned int event_onSlaveConnect(struct slave_connection *cnx);
unsigned int event_onSlaveIdentSuccess(struct slave_connection *cnx);
unsigned int event_onSlaveIdentFail(struct slave_connection *cnx, struct slave_hello_data *hello);
void event_onSlaveDisconnect(struct slave_connection *cnx);

/* ftpd: transfer operations */
unsigned int event_onPreDownload(struct ftpd_client_ctx *client, struct vfs_element *file);
unsigned int event_onPreUpload(struct ftpd_client_ctx *client, struct vfs_element *file);
unsigned int event_onTransferSuccess(struct ftpd_client_ctx *client, struct xfer_ctx *xfer);
void event_onTransferFail(struct ftpd_client_ctx *client, struct xfer_ctx *xfer);

/* ftpd: file & directory vfs */
unsigned int event_onPreDelete(struct ftpd_client_ctx *client, struct vfs_element *file);
void event_onDelete(struct ftpd_client_ctx *client, struct vfs_element *file);

unsigned int event_onPreMakeDir(struct ftpd_client_ctx *client, struct vfs_element *directory);
void event_onMakeDir(struct ftpd_client_ctx *client, struct vfs_element *directory);

unsigned int event_onPreRemoveDir(struct ftpd_client_ctx *client, struct vfs_element *directory);
void event_onRemoveDir(struct ftpd_client_ctx *client, struct vfs_element *directory);

unsigned int event_onPreChangeDir(struct ftpd_client_ctx *client, struct vfs_element *directory);
void event_onChangeDir(struct ftpd_client_ctx *client, struct vfs_element *directory);

/* ftpd: list operations */
void event_onList(struct ftpd_client_ctx *client, struct vfs_element *directory);

#endif /* __EVENTS_H */
