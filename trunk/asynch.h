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

#ifndef __ASYNCH_H
#define __ASYNCH_H

#include "constants.h"

#ifndef NO_FTPD_DEBUG
#  define DEBUG_ASYNCH
#endif

#ifdef DEBUG_ASYNCH
# ifdef FTPD_DEBUG_TO_CONSOLE
#  define ASYNCH_DBG(format, arg...) printf("["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg)
# else
#  define ASYNCH_DBG(format, arg...) logging_write("debug.log", "["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg)
# endif
#else
#  define ASYNCH_DBG(format, arg...)
#endif

#include "packet.h"

/* holds command data and response */
struct slave_asynch_command {
	unsigned long long int uid;
	struct obj o;
	
	struct slave_connection *cnx;

	/* tracking */
	unsigned int timeout; /* desired timeout for the response to arrive, in milliseconds */
	unsigned long long int send_time; /* time at wich the data has been sent */

	/* called when the response arrives, on timeout or on error */
	/* if the callback return zero the slave will get disconnected. */
	unsigned int (*reply_callback)(struct slave_connection *cnx, struct slave_asynch_command *cmd, struct packet *p);
	void *param; /* arbitrary data passed to the callback */

	/* describes the data to be sent */
	unsigned int command; /* IO command type */

	unsigned int data_length;
	char data[];
} __attribute__((packed));

unsigned int asynch_match(struct slave_connection *cnx, struct packet *p);

unsigned int asynch_destroy(struct slave_asynch_command *cmd, struct packet *p);

struct slave_asynch_command *asynch_new(
	struct slave_connection *cnx,
	unsigned char command,
	unsigned int timeout,
	unsigned char *data,
	unsigned int data_length,
	unsigned int (*reply_callback)(struct slave_connection *cnx, struct slave_asynch_command *cmd, struct packet *p),
	void *param
);

#endif /* __ASYNCH_H */
