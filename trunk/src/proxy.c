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

#include <windows.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "socket.h"
#include "proxy.h"
#include "collection.h"
#include "config.h"
#include "logging.h"
#include "constants.h"
#include "service.h"
#include "signal.h"
#include "packet.h"
#include "time.h"

/* 1 if the traffic is chained to another proxy */
static int chained = 0;
static unsigned int chain_ip = 0;
static unsigned short chain_port = 0;

/* listening port */
static unsigned int proxy_port = 0;

/* external ip override */
static unsigned int proxy_external_ip = 0;

/* traffic buffer size */
static unsigned int proxy_buffer_size = 0;

static struct collection *proxies;

static void proxy_obj_destroy(struct proxy_connection *proxy) {
	
	collectible_destroy(proxy);

	if(proxy->p) {
		free(proxy->p);
		proxy->p = NULL;
	}

	if(proxy->in.fd != -1) {
		close_socket(proxy->in.fd);
		socket_monitor_fd_closed(proxy->in.fd);
		proxy->in.fd = -1;
	}

	if(proxy->out.fd != -1) {
		close_socket(proxy->out.fd);
		socket_monitor_fd_closed(proxy->out.fd);
		proxy->out.fd = -1;
	}

	if(proxy->group) {
		signal_clear(proxy->group);
		collection_destroy(proxy->group);
		proxy->group = NULL;
	}

	free(proxy->in.buffer);
	free(proxy->out.buffer);
	
	free(proxy);

	return;
}

struct proxy_connection *proxy_new(int fd) {
	struct proxy_connection *proxy;

	proxy = malloc(sizeof(struct proxy_connection));
	if(!proxy) {
		PROXY_DBG("Memory error");
		return NULL;
	}
	
	obj_init(&proxy->o, proxy, (obj_f)proxy_obj_destroy);
	collectible_init(proxy);

	proxy->group = collection_new(C_CASCADE);
	proxy->rd_packet_cb = NULL;

	proxy->p = NULL;
	proxy->filledsize = 0;

	proxy->in.connected = 1;
	proxy->in.fd = fd;
	proxy->in.ready = 0;
	proxy->in.filledsize = 0;
	proxy->in.maxsize = proxy_buffer_size;
	proxy->in.buffer = malloc(proxy_buffer_size);
	proxy->in.shutdown = 0;

	proxy->out.connected = 0;
	proxy->out.fd = -1;
	proxy->out.ready = 0;
	proxy->out.filledsize = 0;
	proxy->out.maxsize = proxy_buffer_size;
	proxy->out.buffer = malloc(proxy_buffer_size);
	proxy->out.shutdown = 0;
	
	collection_add(proxies, proxy);

	return proxy;
}

void proxy_destroy(struct proxy_connection *proxy) {

	obj_destroy(&proxy->o);
	
	return;
}

int proxy_cleanup_side(struct proxy_connection *proxy, int fd) {

	/* we must delete any events on this socket */
	signal_clear_all_with_filter(proxy->group, (void *)fd);

	/* then we delete the actual socket monitor */
	close_socket(fd);
	socket_monitor_fd_closed(fd);

	if(proxy->in.fd == fd) {
		PROXY_DBG("Input side has closed.");
		proxy->in.fd = -1;
		
		proxy->out.shutdown = 1;

		if(!proxy->out.filledsize) {
			/* No more data to be sent AND we must shutdown */

			PROXY_DBG("No more data to send, shutting down now");
			
			make_socket_blocking(proxy->out.fd, 1);
			shutdown(proxy->out.fd, SD_SEND);
		} else {
			PROXY_DBG("Still %u bytes to send to the output side.", proxy->out.filledsize);
		}
	}
	else if(proxy->out.fd == fd) {
		PROXY_DBG("Output side has closed.");
		proxy->out.fd = -1;
		
		proxy->in.shutdown = 1;
		
		if(!proxy->in.filledsize) {
			/* No more data to be sent AND we must shutdown */

			PROXY_DBG("No more data to send, shutting down now");
			
			make_socket_blocking(proxy->in.fd, 1);
			shutdown(proxy->in.fd, SD_SEND);
		} else {
			PROXY_DBG("Still %u bytes to send to the input side.", proxy->in.filledsize);
		}
	}
	else {
		PROXY_DBG("INVALID side has closed.");
	}

	return 1;
}

