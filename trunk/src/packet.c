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

#ifdef WIN32
#include <windows.h>
#endif

#include <stdio.h>

#include "socket.h"
#include "packet.h"
#include "time.h"

/*
	Read and return a packet from the socket
	return 1 on success, 0 on error.
	This function may return 1 and not return a
	packet in *p in the case the size to be read was
	not at least sizeof(struct packet) big. This
	function may also return 0 but with a packet
	in *p, in wich case the whole packet could not
	be read right away. note that this function should
	only be called when there is data to be read
	on the socket.
*/
unsigned int packet_read(int s, struct packet **p, unsigned int *filledsize) {
	unsigned int size = 0;
	int read;
	unsigned long argp = 0;

	if(!filledsize) return 0;
	
	/* get the length of data available to be read */
	/*if(ioctlsocket(s, FIONREAD, &argp) != 0) {
		PACKET_DBG("" LLU "\t ioctlsocket failed", time_now());
		if(*p) {
			free(*p);
			*p = NULL;
		}
		*filledsize = 0;

		return 0;
	}*/
	
	argp = socket_avail(s);
	if(!argp) {
		/* if the POLLRDNORM event has been raised, there's data to be read
			so if there's no data in the input buffer, it must be that the
			server disconnected us */
			PACKET_DBG("" LLU "\t Disconnected from peer", time_now());
		if(*p) {
			free(*p);
			*p = NULL;
		}
		*filledsize = 0;
		return 0;
	}

	PACKET_DBG("" LLU "\t %u bytes ready right now", time_now(), (int)argp);

	if(*p) {
		/* there's a packet being read. */
		PACKET_DBG("" LLU "\t resuming at %u", time_now(), (*filledsize));

		/* get the size remaining to be read */
		size = ((*p)->size - (*filledsize));
		if(argp > size) argp = size;

		size = recv(s, (((char*)(*p))+(*filledsize)), argp, 0);

		if((int)size <= 0) {
			PACKET_DBG("" LLU "\t size != argp, %u != %u", time_now(), size, (int)argp);

			/* no data could be read */
			if(*p) {
				free(*p);
				*p = NULL;
			}
			*filledsize = 0;
			return 0;
		}

		(*filledsize) += size;

		if((*filledsize) == (*p)->size) {
			PACKET_DBG("" LLU "\t completed the packet !", time_now());
			return 1;
		}
		
		PACKET_DBG("" LLU "\t receival should be resumed !", time_now());
		return 0;
	}

	*filledsize = 0;

	/* make sure there's at least a packet to be read */
	if(argp < sizeof(struct packet)) {
		PACKET_DBG("" LLU "\t not yet a packet to be read !", time_now());
		/* there's not yet a packet to be read. */
		return 1;
	}

	/* start reading the packet */
	read = recv(s, (void*)&size, sizeof(size), 0);
	if(read != sizeof(size)) {
		PACKET_DBG("" LLU "\t can't get this packet size !", time_now());
		return 0;
	}

	PACKET_DBG("" LLU "\t size of the packet being read: %u", time_now(), size);

	/* attempt to send a packet with its size less than a packet */
	if(size < sizeof(struct packet)) {
		PACKET_DBG("" LLU "\t sending a packet less than a packet size !", time_now());
		return 0;
	}

	/* alloc the new packet */
	(*p) = malloc(size);
	if(!(*p)) {
		PACKET_DBG("" LLU "\t memory error !", time_now());
		return 0;
	}
	memset(*p, 0, size);

	(*p)->size = size;
	if(argp > size) argp = size;

	/* read what can be read */
	read = recv(s, (((char*)(*p))+sizeof(size)), (argp - sizeof(size)), 0);
	if(!read || (read < (sizeof(struct packet) - sizeof(size)))) {
		PACKET_DBG("" LLU "\t can't get packet body !", time_now());
		free(*p);
		(*p) = NULL;
		return 0;
	}

	PACKET_DBG("" LLU "\t %u bytes have been read !", time_now(), read);

	/* finished reading the packet ? */
	if((read + sizeof(size)) == (*p)->size) {
		PACKET_DBG("" LLU "\t packet entirely read !", time_now());
		return 1;
	}

	(*filledsize) = (read + sizeof(size));
	//io->timestamp = time_now();
	PACKET_DBG("" LLU "\t packet should be resumed (1) !", time_now());

	return 0;
}

struct packet *packet_new(unsigned long long int uid, unsigned int type, void *buffer, unsigned int length) {
	struct packet *p;

	if(!buffer && length) {
		PACKET_DBG("Parameter error");
		return 0;
	}

	p = malloc(length + sizeof(struct packet));
	if(!p) {
		PACKET_DBG("Memory error");
		return 0;
	}

	p->size = length + sizeof(struct packet);
	p->uid = uid;
	p->type = type;

	if(buffer)
		memcpy(&p->data[0], buffer, length);

	return p;
}
