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

#ifndef __SOCKET_H
#define __SOCKET_H

#include "constants.h"
#include "signal.h"
#include "obj.h"

#ifndef NO_FTPD_DEBUG
#  define DEBUG_SOCKET
//#  define DEBUG_SOCKET_SIGNALS
#endif

#ifdef DEBUG_SOCKET
# include "logging.h"
# ifdef FTPD_DEBUG_TO_CONSOLE
#  define SOCKET_DBG(format, arg...) printf("["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg)
# else
#  define SOCKET_DBG(format, arg...) logging_write("debug.log", "["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg)
# endif
#else
#  define SOCKET_DBG(format, arg...)
#endif

#ifdef DEBUG_SOCKET_SIGNALS
# include "logging.h"
# ifdef FTPD_DEBUG_TO_CONSOLE
#  define SOCKET_SIGNALS_DBG(format, arg...) printf("["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg)
# else
#  define SOCKET_SIGNALS_DBG(format, arg...) logging_write("debug.log", "["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg)
# endif
#else
#  define SOCKET_SIGNALS_DBG(format, arg...)
#endif

#include <windows.h>
#include <winsock.h>

extern unsigned long long int socket_current;

extern struct collection *socket_monitors;

struct socket_monitor {
	struct obj o;

	int fd;

	/*
		As long as connected = 0, the socket will be kept in nonblocking mode.
		The "socket-connected" signal will be called when connected is set to 1.
	*/
	int connected;
	/*
		Tell if the socket has been created with listen(). connected always stay
		to 0 if listening == 1, and SOCKET_EVENT_CONNECT is called every time
		a new connection is made.
	*/
	int listening;

	/*
		Signal that the socket has faulted, or has died on purpose (connection closed).
		This should not happens, since the application should monitor the CLOSE event
		and then call socket_monitor_fd_closed() but this is here so we don't
		poison the other sockets with invalid ones.
	*/
	int dead;

	struct collection *signals;

	/* to avoid looking them up every times. */
	struct signal_ctx *connect_signal;
	struct signal_ctx *read_signal;
	struct signal_ctx *write_signal;
	struct signal_ctx *close_signal;
	struct signal_ctx *error_signal;
} __attribute__((packed));

void socket_init();
void socket_free();

/* Socket constructors/destructors */
int connect_to_ip(unsigned int ip, short port);
int connect_to_ip_non_blocking(unsigned int ip, short port);
int connect_to_host(const char *hostname, short port);
int connect_to_host_non_blocking(const char *hostname, short port);
int create_listening_socket(short int port);
void close_socket(int s);

/* Utilities */
int socket_avail(int fd);
int socket_set_max_read(int fd, unsigned int max);
int socket_set_max_write(int fd, unsigned int max);
int make_socket_blocking(int s, int blocking);
int socket_linger(int fd, unsigned short timeout);

/* Address formatting  */
unsigned int socket_split_addr(const char *addr, char **host, unsigned short *port);
unsigned int socket_peer_address(int s);
unsigned int socket_local_address(int s);
const char *socket_formated_peer_address(int s);
unsigned int socket_addr(const char *hostname);


/*
	Register a socket to be monitored.
*/
int socket_monitor_new(int fd, int connected, int listening);

/*
	when the socket is closed, this function must be called
	in order to delete it from the monitoring system.
*/
int socket_monitor_fd_closed(int fd);

/*
	This must be called periodically.
	All sockets are polled here, the number of successfull poll is returned.
*/
int socket_poll();

/*
	This must be used to add any signal on a socket.
*/
struct signal_callback *socket_monitor_signal_add(int fd, struct collection *group, char *name, int (*callback)(void *obj, void *param), void *param);

#endif
