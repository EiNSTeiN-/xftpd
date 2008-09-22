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

#define FD_SETSIZE 512

#include <winsock2.h>
#include <windows.h>
#include <poll.h>
#include <stdio.h>

#include "socket.h"
#include "collection.h"
#include "time.h"
#include "signal.h"
#include "obj.h"

unsigned long long int socket_current = 0;

void socket_init()
{

	SOCKET_DBG("Socket set size is %u", FD_SETSIZE);

	WORD wVersionRequested = MAKEWORD(2, 2);
	WSADATA wsaData;

	WSAStartup(wVersionRequested, &wsaData);

	return;
}

void socket_free()
{
	WSACleanup();
	
	return;
}

/* take addr wich is something like localhost:3456 and return localhost in host and 3456 int port */
unsigned int socket_split_addr(const char *addr, char **host, unsigned short *port) {
	char *s, *p;

	if(host) *host = NULL;
	if(port) *port = 0;

	s = strdup(addr);
	if(!s) return 0;

	p = strchr(s, ':');
	if(!p) {
		free(s);
		return 0;
	}

	*p = 0;
	p++;

	if(port)
		*port = atoi(p);

	if(host)
		*host = s;
	else
		free(s);

	return 1;
}

unsigned int socket_peer_address(int s) {
	struct sockaddr_in addr;
	unsigned int len = sizeof(struct sockaddr_in);

	if(getpeername(s, (struct sockaddr *)&addr, &len))
		return 0;

	return addr.sin_addr.S_un.S_addr;
}

const char *socket_formated_peer_address(int s) {
	struct sockaddr_in addr;
	unsigned int len = sizeof(struct sockaddr_in);

	if(getpeername(s, (struct sockaddr *)&addr, &len))
		return 0;

	return inet_ntoa((struct in_addr )addr.sin_addr);
}

unsigned int socket_local_address(int s) {
	struct sockaddr_in addr;
	unsigned int len = sizeof(struct sockaddr_in);

	if(getsockname(s, (struct sockaddr *)&addr, &len))
		return 0;

	return addr.sin_addr.S_un.S_addr;
}

int make_socket_blocking(int s, int blocking) {
	u_long opts;

	if(blocking == 0) opts = 1;
	else opts = 0;

	return (ioctlsocket(s, FIONBIO, &opts) == 0);
}

unsigned int socket_addr(const char *hostname) {
  LPHOSTENT host_entry;

  host_entry = gethostbyname(hostname); /* FIXME: use getaddrinfo */
  if(host_entry == NULL) return INVALID_SOCKET;
  
  return *((unsigned int*)*host_entry->h_addr_list);
}

int socket_linger(int fd, unsigned short timeout) {
	struct linger l;

	l.l_onoff = 1;
	l.l_linger = timeout;

	return setsockopt(fd, IPPROTO_TCP, SO_LINGER, (void*)&l, sizeof(l));
}

int connect_to_ip_non_blocking(unsigned int ip, short port) {
	SOCKADDR_IN saServer;
	int s;

	s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(s == INVALID_SOCKET) {
		SOCKET_DBG("socket() failed.");
		return INVALID_SOCKET;
	}
	make_socket_blocking(s, 0);
	saServer.sin_family = AF_INET;
	saServer.sin_addr.S_un.S_addr = ip;
	saServer.sin_port = htons(port);
	if((connect(s, (LPSOCKADDR)&saServer, sizeof(struct sockaddr)) == SOCKET_ERROR) &&
			(GetLastError() != WSAEWOULDBLOCK)) {
		SOCKET_DBG("non-blocking connect() failed.");
		closesocket(s);
		return INVALID_SOCKET;
	}

	/* enable TCP_NODELAY */
	/*{
		BOOL optval = 1;
		if(setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (void*)&optval, sizeof(optval))) {
			SOCKET_DBG("could not enable TCP_NODELAY.");
		}
	}*/

	/* enable SO_LINGER */
	socket_linger(s, 0);

	socket_current++;

	return s;
}