int proxy_connection_error(int fd, struct proxy_connection *cnx) {

	PROXY_DBG("Connection error");
	proxy_destroy(cnx);

	return 1;
}

int proxy_connection_close(int fd, struct proxy_connection *cnx) {

	PROXY_DBG("Connection closed");
	
	/* Cleanup the closed side. */
	proxy_cleanup_side(cnx, fd);

	if(cnx->in.fd == -1 && cnx->out.fd == -1) {
		PROXY_DBG("(at:%I64u) Second side closed.", time_now());
		proxy_destroy(cnx);
	} else {
		PROXY_DBG("(at:%I64u) First side closed.", time_now());
	}

	return 1;
}

int proxy_connection_read(int fd, struct proxy_data *dst) {
	int avail, recvd;

	/* read data from 'fd' and put it in 'dst->buffer' */

	avail = socket_avail(fd);
	if(avail <= 0) {
		/* invalid available number of bytes */
		PROXY_DBG("No valid available size: shutting down socket");
		make_socket_blocking(fd, 1);
		shutdown(fd, SD_SEND);
		return 0;
	}

	if(avail > (dst->maxsize - dst->filledsize))
		avail = (dst->maxsize - dst->filledsize);

	if(!avail) {
		/* buffer is FULL: maybe we should increase it here ? */
		return 1;
	}

	recvd = recv(fd, &dst->buffer[dst->filledsize], avail, 0);
	if(recvd <= 0) {
		/* error while receiving */
		PROXY_DBG("Error while receiving: shutting down socket");
		make_socket_blocking(fd, 1);
		shutdown(fd, SD_SEND);
		return 0;
	}
	dst->filledsize += recvd;

	return 1;
}

int proxy_connection_write(int fd, struct proxy_data *src) {
	int sent;

	/* we take data from src->buffer and send it to 'fd' */

	if(!src->filledsize)
		return 1;

	sent = send(fd, src->buffer, src->filledsize, 0);
	if(sent <= 0) {
		/* no data sent ... */
		PROXY_DBG("DATA COULD NOT BE SENT: Connection error");

		make_socket_blocking(fd, 1);
		shutdown(fd, SD_SEND);
		
		src->filledsize = 0;
		return 0;
	}

	memmove(src->buffer, &src->buffer[sent], src->filledsize-sent);
	src->filledsize -= sent;

	if(!src->filledsize && src->shutdown) {
		/* No more data to be sent AND we must shutdown */

		PROXY_DBG("No more data to send, and we must shutdown");
		
		make_socket_blocking(fd, 1);
		shutdown(fd, SD_SEND);
	}

	return 1;
}

int proxy_connection_timeout(struct proxy_connection *cnx) {

	PROXY_DBG("Connection timed out!");
	proxy_destroy(cnx);

	return 1;
}

