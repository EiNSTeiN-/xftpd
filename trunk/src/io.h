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

#ifndef __IO_H
#define __IO_H

#include "constants.h"

#include "debug.h"
#if defined(DEBUG_IO)
# define IO_DBG(format, arg...) { _DEBUG_CONSOLE(format, ##arg) _DEBUG_FILE(format, ##arg) }
#else
# define IO_DBG(format, arg...)
#endif

#ifdef WIN32
#include <windows.h>
#endif

#include <openssl/rsa.h>
#include <openssl/blowfish.h>

#include "socket.h"
#include "crypto.h"
#include "packet.h"

/* TODO: move each of these in thier
	respective owner's .h file */
typedef enum {
	IO_NOTHING,

	/* slave connection dialog */
	IO_PUBLIC_KEY,
	IO_BLOWFISH_KEY,
	IO_HELLO,
	IO_FILE_LIST,

	/* data connection control */
	IO_FAILURE,

	IO_SLAVE_LISTEN,
	IO_SLAVE_LISTENING,

	IO_SLAVE_TRANSFER,
	IO_SLAVE_TRANSFERED,

	IO_DELETELOG,
	IO_DELETE,
	IO_DELETED,

	IO_SFV,
	IO_STATS,

	/* low-level operations */
	IO_ENCRYPTED,
	IO_COMPRESSED,
	IO_UNUSED_1, /* room for more low-level operations */
	IO_UNUSED_2,
	IO_UNUSED_3,
	IO_UNUSED_4,
	IO_UNUSED_5,

	/* ... */
	IO_SFVLOG,

	/* update query */
	IO_UPDATE,

	/* SSL stuff (x509 cert and associated pk) */
	IO_CERTIFICATE_x509,
	IO_CERTIFICATE_PKEY,
	
	IO_ERROR_FILE_OPEN,
	IO_ERROR_FILE_READWRITE,
	IO_ERROR_FILE_NOTFOUND,
	IO_ERROR_FILE_NODISK,
	IO_ERROR_CNX_CONNECT_TIMEOUT,
	IO_ERROR_CNX_WRITE_TIMEOUT,
	IO_ERROR_CNX_READ_TIMEOUT,
	IO_ERROR_CNX_ERROR,
	IO_ERROR_SSL_ERROR,
	IO_ERROR_PROXY_GARBAGE,
	
	/* room for more errors ... */
	IO_ERROR_UNUSED_1,
	IO_ERROR_UNUSED_2,
	IO_ERROR_UNUSED_3,
	IO_ERROR_UNUSED_4,
	IO_ERROR_UNUSED_5,
	IO_ERROR_UNUSED_6,
	
} io_packet_type;

#define IO_FLAGS_ENCRYPTED	0x1001
#define IO_FLAGS_COMPRESSED	0x1002

struct io_context {
	//struct encap_ctx *encap;
	int fd;

	unsigned int filled_size; /* filled size of the whole structure */
	struct packet *p; /* packet being read, if it was not completely available. */
	unsigned int timestamp; /* time at wich the packet started to be read */

	unsigned int flags;
	unsigned int compression_threshold;

	/* local */
	int lkey_set;
	struct keypair lkey;
	int lbf_set;
	BF_KEY lbf;

	/* remote */
	int rkey_set;
	struct keypair rkey;
	int rbf_set;
	BF_KEY rbf;
} __attribute__((packed));


unsigned int io_read_packet(struct io_context *io, struct packet **p, unsigned int timeout);
unsigned int io_write_packet(struct io_context *io, struct packet **p);
unsigned int io_write_data(struct io_context *io, unsigned long long int uid, unsigned int type, void *buffer, unsigned int length);

#endif /* __IO_H */