int connect_to_ip(unsigned int ip, short port) {
	int s;
	SOCKADDR_IN saServer;

	s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(s == INVALID_SOCKET) return INVALID_SOCKET;
	saServer.sin_family = AF_INET;
	saServer.sin_addr.S_un.S_addr = ip;
	saServer.sin_port = htons(port);
	if(connect(s, (LPSOCKADDR)&saServer, sizeof(struct sockaddr)) == SOCKET_ERROR) {
		SOCKET_DBG("connect() failed.");
		closesocket(s);
		return INVALID_SOCKET;
	}

	/* enable TCP_NODELAY */
	/*{
		BOOL optval = 1;
		if(setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (void*)&optval, sizeof(optval))) {
			SOCKET_DBG("could not enable TCP_NODELAY.");
		}
	}*/

	/* enable SO_LINGER */
	socket_linger(s, 0);

	socket_current++;

	return s;
}

int connect_to_host(const char *hostname, short port) {
  LPHOSTENT host_entry;

  host_entry = gethostbyname(hostname); /* FIXME: use getaddrinfo */
  if(host_entry == NULL) return INVALID_SOCKET;
  
  return connect_to_ip(*((unsigned int*)*host_entry->h_addr_list), port);
}

int connect_to_host_non_blocking(const char *hostname, short port) {
  LPHOSTENT host_entry;

  host_entry = gethostbyname(hostname); /* FIXME: use getaddrinfo */
  if(host_entry == NULL) return INVALID_SOCKET;
  
  return connect_to_ip_non_blocking(*((unsigned int*)*host_entry->h_addr_list), port);
}

int create_listening_socket(short int port)
{
	int s;
	SOCKADDR_IN addr;
	BOOL i;

	s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(s == INVALID_SOCKET) {
		SOCKET_DBG("socket() failed.");
		return INVALID_SOCKET;
	}
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	/* set reuse option ON */
	i = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&i, sizeof(BOOL));

	/* enable TCP_NODELAY */
	/*{
		BOOL optval = 1;
		if(setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (void*)&optval, sizeof(optval)))
			printf("could not enable TCP_NODELAY\n");
	}*/

	if(bind(s, (LPSOCKADDR)&addr, sizeof(struct sockaddr)) != 0) {
		SOCKET_DBG("bind() failed.");
		closesocket(s);
		return INVALID_SOCKET;
	}
	listen(s, 200); // SOMAXCONN

	socket_current++;

	return s;
}

void close_socket(int fd)
{

	if(fd != -1) {
		make_socket_blocking(fd, 1);
		shutdown(fd, SD_SEND);

		if(closesocket(fd)) {
			SOCKET_DBG("closesocket() failed. Gle: %u", WSAGetLastError());
		}
		if(socket_current) {
			socket_current--;
		} else {
			SOCKET_DBG("closesocket() unbalanced!");
		}
	}

	return;
}

#ifdef WIN32

/*
	WIN32 port of poll.
	Due to the implementation of select(), this function will fail if ndfs > FD_SETSIZE
*/
int poll(struct pollfd fds[], unsigned int nfds, int timeout) {
    fd_set readset, writeset, exceptset;
    struct timeval tv, *tvptr;
    int rv, i;

	if(!nfds)
		return 0;

	if(nfds > FD_SETSIZE)
		return -1;

    if (timeout < 0) {
        tvptr = NULL;
    }
    else {
        tv.tv_sec = timeout;
        tv.tv_usec = 0;
        tvptr = &tv;
    }

    FD_ZERO(&readset);
    FD_ZERO(&writeset);
    FD_ZERO(&exceptset);

    for (i = 0; i < nfds; i++) {

        fds[i].revents = 0;

		if(fds[i].fd == INVALID_SOCKET)
			continue;

        if (fds[i].events & POLLRDNORM) {
            FD_SET(fds[i].fd, &readset);
        }
        if (fds[i].events & POLLWRNORM) {
            FD_SET(fds[i].fd, &writeset);
        }
        if (fds[i].events & POLLERR) {
            FD_SET(fds[i].fd, &exceptset);
        }
    }

    rv = select(0, &readset, &writeset, &exceptset, tvptr);

    if (!rv) return 0;
	if(rv < 0) return -1;

    for (i = 0; i < nfds; i++) {
        if (FD_ISSET(fds[i].fd, &readset)) {
            fds[i].revents |= POLLRDNORM;
        }
        if (FD_ISSET(fds[i].fd, &writeset)) {
            fds[i].revents |= POLLWRNORM;
        }
        if (FD_ISSET(fds[i].fd, &exceptset)) {
            fds[i].revents |= POLLERR;
        }
    }

    return rv;
}

#endif