int proxy_connection_out_connect(int fd, struct proxy_connection *cnx) {

	if(cnx->p && (cnx->p->type == PROXY_LISTEN)) {

		/* if we were listening here, we need to switch to the accept socket */
		cnx->out.fd = accept(fd, NULL, 0);

		signal_clear_all_with_filter(cnx->group, (void *)fd);

		socket_monitor_fd_closed(fd);
		close_socket(fd);

		if(cnx->out.fd == -1) {
			/* should NEVER happen */
			PROXY_DBG("Could not accept the connection from out listening socket");
			proxy_destroy(cnx);
			return 0;
		}
		//socket_current++;

		/* enable linger so we'll perform hard abort on closesocket() */
		socket_linger(cnx->out.fd, 0);

		socket_monitor_new(cnx->out.fd, 1, 1);
		socket_monitor_signal_add(cnx->out.fd, cnx->group, "socket-error", (signal_f)proxy_connection_error, cnx);
		socket_monitor_signal_add(cnx->out.fd, cnx->group, "socket-close", (signal_f)proxy_connection_close, cnx);
	}

	/* out socket of chained proxy is ready */
	cnx->out.connected = 1;
	cnx->out.ready = 1;
	cnx->in.ready = 1;
	
	/* set the correct buffer size ... */
	socket_set_max_read(cnx->out.fd, proxy_buffer_size);
	socket_set_max_write(cnx->out.fd, proxy_buffer_size);

	/* read[in to out] */
	socket_monitor_signal_add(cnx->in.fd, cnx->group, "socket-read", (signal_f)proxy_connection_read, &cnx->out);

	/* read[out to in] */
	socket_monitor_signal_add(cnx->out.fd, cnx->group, "socket-read", (signal_f)proxy_connection_read, &cnx->in);

	if(!cnx->p || (cnx->p->type != PROXY_LISTEN)) {
		/* write[in to in] */
		socket_monitor_signal_add(cnx->in.fd, cnx->group, "socket-write", (signal_f)proxy_connection_write, &cnx->in);
	}

	/* write[out to out] */
	socket_monitor_signal_add(cnx->out.fd, cnx->group, "socket-write", (signal_f)proxy_connection_write, &cnx->out);

	if(cnx->p && (cnx->p->type == PROXY_CONNECT)) {
		/* enqueue the "connected" packet to the "in" queue */
		struct packet *p;

		p = packet_new(cnx->p->uid, PROXY_CONNECTED, NULL, 0);
		if(!p) {
			PROXY_DBG("Memory error");
			proxy_destroy(cnx);
			return 0;
		}

		memcpy(&cnx->in.buffer[cnx->in.filledsize], p, p->size);
		cnx->in.filledsize += p->size;
	}

	if(cnx->p) {
		free(cnx->p);
		cnx->p = NULL;
	}

	return 1;
}

int proxy_connection_read_packet(int fd, struct proxy_connection *cnx) {
	unsigned int ret;

	ret = packet_read(fd, &cnx->p, &cnx->filledsize);
	if(!ret) {
		if(!cnx->p) {
			/* should NEVER happen */
			proxy_destroy(cnx);
			return 0;
		} else {
			/* packet could not be entierly read */
			return 1;
		}
	}

	if(ret && !cnx->p) {
		/* there was not enough data yet */
		return 1;
	}

	/* packet FULLY read */
	switch(cnx->p->type) {
	case PROXY_LISTEN:
		{
			struct proxy_listen *listen = (struct proxy_listen *)&cnx->p->data;
			struct proxy_listening listening;
			struct packet *p;

			PROXY_DBG("Listen packet received!");

			cnx->out.fd = create_listening_socket(listen->port);
			if(cnx->out.fd == -1) {
				/* should NEVER happen */
				PROXY_DBG("Could not listen on port %u", listen->port);
				proxy_destroy(cnx);
				return 0;
			}

			{
				struct signal_callback *s;

				socket_monitor_new(cnx->out.fd, 0, 1);

				s = socket_monitor_signal_add(cnx->out.fd, cnx->group, "socket-connect", (signal_f)proxy_connection_out_connect, cnx);
				signal_timeout(s, PROXY_LISTEN_TIMEOUT, (timeout_f)proxy_connection_timeout, cnx);

				socket_monitor_signal_add(cnx->out.fd, cnx->group, "socket-error", (signal_f)proxy_connection_error, cnx);
				socket_monitor_signal_add(cnx->out.fd, cnx->group, "socket-close", (signal_f)proxy_connection_close, cnx);
			}

			listening.ip = proxy_external_ip ? proxy_external_ip : socket_local_address(cnx->in.fd);

			/* enqueue the "listening" packet to the "in" queue */
			p = packet_new(cnx->p->uid, PROXY_LISTENING, &listening, sizeof(listening));
			if(!p) {
				PROXY_DBG("Memory error");
				proxy_destroy(cnx);
				return 0;
			}

			memcpy(&cnx->in.buffer[cnx->in.filledsize], p, p->size);
			cnx->in.filledsize += p->size;

			/* register the write[in to in] callback because of the enqueued "listening" packet */
			socket_monitor_signal_add(cnx->in.fd, cnx->group, "socket-write", (signal_f)proxy_connection_write, &cnx->in);
			cnx->in.ready = 1;
		}
		break;
	case PROXY_CONNECT:
		{
			struct proxy_connect *connect = (struct proxy_connect *)&cnx->p->data;

			PROXY_DBG("Connect packet received!");

			cnx->out.fd = connect_to_ip_non_blocking(connect->ip, connect->port);
			if(cnx->out.fd == -1) {
				/* should NEVER happen */
				PROXY_DBG("Could not connect to requested ip/port");
				proxy_destroy(cnx);
				return 0;
			}

			socket_monitor_new(cnx->out.fd, 0, 0);
			socket_monitor_signal_add(cnx->out.fd, cnx->group, "socket-connect", (signal_f)proxy_connection_out_connect, cnx);
			socket_monitor_signal_add(cnx->out.fd, cnx->group, "socket-error", (signal_f)proxy_connection_error, cnx);
			socket_monitor_signal_add(cnx->out.fd, cnx->group, "socket-close", (signal_f)proxy_connection_close, cnx);
		}
		break;
	default:
		/* should NEVER happen */
		PROXY_DBG("Proxy sent garbage");
		proxy_destroy(cnx);
		return 0;
	}

	/* remove this very callback */
	signal_del(cnx->group, cnx->rd_packet_cb);
	cnx->rd_packet_cb = NULL;

	return 1;
}

