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

#ifndef __PROXY_H
#define __PROXY_H

#include "constants.h"

#include "debug.h"
#if defined(DEBUG_PROXY)
# define PROXY_DBG(format, arg...) { _DEBUG_CONSOLE(format, ##arg) _DEBUG_FILE(format, ##arg) }
#else
# define PROXY_DBG(format, arg...)
#endif

#include "signal.h"
#include "packet.h"

enum {
	PROXY_LISTEN,
	PROXY_LISTENING,
	PROXY_CONNECT,
	PROXY_CONNECTED,
};

struct proxy_connect {
	unsigned int ip;
	unsigned short port;
} __attribute__((packed));

struct proxy_listen {
	unsigned short port;
} __attribute__((packed));

/* the *server* tells the ip back to the client because proxies may be chained */
struct proxy_listening {
	unsigned int ip;
} __attribute__((packed));

struct proxy_data {
	unsigned int connected;
	unsigned int ready;
	int fd;

	int shutdown;

	char *buffer;
	unsigned int filledsize;
	unsigned int maxsize;
} __attribute__((packed));

struct proxy_connection {
	struct obj o;
	struct collectible c;
	
	struct signal_callback *rd_packet_cb;

	struct packet *p;
	unsigned int filledsize;

	struct collection *group;

	struct proxy_data in;
	struct proxy_data out;
} __attribute__((packed));

#endif /* __PROXY_H */