/* return the number of bytes available from the socket */
int socket_avail(int fd) {
	unsigned long argp = 0;
	
	/* get the length of data available to be read */
	if(ioctlsocket(fd, FIONREAD, &argp) != 0)
		return 0;

	return (int)argp;
}

int socket_set_max_read(int fd, unsigned int max) {

	return setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char*)&max, sizeof(max));
}

int socket_set_max_write(int fd, unsigned int max) {

	return setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char*)&max, sizeof(max));
}

struct collection *socket_monitors = NULL; /* struct socket_monitor */

static int socket_monitor_get_fd_matcher(struct collection *c, struct socket_monitor *monitor, int fd) {
	/* return any fd that matches and that is NOT DEAD */
	return ((monitor->fd == fd) && !monitor->dead);
}

static struct socket_monitor *socket_monitor_get_fd(int fd) {

	if(!socket_monitors) {
		return NULL;
	}

	return collection_match(socket_monitors, (collection_f)socket_monitor_get_fd_matcher, (void *)fd);
}

static void socket_monitor_obj_destroy(struct socket_monitor *monitor) {

	signal_unref(monitor->connect_signal);
	monitor->connect_signal = NULL;

	signal_unref(monitor->read_signal);
	monitor->read_signal = NULL;

	signal_unref(monitor->write_signal);
	monitor->write_signal = NULL;

	signal_unref(monitor->close_signal);
	monitor->close_signal = NULL;

	signal_unref(monitor->error_signal);
	monitor->error_signal = NULL;

	if(!collection_size(monitor->signals)) {
		collection_destroy(monitor->signals);
		monitor->signals = NULL;
	} else {
		SOCKET_DBG("ERROR!!! Could NOT delete the collection because it is NOT empty!");
	}
	
	collection_delete(socket_monitors, monitor);
	free(monitor);

	return;
}

int socket_monitor_new(int fd, int connected, int listening) {
	struct socket_monitor *monitor;

	if(fd == -1) {
		return 0;
	}
	
	if(!socket_monitors) {
		/* happens on the first call */
		socket_monitors = collection_new();
	}

	monitor = socket_monitor_get_fd(fd);
	if(monitor) {
		/* socket already added. something's wrong. */
		SOCKET_DBG("fds[%08x] already present", fd);
		return 0;
	}

	monitor = malloc(sizeof(struct socket_monitor));
	if(!monitor) {
		SOCKET_DBG("fds[%08x] memory error", fd);
		return 0;
	}

	obj_init(&monitor->o, monitor, (obj_f)socket_monitor_obj_destroy);
	monitor->dead = 0;
	monitor->connected = connected;
	monitor->listening = listening;
	monitor->fd = fd;
	monitor->signals = collection_new();

	monitor->connect_signal = signal_get(monitor->signals, "socket-connect", 1);
	signal_ref(monitor->connect_signal);

	monitor->read_signal = signal_get(monitor->signals, "socket-read", 1);
	signal_ref(monitor->read_signal);

	monitor->write_signal = signal_get(monitor->signals, "socket-write", 1);
	signal_ref(monitor->write_signal);

	monitor->close_signal = signal_get(monitor->signals, "socket-close", 1);
	signal_ref(monitor->close_signal);

	monitor->error_signal = signal_get(monitor->signals, "socket-error", 1);
	signal_ref(monitor->error_signal);

	collection_add(socket_monitors, monitor);
	SOCKET_SIGNALS_DBG("fds[%08x] now added at %08x/%08x", fd, (int)monitor, (int)monitor->signals);

	make_socket_blocking(fd, 0);

	return 1;
}

struct signal_callback *socket_monitor_signal_add(int fd, struct collection *group, char *name, int (*callback)(void *obj, void *param), void *param) {
	struct socket_monitor *monitor;
	struct signal_callback *s;

	if(fd == -1) {
		return 0;
	}

	monitor = socket_monitor_get_fd(fd);
	if(!monitor) {
		/* socket already added. something's wrong. */
		SOCKET_DBG("fds[%08x] Does NOT exist!", fd);
		return NULL;
	}

	s = signal_add(monitor->signals, group, name, callback, param);
	if(!s) {
		SOCKET_DBG("fds[%08x] signal_add failed!", fd);
		return NULL;
	}
	signal_filter(s, (void *)fd);

	return s;
}