int proxy_listening_connect(int fd, unsigned int *listening) {
	struct proxy_connection *cnx;
	int fd_in, fd_out;

	/* Connection detected on the main listening socket */
	fd_in = accept(fd, NULL, 0);
	if(fd_in == -1) {
		PROXY_DBG("Cannot accept incomming connection");
		return 1;
	}
	//socket_current++;

	/* enable linger so we'll perform hard abort on closesocket() */
	socket_linger(fd_in, 0);

	/* wrap a proxy_connection around the new socket */
	cnx = proxy_new(fd_in);
	if(!cnx) {
		PROXY_DBG("Memory error");
		return 1;
	}

	/* register events */
	socket_monitor_new(fd_in, 1, 1);

	if(chained) {
		/* open the outgoing connection right now if this proxy is chained */
		fd_out = connect_to_ip_non_blocking(chain_ip, chain_port);
		if(fd_out == -1) {
			PROXY_DBG("Cannot open outgoing connection for chaining");
			proxy_destroy(cnx);
			return 0;
		}

		cnx->out.fd = fd_out;

		socket_monitor_new(fd_out, 0, 0);
		socket_monitor_signal_add(fd_out, cnx->group, "socket-connect", (signal_f)proxy_connection_out_connect, cnx);
		socket_monitor_signal_add(fd_out, cnx->group, "socket-error", (signal_f)proxy_connection_error, cnx);
		socket_monitor_signal_add(fd_out, cnx->group, "socket-close", (signal_f)proxy_connection_close, cnx);
	} else {
		/*
			if the proxy is not chained, it means we have to handle a packet from the client
			wich will tell us where to connect and/or what to do.
		*/
		cnx->rd_packet_cb = socket_monitor_signal_add(fd_in, cnx->group, "socket-read", (signal_f)proxy_connection_read_packet, cnx);
	}

	/* set the correct buffer size ... */
	socket_set_max_read(fd_in, proxy_buffer_size);
	socket_set_max_write(fd_in, proxy_buffer_size);

	socket_monitor_signal_add(fd_in, cnx->group, "socket-error", (signal_f)proxy_connection_error, cnx);
	socket_monitor_signal_add(fd_in, cnx->group, "socket-close", (signal_f)proxy_connection_close, cnx);

	return 1;
}

int proxy_listening_error(int fd, unsigned int *listening) {

	PROXY_DBG("Listening socket died of an error.");
	*listening = 0;

	return 1;
}

int proxy_listening_close(int fd, unsigned int *listening) {

	PROXY_DBG("Listening socket closed gracefully.");
	*listening = 0;

	return 1;
}

