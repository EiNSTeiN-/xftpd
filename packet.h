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

#ifndef __PACKET_H
#define __PACKET_H

#include "constants.h"

#ifndef NO_FTPD_DEBUG
//#  define DEBUG_PACKET
#endif

#ifdef DEBUG_PACKET
# ifdef FTPD_DEBUG_TO_CONSOLE
#  define PACKET_DBG(format, arg...) printf("["__FILE__ ":\t\t%d ]\t" format "\n", __LINE__, ##arg)
# else
#  define PACKET_DBG(format, arg...) logging_write("debug.log", "["__FILE__ ":\t\t%d ]\t" format "\n", __LINE__, ##arg)
# endif
#else
#  define PACKET_DBG(format, arg...)
#endif

#include <windows.h>

#include "socket.h"

struct packet {
	unsigned int size; /* size of the whole packet */
	unsigned char type;	 /* packet header */
	unsigned long long int uid; /* unique id of the packet: the slave must reply with the same id */
	unsigned char data[];
} __attribute__((packed));

/* this function may return 0 with a non-null *p if the whole packet could not be read.
	in this case, the function must be called again to complete the operation.
	if the function return 0 with a null *p, then the function failed. */
unsigned int packet_read(int s, struct packet **p, unsigned int *filledsize);
struct packet *packet_new(unsigned long long int uid, unsigned int type, void *buffer, unsigned int length);

#endif /* __PACKET_H */