int socket_monitor_fd_closed(int fd) {
	struct socket_monitor *monitor;

	monitor = socket_monitor_get_fd(fd);
	if(!monitor) {
		SOCKET_SIGNALS_DBG("fds[%08x] could not be matched to an alive fd!", fd);
		return 0;
	}

	monitor->dead = 1;

	collection_void(monitor->signals);
	obj_destroy(&monitor->o);

	return 1;
}

static int socket_handle_fdset(struct pollfd fdset[], struct socket_monitor *fdset_monitors[], unsigned int nfds) {
	unsigned int i;

	for(i=0;i<nfds;i++) {

		if(!fdset_monitors[i]) {
			continue;
		}

		if(fdset[i].fd == -1) {
			obj_unref(&fdset_monitors[i]->o);
			collection_unlock(socket_monitors, fdset_monitors[i]);
			fdset_monitors[i] = NULL;
			continue;
		}

		if(fdset_monitors[i]->dead) {
			obj_unref(&fdset_monitors[i]->o);
			collection_unlock(socket_monitors, fdset_monitors[i]);
			fdset_monitors[i] = NULL;
			continue;
		}

		if(fdset[i].revents & POLLERR) {
			/*
				Any socket error override other events.
			*/

			if(!fdset_monitors[i]->connected) {
				/* Well, that's a real error only if the socket isn't connected. */
				fdset_monitors[i]->dead = 1;
			}

			SOCKET_SIGNALS_DBG("fds[%08x] POLLERR", fdset[i].fd);

			signal_raise(fdset_monitors[i]->error_signal, (void *)fdset_monitors[i]->fd);

			if(!fdset_monitors[i]->connected) {
				obj_destroy(&fdset_monitors[i]->o);
			}
			obj_unref(&fdset_monitors[i]->o);
			collection_unlock(socket_monitors, fdset_monitors[i]);
			fdset_monitors[i] = NULL;
			continue;
		}

		if(fdset[i].revents & POLLRDNORM) {

			if(!fdset_monitors[i]->connected && fdset_monitors[i]->listening) {

				/* the socket is now connected */

				/*
					we may have more than one connection on a listening socket,
					so we never mark a listening socket as connected. The caller
					must accept() the connection and register the new socket
					instead.
				*/

				SOCKET_SIGNALS_DBG("fds[%08x] POLLRDNORM, connect at %08x", fdset[i].fd, (int)fdset_monitors[i]);
				signal_raise(fdset_monitors[i]->connect_signal, (void *)fdset_monitors[i]->fd);

				obj_unref(&fdset_monitors[i]->o);
				collection_unlock(socket_monitors, fdset_monitors[i]);
				fdset_monitors[i] = NULL;
				continue;
			}
			
			if(socket_avail(fdset_monitors[i]->fd) <= 0) {

				/* the socket had closed gracefully */

				/*
					if the available data size is zero while the POLLRDNORM
					flag is raised, it means the socket just closed
				*/
				fdset_monitors[i]->dead = 1;
				SOCKET_SIGNALS_DBG("fds[%08x] POLLRDNORM, close, %d avail.", fdset[i].fd, socket_avail(fdset_monitors[i]->fd));
				signal_raise(fdset_monitors[i]->close_signal, (void *)fdset_monitors[i]->fd);

				obj_destroy(&fdset_monitors[i]->o);
				obj_unref(&fdset_monitors[i]->o);
				collection_unlock(socket_monitors, fdset_monitors[i]);
				fdset_monitors[i] = NULL;
				continue;
			}
			
			if(fdset_monitors[i]->connected) {

				/* data is ready to be read from the socket */

				//SOCKET_DBG("fds[%08x] POLLRDNORM, read, %u avail.", fdset[i].fd, socket_avail(fdset[i].fd));
				signal_raise(fdset_monitors[i]->read_signal, (void *)fdset_monitors[i]->fd);

				if(fdset_monitors[i]->dead) {
					SOCKET_SIGNALS_DBG("fds[%08x] Deleted during read", fdset[i].fd);
					obj_unref(&fdset_monitors[i]->o);
					collection_unlock(socket_monitors, fdset_monitors[i]);
					fdset_monitors[i] = NULL;
					continue;
				}
			}
		}

		if(fdset[i].revents & POLLWRNORM) {
			if(!fdset_monitors[i]->connected && !fdset_monitors[i]->listening) {
				
				/* the socket is now connected */

				fdset_monitors[i]->connected = 1;
				SOCKET_SIGNALS_DBG("fds[%08x] POLLWRNORM, connect at %08x", fdset[i].fd, (int)fdset_monitors[i]);
				signal_raise(fdset_monitors[i]->connect_signal, (void *)fdset_monitors[i]->fd);

				obj_unref(&fdset_monitors[i]->o);
				collection_unlock(socket_monitors, fdset_monitors[i]);
				fdset_monitors[i] = NULL;
				continue;
			}
			else if(fdset_monitors[i]->connected) {

				/* data is ready to be written to the socket */
				//SOCKET_DBG("fds[%08x] POLLWRNORM, write", fdset[i].fd);
				signal_raise(fdset_monitors[i]->write_signal, (void *)fdset_monitors[i]->fd);

				if(fdset_monitors[i]->dead) {
					SOCKET_SIGNALS_DBG("fds[%08x] Deleted during write", fdset[i].fd);
					obj_unref(&fdset_monitors[i]->o);
					collection_unlock(socket_monitors, fdset_monitors[i]);
					fdset_monitors[i] = NULL;
					continue;
				}
			}
		}

		/* check for any timeout here */
		if(obj_isvalid(&fdset_monitors[i]->o)) {
			signal_poll(fdset_monitors[i]->signals);
		}

		obj_unref(&fdset_monitors[i]->o);
		collection_unlock(socket_monitors, fdset_monitors[i]);
		fdset_monitors[i] = NULL;
	}

	return 1;
}

