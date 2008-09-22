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

#include <zlib.h>

#include "socket.h"
#include "io.h"
#include "time.h"
#include "constants.h"
#include "logging.h"
#include "packet.h"
//#include "encap.h"

/* these functions implement the layer between the
	packets and the data exchanged by master and slave.
	
	this layer provides:

		//encapsulation: redirect traffic thru a xftpd-proxy server
		encryption: encrypt and decrypt traffic silently
		compression: compress and decompress traffic silently
*/

/* send the packet on the socket */
/* NOTE that the packet may be reallocated in memory during this process */
unsigned int io_write_packet(struct io_context *io, struct packet **p) {
	unsigned int encrypted_size;
	unsigned char *encrypted;
	unsigned long long int n;
	unsigned long len;
	char *compressed;

	/* IMPORTANT: compression before encryption give better results */
	/* realloc the packet and compress it, if needed */
	if(((io->flags & IO_FLAGS_COMPRESSED) == IO_FLAGS_COMPRESSED) && ((*p)->size >= io->compression_threshold)) {
		struct packet *ep; /* encrypted packet */

		n = (*p)->size;
		n *= 1001;
		n /= 1000;
		n += 13;

		len = (unsigned long)n;
		compressed = malloc(len+4);
		if(!compressed) {
			IO_DBG("Memory error");
			return 0;
		}

		if(compress2((unsigned char *)&compressed[4], &len, (void*)(*p), (*p)->size, Z_BEST_COMPRESSION) != Z_OK) {
			IO_DBG("ZLib compress2() error: psize: %u, n: " LLU "", (*p)->size, n);
			free(compressed);
			return 0;
		}

		*(unsigned int *)compressed = (*p)->size;

		ep = packet_new((*p)->uid, IO_COMPRESSED, compressed, (len+4));
		free(compressed);
		if(!ep) {
			IO_DBG("Could not create IO_COMPRESSED packet");
			return 0;
		}

		free(*p);
		*p = ep;
	}

	/* realloc the packet and encrypt it, if needed */
	if(((io->flags & IO_FLAGS_ENCRYPTED) == IO_FLAGS_ENCRYPTED)) {
		struct packet *ep; /* encrypted packet */

		/* encrypt the whole packet with the local blowfish key */
		encrypted = crypto_cipher_encrypt(&io->lbf, (*p), (*p)->size, &encrypted_size, 1);
		if(!encrypted) {
			IO_DBG("Could not encrypt packet");
			return 0;
		}
		
		ep = packet_new((*p)->uid, IO_ENCRYPTED, encrypted, encrypted_size);
		free(encrypted);
		if(!ep) {
			IO_DBG("Could not create IO_ENCRYPTED packet");
			return 0;
		}

		free(*p);
		*p = ep;
	}	

	return send(io->fd, (void*)(*p), (*p)->size, 0);
}

unsigned int io_write_data(struct io_context *io, unsigned long long int uid, unsigned int type, void *buffer, unsigned int length) {
	struct packet *p;
	unsigned int ret;
	
	p = packet_new(uid, type, buffer, length);
	if(!p) {
		IO_DBG("Could not create new packet");
		return 0;
	}

	ret = io_write_packet(io, &p);

	if(ret != p->size) {
		IO_DBG("Could not write entire packet, only %d bytes on %u (%u)",
			ret, p->size, (int)WSAGetLastError());
		free(p);
		return 0;
	}
	free(p);

	return 1;
}

/* read and return a packet from the socket
	return 1 on success, 0 on error. note that
	the function may return 1 and not return a
	packet in *p, this is the case when the whole
	packet was not ready to be read right away.
	however this function should only be called
	when there is data to be read on the socket. */
unsigned int io_read_packet(struct io_context *io, struct packet **p, unsigned int timeout) {
	unsigned int ret;

	*p = NULL;

	if(!io->p) {
		/* update the timestamp because this is our first try */
		io->timestamp = time_now();
	}

	ret = packet_read(io->fd, &io->p, &io->filled_size);
	if(!ret) {
		if(!io->p) {
			io->filled_size = 0;
			io->timestamp = 0;
			return 0;
		}

		if(timeout != -1) {
			if(timer(io->timestamp) > timeout) {
				/* operation timed out! */
				free(io->p);
				io->p = NULL;
				io->filled_size = 0;
				io->timestamp = 0;
				return 0;
			}
		}

		return 1;
	}

	if(!io->p) {
		/* not enough data for a packet */
		return 1;
	}

	/* no error, packet has been retreived */
	*p = io->p;
	io->p = NULL;
	io->filled_size = 0;
	io->timestamp = 0;

	while(((*p)->type == IO_ENCRYPTED) || ((*p)->type == IO_COMPRESSED)) {
		switch((*p)->type) {
		case IO_ENCRYPTED:
			{
				/* decrypt this packet's data field, we should have a valid new packet inside */
				unsigned int unencrypted_size;
				struct packet *ep;

				/* decrypt the packet with the remote blowfish key */
				ep = (void*)crypto_cipher_encrypt(&io->rbf, &(*p)->data[0], ((*p)->size - sizeof(struct packet)), &unencrypted_size, 0);

				/* destroy the encrypted packet */
				free(*p);
				*p = NULL;

				if(!ep) {
					IO_DBG("Could not decrypt packet");
					return 0;
				}

				if((unencrypted_size < sizeof(struct packet)) || (ep->size != unencrypted_size)) {
					IO_DBG("Decrypted packet is not valid");
					free(ep);
					return 0;
				}

				/* return the unencrypted packet in *p */
				*p = ep;

				break;
			}
		case IO_COMPRESSED:
			{
				/* uncompress this packet's data field, we should have a valid new packet inside */
				unsigned int uncompressed_size;
				unsigned long i;
				struct packet *ep;

				uncompressed_size = *(unsigned int *)(&((*p)->data[0]));

				ep = malloc(uncompressed_size);
				if(!ep) {
					IO_DBG("Memory error");
					free(*p);
					*p = NULL;
					return 0;
				}

				i = uncompressed_size;
				if(uncompress((void*)ep, &i, &(*p)->data[4], (*p)->size - sizeof(struct packet) - 4) != Z_OK) {
					IO_DBG("could not uncompress the data");
					free(ep);
					return 0;
				}

				/*{
					unsigned int data_size;
					double ratio;

					data_size = ((*p)->size - sizeof(struct packet) - 4);

					ratio = (signed int)(i - data_size);
					ratio /= i;
					ratio *= 100;

					IO_DBG("Compression ratio: %.2f%%", ratio);
				}*/

				free(*p);
				*p = NULL;

				if((i < sizeof(struct packet)) || (ep->size != i)) {
					IO_DBG("Uncompressed data is not valid");
					free(ep);
					return 0;
				}

				*p = ep;
				
				break;
			}
		}
	}

	return 1;
}