void set_current_path() {
	char full_path[MAX_PATH];
	char *ptr;

	GetModuleFileName(NULL, full_path, sizeof(full_path));
	ptr = strchr(full_path, '\\');
	if(ptr) {
		while(strchr(ptr+1, '\\')) ptr = strchr(ptr+1, '\\');
		*(ptr+1) = 0;
	}
	_chdir(full_path);

	return;
}

#ifdef PROXY_WIN32_SERVICE
int win32_service_main() {
#else
int main(int argc, char* argv[]) {
#endif
	struct collection *listen_signals;
	int listen_fd = -1;

	unsigned int listening = 1;

	PROXY_DBG("Loading ...");

	set_current_path();

#ifdef PROXY_SILENT_CRASH
	SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
#endif

	socket_init();
	
	proxy_buffer_size = (unsigned short)config_raw_read_int(PROXY_CONFIG_FILE, "proxy.buffer-size", 0);
	if(!proxy_buffer_size) {
		PROXY_DBG("Cannot get proxy's buffer size, defaulting to 1 MB (proxy.buffer-size)");
		proxy_buffer_size = 1 * 1024 * 1024;
	}

	proxy_port = (unsigned short)config_raw_read_int(PROXY_CONFIG_FILE, "proxy.port", 0);
	if(!proxy_port) {
		PROXY_DBG("Cannot get local listening port (proxy.port)");
		return 1;
	}

	{
		char *addr;
		addr = config_raw_read(PROXY_CONFIG_FILE, "proxy.external-ip", NULL);
		if(addr) {
			unsigned int ip;

			ip = socket_addr(addr);

			if(ip && (ip != -1)) {
				proxy_external_ip = ip;
				PROXY_DBG("External ip is set to %s", addr);
			}

			free(addr);
		}
	}

	{
		char *p, *addr;
		p = config_raw_read(PROXY_CONFIG_FILE, "proxy.chain", NULL);
		if(p) {
			if(socket_split_addr(p, &addr, &chain_port)) {
				chain_ip = socket_addr(addr);
				if(chain_ip != -1) {
					chained = 1;
					PROXY_DBG("Chaining traffic to %s:%u", addr, chain_port);
				}
				free(addr);
			}
			free(p);
		}
	}

	listen_signals = collection_new(C_CASCADE);
	proxies = collection_new(C_CASCADE);

	listen_fd = create_listening_socket(proxy_port);
	if(listen_fd == -1) {
		PROXY_DBG("Could not listen on port %u", proxy_port);
		return 1;
	}

	socket_monitor_new(listen_fd, 0, 1);
	socket_monitor_signal_add(listen_fd, listen_signals, "socket-connect", (signal_f)proxy_listening_connect, &listening);
	socket_monitor_signal_add(listen_fd, listen_signals, "socket-error", (signal_f)proxy_listening_error, &listening);
	socket_monitor_signal_add(listen_fd, listen_signals, "socket-close", (signal_f)proxy_listening_close, &listening);

	while(listening) {

		//signal_poll();

		socket_poll();
		
		collection_cleanup_iterators();

		Sleep(PROXY_SLEEP_TIME);
	}

	PROXY_DBG("Proxy's main loop exited");

	signal_clear(listen_signals);
	collection_destroy(listen_signals);
	listen_signals = 0;

	close_socket(listen_fd);
	socket_monitor_fd_closed(listen_fd);

	/*while(collection_size(proxies)) {
		void *first = collection_first(proxies);
		proxy_destroy(first);
		collection_delete(proxies, first);
	}*/
	collection_destroy(proxies);
	
	socket_free();

	return 0;
}


#ifdef PROXY_WIN32_SERVICE
int main(int argc, char* argv[]) {
	char *name;
	char *displayname;
	char *desc;

	set_current_path();

	name = config_raw_read(PROXY_CONFIG_FILE, "proxy.service.name", "xFTPd-proxy");
	displayname = config_raw_read(PROXY_CONFIG_FILE, "proxy.service.displayname", "xFTPd-proxy");
	desc = config_raw_read(PROXY_CONFIG_FILE, "proxy.service.description", "xFTPd-proxy");

	// if we're a service, it means someone *else* will get the result
	if(service_start_and_call(name, displayname, desc, (func)&win32_service_main)) {
		return 1;
	}

	return 0;
}
#endif