struct socket_poll_ctx {
	unsigned int count;
	struct pollfd fdset[FD_SETSIZE];
	unsigned int nfds;
	struct socket_monitor *fdset_monitors[FD_SETSIZE];
} __attribute__((packed));

unsigned int socket_poll_monitors(struct collection *c, struct socket_monitor *monitor, void *param) {
	struct socket_poll_ctx *ctx = param;
	int r;

	if(monitor->dead) {
		return 1;
	}

	/* lock now, we'll unlock it when the poll will be over */
	collection_lock(c, monitor);
	obj_ref(&monitor->o);

	ctx->fdset_monitors[ctx->nfds] = monitor;
	ctx->fdset[ctx->nfds].fd = monitor->fd;
	ctx->fdset[ctx->nfds].events = 0;
	ctx->fdset[ctx->nfds].revents = 0;

	if(!monitor->connected) {
		if(monitor->listening)
			ctx->fdset[ctx->nfds].events |= POLLRDNORM;
		else
			ctx->fdset[ctx->nfds].events |= POLLWRNORM;
		
		ctx->fdset[ctx->nfds].events |= POLLERR;
	} else {
		ctx->fdset[ctx->nfds].events |= POLLERR | POLLRDNORM | POLLWRNORM;
	}

	ctx->nfds++;

	if(ctx->nfds == FD_SETSIZE) {
		/* the set is full, we have to poll now */
		r = poll(ctx->fdset, ctx->nfds, 0);
		if(r == -1) {
			SOCKET_DBG("socket_poll: poll returned -1, gle: %u", WSAGetLastError());
		} else {
			ctx->count += r;
		}

		//if(r) {
			socket_handle_fdset(ctx->fdset, ctx->fdset_monitors, ctx->nfds);
		//}

		ctx->nfds = 0;
	}

	return 1;
}

int socket_poll() {
	struct socket_poll_ctx ctx;
	int r = 0;

	ctx.count = 0;
	ctx.nfds = 0;

	collection_iterate(socket_monitors, (collection_f)socket_poll_monitors, &ctx);

	/*
	socket_err_time = 0;
	socket_read_time = 0;
	socket_write_time = 0;
	socket_connect_time = 0;
	socket_close_time = 0;
	*/

	if(ctx.nfds) {
		r = poll(ctx.fdset, ctx.nfds, 0);
		if(r == -1) {
			SOCKET_DBG("socket_poll: poll returned -1, gle: %u", WSAGetLastError());
		} else
			ctx.count += r;

		//if(r) {
			socket_handle_fdset(ctx.fdset, ctx.fdset_monitors, ctx.nfds);
		//}

		ctx.nfds = 0;
	}
	
	/*
	socket_last_err_time = socket_err_time;
	socket_last_read_time = socket_read_time;
	socket_last_write_time = socket_write_time;
	socket_last_connect_time = socket_connect_time;
	socket_last_close_time = socket_close_time;
	*/

	return ctx.count;
}
