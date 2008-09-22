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

#ifndef __SECURE_H
#define __SECURE_H

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "constants.h"

#include "debug.h"
#if defined(DEBUG_SECURE)
# define SECURE_DBG(format, arg...) { _DEBUG_CONSOLE(format, ##arg) _DEBUG_FILE(format, ##arg) }
#else
# define SECURE_DBG(format, arg...)
#endif

#include "collection.h"

/*
	FIXME: the code for dropping the secure session
		while it's in use does not work properly.
		> in secure_drop()
	
*/

enum {
	SECURE_TYPE_CLIENT,
	SECURE_TYPE_SERVER,
	SECURE_TYPE_AUTO,
};

enum {
	SECURE_OPERATION_NONE,
	SECURE_OPERATION_HANDSHAKE,
	SECURE_OPERATION_RECV,
	SECURE_OPERATION_SEND,
	SECURE_OPERATION_SHUTDOWN_1,
	SECURE_OPERATION_SHUTDOWN_2, /* for bidirectional shutdowns */
};

enum {
	SECURE_STATUS_NONE,
	SECURE_STATUS_WANT_READ,
	SECURE_STATUS_WANT_WRITE,
	SECURE_STATUS_CONNECTED,
	SECURE_STATUS_ERROR,
};

/* this structure must be declared staticly and embedded into a parent structure */
struct secure_ctx {
	int type; /* one of client or server */
	int use_secure; /* 0 if ssl negotiation is not activated */
	
	SSL_CTX *ssl_ctx; /* configured once and reused to create the "ssl" structure below. */
	SSL *ssl; /*  */
	int lasterror; /* last error generated by the ssl context */
	
	int fd; /* socket */
	struct collection *group; /* higher level group: what we're listening for */
	struct collection *signals; /* lower level signals: what we're raising */
	
	//int negotiating; /* 0 if the ssl negotiation has not been done yet */
	int status; /* one of none, want read, want write, connected or error */
	int operation; /* one of none, handshake, recv or send */
	
	/* we could store them globally but it would take more time to look them up */
	struct signal_ctx *read_signal;
	struct signal_ctx *write_signal;
	struct signal_ctx *resume_recv_signal;
	struct signal_ctx *resume_send_signal;
	struct signal_ctx *connect_signal;
	struct signal_ctx *error_signal;
	
} __attribute__((packed));

/*  */
int secure_init();
void secure_free();
int secure_poll();

/*
	Setup a secure_ctx structure to its default values. The
	structure should be embedded staticly in an host structure.
	"secure_type" may be one of SECURE_CLIENT or SECURE_SERVER.
	After a call to this function, the caller may setup the
	"ssl_ctx" field of the secure_ctx structure to reflect its
	needs.

	This function should be called whenever the host structure
	is initialized.

	Once any of "socket-error", "socket-close" or "secure-error"
	event is raised, the secure connection's context must be
	destroyed.
*/
int secure_setup(struct secure_ctx *secure, int secure_type);
	
/*
	Change the secure type. It must be done before 
*/
int secure_type(struct secure_ctx *secure, int secure_type);

/*
	This function is to be called once the connection is fully
	established on the socket. After a call to this function,
	the caller may setup the "ssl" field of this structure to
	reflect its needs using OpenSSL's internal functions.

	This function DOES NOT initiate a ssl negitiation. A separate
	call to secure_negotiate must be issued for that.
	
	The "socket-read" and "socket-write" events are hooked by this
	function, so the caller must then hook the "secure-read" and
	"secure-write" events that are raised given any of the
	corresponsing actions are ready to be performed.
*/
int secure_connect(struct secure_ctx *secure, int fd);

/*
	Activate the secure connection. A full ssl negotiation
	will be initiated. A call to secure_connect must already
	have been issued. The negotiation fully support non-blocking
	sockets, so no garantee is given that the socket will be
	ready to be written to after this function returns. The
	caller should wait until the write event is raised.

	The "secure-connect" event will be raised when the secure
	connection is first established. The "secure-error" event
	will be raised if an unrecoverable error occurs during any
	ssl operation.
*/
int secure_negotiate(struct secure_ctx *secure);

/*
	Disactivate the secure connection. This function may be
	called at any time, if the secure connection is already
	established, then it is shut down. In this case the "ssl_ctx"
	field stay untouched but the "ssl" field is destroyed (freed
	and zeroed). After a call to this function, if the caller
	wish to reactivate the secure connection, a subsequent call
	to secure_negotiate must be issued.
	
	Both "secure-read" and "secure-write" events are still raised
	when a connection is dropped, but no other "secure-*" events
	are raised, and this secure layer behaves just like it did not
	exists.

	WARNING: does not work in the current implementation.
*/
int secure_drop(struct secure_ctx *secure);

/*
	Behaves like the standard recv() and send(). These wrappers
	must always be used instead of recv() and send().

	If the return value is -1 and *tryagain is set to a non-zero
	value, then the caller must wait for any of the two "secure-resume-recv"
	or "secure-resume-send" events to be raised and repeat the failed
	call. This happens only when the secure connection is active.
	Any send operation must be repeated with exactly the same
	parameters.

	In most of the cases -- but not in all -- the "secure-resume-recv/send"
	events can be assigned the same callbacks as the "secure-read/write"
	events.
*/
int secure_recv(struct secure_ctx *secure, char *buf, int len, int *tryagain);
int secure_send(struct secure_ctx *secure, const char *buf, int len, int *tryagain);

/* Add a socket signal that may be raised by this module */
struct signal_callback *secure_signal_add(struct secure_ctx *secure, struct collection *group, char *name, int (*callback)(void *obj, void *param), void *param);

/* Signal the closure of the underlying socket. */
int secure_close(struct secure_ctx *secure);

/* Clean up the secure_ctx structure. */
int secure_destroy(struct secure_ctx *secure);

#endif /* __SECURE_H */

