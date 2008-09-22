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

#ifndef __IRCCORE_H
#define __IRCCORE_H

#include "constants.h"

#ifndef NO_FTPD_DEBUG
#  define DEBUG_IRC
#endif

#ifdef DEBUG_IRC
# ifdef FTPD_DEBUG_TO_CONSOLE
#  define IRC_DBG(format, arg...) printf("["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg)
# else
#  define IRC_DBG(format, arg...) logging_write("debug.log", "["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg)
# endif
#else
#  define IRC_DBG(format, arg...)
#endif

#include "users.h"

extern struct irc_server irccore_server;
extern struct collection *irc_hooks;

typedef enum {
	IRC_SOURCE_ANY,
	IRC_SOURCE_CHANNEL,
	IRC_SOURCE_PRIVATE,
	IRC_SOURCE_NOTICE,
} irc_source;

struct irc_handler {
	struct obj o;
	struct collectible c;
	
	char *handler;
	irc_source src;
};

typedef struct irc_nick_change irc_nick_change;
struct irc_nick_change {
	char *hostname;
	char *nick;
} __attribute__((packed));

typedef struct irc_join_channel irc_join_channel;
struct irc_join_channel {
	char *hostname;
	char *channel;
} __attribute__((packed));

typedef struct irc_part_channel irc_part_channel;
struct irc_part_channel {
	char *hostname;
	char *channel;
} __attribute__((packed));

typedef struct irc_kick_user irc_kick_user;
struct irc_kick_user {
	char *hostname;
	char *victim;
	char *channel;
} __attribute__((packed));

typedef struct irc_quit irc_quit;
struct irc_quit {
	char *hostname;
} __attribute__((packed));

typedef struct irc_channel irc_channel;
struct irc_channel {
	struct obj o;
	struct collectible c;

	unsigned long long int timestamp; /* last JOIN command timestamp */
	char joined;

	/* collection of the name of all groups the channel is member of */
	struct collection *groups;

	char *name;
	char *key;
	// TODO: blowfish key

	struct collection *queue; /* message queue */
} __attribute__((packed));

struct irc_ctx;

typedef struct irc_server irc_server;
struct irc_server {
	//struct obj o;
	
	//struct irc_ctx *irc_ctx;

	char connected;

	struct collection *channels;

	char *address;
	unsigned short port;
	
	struct collection *group;
	int s;

	char *nickname;
	char *realname;
	char *ident;

	unsigned int filled_length;
	char buffer[1024]; /* hardcoded 1024-bytes message size limit */

	unsigned int delay; /* delay between messages, in milliseconds */
	unsigned long long int timestamp; /* last send time */

	struct collection *queue; /* message queue */

} __attribute__((packed));

// structure used to carry information on
// a message
typedef struct irc_message irc_message;
struct irc_message {
	struct irc_server *server; /* this field is hidden to lua
								since the current implementation
								has support for only one server */
	char *src; /* always the nickname of the sender */
	char *dest; /* where to reply: the source channel or 'sender' if the message was private */
	char *args; /* message arguments */
} __attribute__((packed));

int irccore_init();
void irccore_free();
int irccore_reload();

int irc_connect(struct irc_server *server);
void irc_disconnect(struct irc_server *server);

struct irc_channel *irc_channel_add(struct irc_server *server, char *name, char *key);
unsigned int irc_channel_add_group(struct irc_channel *channel, char *group);

unsigned int irc_lua_raw(const char *message);
unsigned int irc_lua_say(char *channel, char *message);
unsigned int irc_lua_broadcast(char *target, char *message);

unsigned int irc_broadcast_group(struct irc_server *server, char *group, char *message);

unsigned int irc_tree_add(struct collection *branches, char *trigger, char *handler, irc_source src);

#endif /* __IRCCORE_H */
