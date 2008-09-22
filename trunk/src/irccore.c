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
//#include <poll.h>

#include "constants.h"

#include "asprintf.h"
#include "collection.h"
#include "config.h"
#include "logging.h"
#include "socket.h"
#include "irccore.h"
#include "time.h"
#include "tree.h"
#include "luainit.h"
#include "signal.h"
#include "main.h"
#include "events.h"
#include "blowfish.h"


/* for now, there's support for only one server */
struct irc_server irccore_server;

struct collection *irc_hooks = NULL;

static char *irc_prefix = NULL;

static void irc_channel_obj_destroy(struct irc_channel *channel) {
	
	collectible_destroy(channel);
	
	/* TODO */
	
	return;
}

/* add a channel to the specified server */
struct irc_channel *irc_channel_add(struct irc_server *server, char *name, char *key) {
	struct irc_channel *channel;

	channel = malloc(sizeof(struct irc_channel));
	if(!channel) {
		IRCCORE_DBG("Memory error");
		return 0;
	}
	
	obj_init(&channel->o, channel, (obj_f)irc_channel_obj_destroy);
	collectible_init(channel);

	channel->timestamp = time_now();
	channel->joined = 0;

	channel->name = strdup(name);
	if(!channel->name) {
		IRCCORE_DBG("Memory error");
		free(channel);
		return 0;
	}

	if(key) {
		channel->key = strdup(key);
		if(!channel->key) {
			IRCCORE_DBG("Memory error");
			free(channel->name);
			free(channel);
			return 0;
		}
	} else {
		channel->key = NULL;
	}

	channel->queue = collection_new(C_CASCADE);
	channel->groups = collection_new(C_CASCADE);

	if(!collection_add(server->channels, channel)) {
		IRCCORE_DBG("Collection error");
		collection_destroy(channel->groups);
		collection_destroy(channel->queue);
		free(channel->name);
		if(key) free(channel->key);
		free(channel);
		return NULL;
	}

	//IRCCORE_DBG("Channel %s added", name);

	return channel;
}

/* enqueue a raw message to be sent to the server */
unsigned int irc_raw(struct irc_server *server, const char *fmt, ...) {
	struct ftpd_collectible_line *l;
	int result;
	char *str;

	va_list args;
	va_start(args, fmt);
	result = vasprintf(&str, fmt, args);
	if(result == -1) return 0;
	va_end(args);
	
	l = ftpd_line_new(str);
	if(l) {
		if(!collection_add(server->queue, l)) {
			IRCCORE_DBG("Collection error");
		} else {
			collection_movelast(server->queue, l);
		}
	}
	free(str);

	return 1;
}

/* used by irc_channel_exist */
static int get_channel_callback(struct collection *c, void *item, void *param) {
	struct irc_channel *channel = item;
	struct {
		struct irc_server *server;
		char *channel_name;
		struct irc_channel *channel;
	} *ctx = param;

	if(!strcasecmp(ctx->channel_name, channel->name)) {
		ctx->channel = channel;
	}

	return 1;
}

/* return the specified channel struct or NULL on error */
static struct irc_channel *get_channel(struct irc_server *server, char *channel_name) {
	struct {
		struct irc_server *server;
		char *channel_name;
		struct irc_channel *channel;
	} ctx = { server, channel_name, NULL };

	collection_iterate(server->channels, get_channel_callback, &ctx);
	
	return ctx.channel;
}

static unsigned int enqueue_message(struct collection *queue, char *fmt, ...) {
	struct ftpd_collectible_line *l;
	int result;
	char *str;

	va_list args;
	va_start(args, fmt);
	result = vasprintf(&str, fmt, args);
	if(result == -1) return 0;
	va_end(args);
	
	l = ftpd_line_new(str);
	if(l) {
		if(!collection_add(queue, l)) {
			IRCCORE_DBG("Collection error");
		} else {
			collection_movelast(queue, l);
		}
	}
	free(str);

	return 1;
}

unsigned int irc_say(struct irc_server *server, char *channel_name, char *fmt, ...) {
	struct irc_channel *channel;
	int result;
	char *str;

	va_list args;
	va_start(args, fmt);
	result = vasprintf(&str, fmt, args);
	if(result == -1) return 0;
	va_end(args);

	channel = get_channel(server, channel_name);

	if(channel) {
		if(channel->use_blowfish) {
			char *enc_message;
			
			/* encrypt the blowfish message */
			enc_message = blowfish_encrypt(&channel->blowfish, str);
			if(!enc_message) {
				IRCCORE_DBG("memory error while encrypting blowfish message");
				free(str);
				return 1;
			}
			
			/* enqueue the message in the channel list */
			if(!enqueue_message(channel->queue, "PRIVMSG %s :+OK %s\n", channel->name, enc_message)) {
				free(str);
				return 0;
			}
			
			free(enc_message);
		} else {
			/* enqueue the message in the channel list */
			if(!enqueue_message(channel->queue, "PRIVMSG %s :%s\n", channel->name, str)) {
				free(str);
				return 0;
			}
		}
	} else {
		/* enqueue the message in the global list */
		if(!enqueue_message(server->queue, "PRIVMSG %s :%s\n", channel_name, str)) {
			free(str);
			return 0;
		}
	}

	free(str);

	return 1;
}

/* add a group to a channel */
unsigned int irc_channel_add_group(struct irc_channel *channel, char *group) {
	struct ftpd_collectible_line *l;

	l = ftpd_line_new(group);
	if(l) {
		if(!collection_add(channel->groups, l)) {
			IRCCORE_DBG("Collection error");
			return 0;
		}
		return 1;
	}

	return 0;
}

static int is_channel_in_group_callback(struct collection *c, struct ftpd_collectible_line *group, void *param) {
	struct {
		unsigned int success;
		char *group;
	} *ctx = param;

	if(!strcasecmp(group->line, ctx->group)) {
		ctx->success = 1;
		return 0;
	}

	return 1;
}

/* check if the channel is in the specified group */
static unsigned int is_channel_in_group(struct irc_channel *channel, char *group) {
	struct {
		unsigned int success;
		char *group;
	} ctx = { 0, group };

	collection_iterate(channel->groups, (collection_f)is_channel_in_group_callback, &ctx);

	return ctx.success;
}

/* used by broadcast_group */
static int broadcast_group_callback(struct collection *c, struct irc_channel *channel, void *param) {
	struct {
		unsigned int success;
		char *target;
		char *message;
		struct irc_server *server;
	} *ctx = param;

	if(!strcasecmp(ctx->target, "all") || is_channel_in_group(channel, ctx->target)) {
		if(!irc_say(&irccore_server, channel->name, "%s", ctx->message)) {
			ctx->success = 0;
			return 0;
		} else {
			ctx->success = 1;
			return 1;
		}
	}

	return 1;
}

/* broadcast any channel group with a message */
unsigned int irc_broadcast_group(struct irc_server *server, char *group, char *message) {
	struct {
		unsigned int success;
		char *target;
		char *message;
		struct irc_server *server;
	} ctx = { 0, group, message, server };

	collection_iterate(server->channels, (collection_f)broadcast_group_callback, &ctx);

	return ctx.success;
}

/* 'target' may be any channel group, any real channel, or any nickname.
	the target is resolved by first looking up for any channel group
	if none can be found then the target is assumed to be a channel
	name or a nickname and the message is passed up to irc_say
	*/
unsigned int irc_lua_broadcast(char *target, char *message) {

	if(!target || !message) {
		IRCCORE_DBG("Parameter error");
		return 0;
	}

	if(irc_broadcast_group(&irccore_server, target, message)) return 1;
	if(irc_say(&irccore_server, target, "%s", message)) return 1;

	return 0;
}

unsigned int irc_lua_say(char *channel, char *message) {

	if(!channel | !message) {
		IRCCORE_DBG("Parameter error");
		return 0;
	}

	return irc_say(&irccore_server, channel, "%s", message);
}


unsigned int irc_lua_raw(const char *message) {

	return irc_raw(&irccore_server, "%s", message);
}

/* used by irc_channel_exist */
static unsigned int channel_exist_callback(struct collection *c, struct irc_channel *channel, void *param) {
	struct {
		unsigned int success;
		struct irc_server *server;
		char *channel_name;
	} *ctx = param;

	if(!strcasecmp(ctx->channel_name, channel->name)) {
		ctx->success = 1;
	}

	return 1;
}

/* return 1 if the specified channel exists */
unsigned int irc_channel_exist(struct irc_server *server, char *channel_name) {
	struct {
		unsigned int success;
		struct irc_server *server;
		char *channel_name;
	} ctx = { 0, server, channel_name };

	collection_iterate(server->channels, (collection_f)channel_exist_callback, &ctx);
	
	return ctx.success;
}

static unsigned int call_command_handlers(struct collection *c, struct irc_handler *handler, void *param) {
	struct {
		struct irc_server *server;
		char *sender;
		char *target;
		unsigned int notice;
		char *args;
	} *ctx = param;
	struct irc_message m = { ctx->server, ctx->sender, ctx->target, ctx->args };
	//unsigned int err;
	unsigned int ret = 1;
	lua_State *L = handler->script->L;
	
	/* want from notice but comming from somewhere else */
	if((handler->src == IRC_SOURCE_NOTICE) && !ctx->notice) return 1;
	
	/* want from channel but comming from private */
	if((handler->src == IRC_SOURCE_CHANNEL) && (*ctx->target != '#')) return 1;
	
	/* want from private but comming from channel */
	if((handler->src == IRC_SOURCE_PRIVATE) && (*ctx->target == '#')) return 1;
	
	lua_pushcfunction(L, luainit_traceback);
	
	luainit_tget(L, IRCCORE_REFTABLE, handler->handler_index);
	if(lua_isfunction(L, -1)) {
		int err;
		
		/* push all params */
		tolua_pushusertype(L, &m, "irc_message");
		
		/* call the function with two params and one return */
		err = lua_pcall(L, 1, 1, -3);
		if(err) {
			/* do something with the error ... ? */
			luainit_error(L, "(calling irc handler)", err);
		} else {
			ret = lua_isnumber(L, -1) ? (int)lua_tonumber(L, -1) : (int)tolua_toboolean(L, -1, ret);
		}
		
		/* pops the error message or the return value */
		lua_pop(L, 1);
	} else {
		/* pops the thing we just pushed that is not a function */
		lua_pop(L, 1);
	}
	lua_pop(L, 1); /* pops the errfunc */
	
	return ret;
}

static int irc_handler_command_do_handlers(char *message, void *param) {
	struct collection *handlers;
	struct {
		struct irc_server *server;
		char *sender;
		char *target;
		unsigned int notice;
		char *args;
	} *ctx = param;
	
	handlers = tree_get(irc_hooks, message, &ctx->args);
	if(!handlers) {
		IRCCORE_DBG("No handler for command %s", message);
		return 0;
	}
	
	/* call all handlers found for that command */
	collection_iterate(handlers, (collection_f)call_command_handlers, ctx);
	
	if(ctx->args) {
		free(ctx->args);
		ctx->args = NULL;
	}
	
	return 1;
}

/* handle any command sent over irc */
/* notice: 0 for PRIVMSG */
/* target: nickname or channel */
static unsigned int irc_handle_command(struct irc_server *server, char *sender, unsigned int notice, char *target, char *message) {
	struct {
		struct irc_server *server;
		char *sender;
		char *target;
		unsigned int notice;
		char *args;
	} ctx = { server, sender, target, notice, NULL };
	char *ptr;

	/* if the target is not a channel, then change the target for the sender's nickname */
	if(*target != '#') {
		ctx.target = strdup(sender);
		if(!ctx.target) {
			IRCCORE_DBG("Memory error");
			return 0;
		}

		ptr = strchr(ctx.target, '!');
		if(ptr) *ptr = 0;
	}

	/* skip the command prefix */
	if(!strncasecmp(message, irc_prefix, strlen(irc_prefix))) {
		message += strlen(irc_prefix);
		
		if(!irc_handler_command_do_handlers(message, &ctx)) {
			if(*target != '#') free(ctx.target);
			return 1;
		}
	} else if(!strncasecmp(message, "+OK ", 4) || !strncasecmp(message, "mcps ", 5)) {
		struct irc_channel *channel;
		
		message = strchr(message, ' ')+1;
		
		if(!*message) {
			if(*target != '#') free(ctx.target);
			return 1;
		}
		
		channel = get_channel(server, target);
		if(channel && channel->use_blowfish) {
			char *dec_message;
			
			dec_message = blowfish_decrypt(&channel->blowfish, message);
			if(!dec_message) {
				IRCCORE_DBG("memory error when decrypting blowfish message");
			} else {
				if(!strncasecmp(dec_message, irc_prefix, strlen(irc_prefix))) {
					char *tmp = dec_message;
					tmp += strlen(irc_prefix);
					
					if(!irc_handler_command_do_handlers(tmp, &ctx)) {
						if(*target != '#') free(ctx.target);
						memset(dec_message, 0, strlen(dec_message));
						free(dec_message);
						dec_message = NULL;
						return 1;
					}
				}
				
				memset(dec_message, 0, strlen(dec_message));
				free(dec_message);
				dec_message = NULL;
			}
		} else {
			IRCCORE_DBG("Channel not found or channel not using blowfish (%s)", target);
		}
	}
	
	if(*target != '#') free(ctx.target);

	return 1;
}

/* used by irc_channel_exist */
static int mark_channel_joined_callback(struct collection *c, struct irc_channel *channel, void *param) {
	struct {
		unsigned int success;
		struct irc_server *server;
		char *channel_name;
		unsigned int joined;
	} *ctx = param;

	if(!strcasecmp(ctx->channel_name, channel->name)) {
		channel->joined = ctx->joined;
		ctx->success = 1;
	}

	return 1;
}

/* mark the specified channel as joined */
static unsigned int mark_channel_joined(struct irc_server *server, char *channel_name, unsigned int joined) {
	struct {
		unsigned int success;
		struct irc_server *server;
		char *channel_name;
		unsigned int joined;
	} ctx = { 0, server, channel_name, joined };

	collection_iterate(server->channels, (collection_f)mark_channel_joined_callback, &ctx);
	
	return ctx.success;
}

//"sender" changes its nick for "nick"
static int irc_handle_nick(struct irc_server *server, char* sender, char* nick) {
	struct event_parameter params[2];
	struct irc_nick_change ctx;
	unsigned int i;

	/* get the sender's nick alone */
	for(i=0;i<strlen(sender);i++)
		if((*(sender+i) == '!') || (*(sender+i) == '@')) break;

	/* if we're the sender */
	if(!strncasecmp(sender, server->nickname, i)) {
		/* update our nickname internally */
		free(server->nickname);
		server->nickname = strdup(nick);
		if(!server->nickname) {
			IRCCORE_DBG("Memory error");
			return 0;
		}
	}

	/* raise the event */
	ctx.hostname = sender;
	ctx.nick = nick;

	params[0].ptr = server;
	params[0].type = "irc_server";

	params[1].ptr = &ctx;
	params[1].type = "irc_nick_change";

	IRCCORE_DBG("onIrcNickChange (%s, %s)", sender, nick);

	event_raise("onIrcNickChange", 2, &params[0]);

	return 1;
}

//"sender" join "channel"
//we'll check if it is us who join a channel
//if it is we'll check which channel
//if we know the channel we'll set joined to TRUE
//if we don't know it we'll leave the channel
static int irc_handle_join(struct irc_server *server, char* sender, char* channel) {
	struct event_parameter params[2];
	struct irc_join_channel ctx;
	unsigned int i;

	/* get the sender's nick alone */
	for(i=0;i<strlen(sender);i++)
		if((*(sender+i) == '!') || (*(sender+i) == '@')) break;

	/* if we're the sender */
	if(!strncasecmp(sender, server->nickname, i)) {
		if(irc_channel_exist(server, channel)) {
			mark_channel_joined(server, channel, 1);
		} else {
			/* we're not on that channel - leave it */
			irc_raw(server, "PART %s\n", channel);
			return 1;
		}
	}
	
	/* raise the event */
	ctx.hostname = sender;
	ctx.channel = channel;

	params[0].ptr = server;
	params[0].type = "irc_server";

	params[1].ptr = &ctx;
	params[1].type = "irc_join_channel";

	IRCCORE_DBG("onIrcJoinChannel (%s, %s)", sender, channel);

	event_raise("onIrcJoinChannel", 2, &params[0]);

	return 1;
}

//"sender" part "channel"
//we'll check if it is us who part this chan
//if it is we'll check wich channel it is
//if we know the channel we'll set joined to FALSE
static int irc_handle_part(struct irc_server *server, char* sender, char* channel) {
	struct event_parameter params[2];
	struct irc_part_channel ctx;
	unsigned int i;

	/* get the sender's nick alone */
	for(i=0;i<strlen(sender);i++)
		if((*(sender+i) == '!') || (*(sender+i) == '@')) break;

	if(!strncasecmp(sender, server->nickname, i)) {
		mark_channel_joined(server, channel, 0);
	}
	
	/* raise the event */
	ctx.hostname = sender;
	ctx.channel = channel;

	params[0].ptr = server;
	params[0].type = "irc_server";

	params[1].ptr = &ctx;
	params[1].type = "irc_part_channel";

	IRCCORE_DBG("onIrcPartChannel (%s, %s)", sender, channel);

	event_raise("onIrcPartChannel", 2, &params[0]);

	return 1;
}

//"sender" kick "user" from "channel"
//check if if is us who was kicked
//if it is, we search for the channel
//if we know the channel we set joined to FALSE
static int irc_handle_kick(struct irc_server *server, char* sender, char* user, char* channel) {
	struct event_parameter params[2];
	struct irc_kick_user ctx;
	
	if(!strcasecmp(user, server->nickname)) {
		mark_channel_joined(server, channel, 0);
	}
	
	/* raise the event */
	ctx.hostname = sender;
	ctx.victim = user;
	ctx.channel = channel;

	params[0].ptr = server;
	params[0].type = "irc_server";

	params[1].ptr = &ctx;
	params[1].type = "irc_kick_user";

	IRCCORE_DBG("onIrcKickUser (%s, %s, %s)", sender, user, channel);

	event_raise("onIrcKickUser", 2, &params[0]);

	return 1;
}

//"sender" quits the server
static int irc_handle_quit(struct irc_server *server, char* sender) {
	struct event_parameter params[2];
	struct irc_quit ctx;
	
	/* raise the event */
	ctx.hostname = sender;

	params[0].ptr = server;
	params[0].type = "irc_server";

	params[1].ptr = &ctx;
	params[1].type = "irc_quit";

	IRCCORE_DBG("onIrcQuit (%s)", sender);

	event_raise("onIrcQuit", 2, &params[0]);

	return 1;
}

/* handle any line sent by the master.
	return 0 on error */
unsigned int irc_handle(struct irc_server *server, const char *line) {
	unsigned int numeric;
	unsigned int i;
	char *nickname;
	char *s[32];

	if(!strncasecmp(line, "PING", 4)) {
		return irc_raw(server, "PONG%s\n", &line[4]);
	}

	/* separate the line received */
	s[0] = strdup(line+1);
	for(i=1;i<32;i++) {
		s[i] = strchr(s[i-1],' ');
		if(s[i] == NULL) break;
		while(*s[i] == ' ') {
			*s[i] = 0;
			s[i]++;
		}
		if(*s[i] == ':') {
			*s[i] = 0;
			s[i]++;
			break;
		}
	}

	if(!strcasecmp(s[1], "PRIVMSG")) {			// handle any private message
		//Jack send a message to John
		//":Jack!ident@hostname PRIVMSG John :message"
		//Jack say something in the channel #Josh
		//":Jack!ident@hostname PRIVMSG #Josh :message"
		irc_handle_command(server, s[0], 0, s[2], s[3]);
	} else if(!strcasecmp(s[1], "NOTICE")) {		// handle any notice message
		//Jack send a notice to John
		//":Jack!ident@hostname NOTICE John :message"
		irc_handle_command(server, s[0], 1, s[2], s[3]);
	} else if(!strcasecmp(s[1], "NICK")) {		// handle any change of nickname
		//Jack change his nick to John
		//":Jack!ident@hostname NICK :John"
		irc_handle_nick(server, s[0], s[2]);
	} else if(!strcasecmp(s[1], "JOIN")) {		// handle anyone who join a channel
		//Jack join the channel #John
		//":Jack!ident@hostname JOIN :#John"
		//":Jack!ident@hostname JOIN #John" (sometimes)
		irc_handle_join(server, s[0], s[2]);
	} else if(!strcasecmp(s[1], "PART")) {		// handle anyone who part a channel
		//Jack part the channel #John
		//":Jack!ident@hostname PART #John"
		irc_handle_part(server, s[0], s[2]);
	} else if(!strcasecmp(s[1], "KICK")) {		// handle any kick
		//Jack kick John from the channel #Josh
		//":Jack!ident@hostname KICK #Josh John :message"
		irc_handle_kick(server, s[0], s[3], s[2]);
	} else if(!strcasecmp(s[1], "QUIT")) {		// handle any quit
		//Jack quits the s[1]
		//":Jack!ident@hostname QUIT :"
		irc_handle_quit(server, s[0]);
	} else {									// handle any raw commands
		numeric = atoi(s[1]);
		if(numeric) switch(numeric) {
		case 1: {
			
			/*
				TODO: check if this message is really thrown at each connection
			*/
			
			struct event_parameter params[1];
			
			/* raise the event */
			params[0].ptr = server;
			params[0].type = "irc_server";
			
			IRCCORE_DBG("onIrcConnect");
			
			event_raise("onIrcConnect", 1, &params[0]);
			
			break;
		}
		case 432: //erroneous nickname: illegal characters
			//we don't want an assigned nickname if it's erroneous
			IRCCORE_DBG("The server rejected \"%s\" as a nickname.", server->nickname);

			/* free the server address so we won't get reconnected */
			free(server->address);
			server->address = NULL;

			free(s[0]);
			return 0;
		case 433: //nickname already in use
		//case 443: //nickname already in use
		case 431: //no nickname given

			nickname = malloc(strlen(server->nickname)+2);
			if(!nickname) {
				IRCCORE_DBG("Memory error");
				free(s[0]);
				return 0;
			}
			sprintf(nickname, "%s`", server->nickname);
			
			IRCCORE_DBG("\"%s\" is already taken, using \"%s\".", server->nickname, nickname);

			//just add a ` to the nickname so the server will leave us alone
			free(server->nickname);
			server->nickname = nickname;
			
			free(s[0]);
			return irc_raw(server, "NICK %s\n", server->nickname);
		}
	}

	free(s[0]);
	return 1;
}

/* return 0 if no line could be read & correctly parsed */
static unsigned int parse_line_from_master(struct irc_server *server) {
//	unsigned int i;
	int /*avail,*/ recvd, tryagain;
	
	
	while(1) {
		tryagain = 0;
		recvd = secure_recv(&server->secure, &server->buffer[server->filled_length], 1, &tryagain);
		if((recvd == -1) && tryagain) {
			IRCCORE_DBG("Could NOT read a line (will try again!)");
			break;
		}
		if((recvd == -1) &&
#ifdef WIN32
		  (WSAGetLastError() == WSAEWOULDBLOCK)
#else
      (errno == EWOULDBLOCK)
#endif
		) {
			/* no more data available? */
			IRCCORE_DBG("No more data available to be read from socket, %u filled.", server->filled_length);
			break;
		}
		if(recvd != 1) {
			IRCCORE_DBG("Could not receive from socket.");
			return 0;
		}
		
		if(server->buffer[server->filled_length] == 0) continue;
		if(server->buffer[server->filled_length] == '\r') continue;
		if(server->buffer[server->filled_length] == '\n') {
			
			/* parse the line and call the right handler */
			server->buffer[server->filled_length] = 0;
			
			if(!irc_handle(server, server->buffer)) {
				IRCCORE_DBG("Could not handle command from server.");
				return 0;
			}
			
			server->filled_length = 0;
			break;
		}
		
		server->filled_length++;
		if(server->filled_length == sizeof(server->buffer)) {
			IRCCORE_DBG("Server sent a line that is too big to be handled (over %u bytes).", sizeof(server->buffer));
			return 0;
		}
	}

	/* we couldn't catch a \n. return so we
		can read more data next time */

	return 1;
}

/* send a line to the server (and only one line) */
static int send_from_queue_callback(struct collection *c, struct ftpd_collectible_line *l, void *param) {
	struct {
		unsigned int success;
		struct irc_server *server;
		struct collection *queue;
	} *ctx = param;
	unsigned int size = strlen(l->line);
	int i, tryagain = 0;

	i = secure_send(&ctx->server->secure, l->line, size, &tryagain);
	if((i == -1) && tryagain) {
		IRCCORE_DBG("Could NOT send the current line (will try again!)");
		ctx->server->timestamp = time_now();
		return 0;
	}
	
	if(i != size) {
		IRCCORE_DBG("ERROR: Line size mismatch sent size (expected: %u, sent: %d)", size, i);
		ctx->success = 0;
	}

	ftpd_line_destroy(l);

	/* set new timestamp */
	ctx->server->timestamp = time_now();

	return 0;
}

/* return 1 if data was sent from the queue to the server */
static unsigned int send_from_queue(struct irc_server *server, struct collection *queue) {
	struct {
		unsigned int success;
		struct irc_server *server;
		struct collection *queue;
	} ctx = { 1, server, queue };
	
	collection_iterate(queue, (collection_f)send_from_queue_callback, &ctx);

	return ctx.success;
}

/* send data from the channel queue to the server */
static int send_from_channel_queue_callback(struct collection *c, struct irc_channel *channel, void *param) {
	struct {
		unsigned int success;
		struct irc_server *server;
	} *ctx = param;

	if(!channel->joined) {
		if(timer(channel->timestamp) > 5000) {
			/* hardcoded 5 secondes between JOIN queries */
			irc_raw(ctx->server, "JOIN %s%s%s\n", channel->name,
				channel->key ? " " : "",
				channel->key ? channel->key : ""
			);
			IRCCORE_DBG("joining channel %s with key %s", channel->name, channel->key ? channel->key : ""); 
			channel->timestamp = time_now();
		}
		/* return 1 so we'll still try to send a message if any exists */
		return 1;
	} else {

		if(collection_size(channel->queue)) {
			if(!send_from_queue(ctx->server, channel->queue))
				ctx->success = 0;

			/* move this channel to the end of the chain so the next
				time its another channel that will receive a message */
			collection_movelast(ctx->server->channels, channel);
			return 0;
		}
	}

	return 1;
}

/* return 0 if any error occured while sending data to the server */
/* return 1 if no data was ready to be sent */
static unsigned int send_from_channels(struct irc_server *server) {
	struct {
		unsigned int success;
		struct irc_server *server;
	} ctx = { 1, server };

	/* try to send data from any channel queue */
	collection_iterate(server->channels, (collection_f)send_from_channel_queue_callback, &ctx);

	return ctx.success;
}

int irccore_server_secure_write(int fd, struct irc_server *server) {

	if(!server->connected) {
		unsigned int i;
		
		IRCCORE_DBG("Now connected to %s:%u.", server->address, server->port);
		
		/* now we can send/receive something */
		
		i = 1;
		setsockopt(server->s, SOL_SOCKET, SO_KEEPALIVE, (char *)&i, sizeof(int));
		
		/* enqueue the USER line */
		irc_raw(server,
			"NICK %s\n",
			server->nickname
		);
		irc_raw(server,
			"USER %s localhost %s :%s\n",
			server->ident,
			server->address,
			server->realname
		);
		
		server->connected = 1;
		return 1;
	}

	if(collection_size(server->queue) && (timer(server->timestamp) > server->delay)) {
		/* try to send data from the global queue */
		if(!send_from_queue(server, server->queue)) {
			IRCCORE_DBG("Could not send anything from server queue.");
			irc_disconnect(server);
			return 0;
		}
	} else {
		/* try to send data from any channel queue */
		if(!send_from_channels(server)) {
			IRCCORE_DBG("Could not send anything to channel.");
			irc_disconnect(server);
			return 0;
		}
	}

	return 1;
}

int irccore_server_secure_read(int fd, struct irc_server *server) {

	/* read a line & parse it */
	if(!parse_line_from_master(server)) {
		IRCCORE_DBG("Could not parse a line from master.");
		irc_disconnect(server);
		return 0;
	}

	return 1;
}

int irccore_server_secure_connect(int fd, struct irc_server *server) {
	
	IRCCORE_DBG("SSL Negotiation completed with success.");
	
	IRCCORE_DBG("SSL Using Ciphers: %s", SSL_get_cipher_name(server->secure.ssl));
	IRCCORE_DBG("SSL Connection Protocol: %s", SSL_get_cipher_version(server->secure.ssl));
	
	return 1;
}

int irccore_server_secure_error(int fd, struct irc_server *server) {
	
	IRCCORE_DBG("SSL Negotiation could not be completed!");
	
	return 1;
}

int irccore_load_config() {
	unsigned int i;
	char *name, *key, *groups, *group, *ptr, *blowfish_key;
	char buffer[128];
	struct irc_channel *channel;
	
	irccore_server.use_ssl = config_raw_read_int(MASTER_CONFIG_FILE, "xftpd.irc.ssl", 0);

	/* try to get the ciphers list */
	irccore_server.ssl_ciphers = config_raw_read(MASTER_CONFIG_FILE, "xftpd.irc.ssl-ciphers", NULL);

	/* if we can't read the hostname, disable the ircbot */
	irccore_server.address = config_raw_read(MASTER_CONFIG_FILE, "xftpd.irc.address", NULL);
	if(!irccore_server.address) {
		IRCCORE_DBG("xftpd.irc.address is not set.");
		return 0;
	}
	
	i = config_raw_read_int(MASTER_CONFIG_FILE, "xftpd.irc.port", 6667);
	if(!i || (i & 0xffff0000)) {
		IRCCORE_DBG("xftpd.irc.port must be between 1-65535, defaulting to 6667");
		i = 6667;
	}
	irccore_server.port = (short)i;

	irccore_server.nickname = config_raw_read(MASTER_CONFIG_FILE, "xftpd.irc.nickname", "xFTPd_bot");
	if(!irccore_server.nickname)  {
		IRCCORE_DBG("xftpd.irc.nickname is not set.");
		return 0;
	}

	irccore_server.realname = config_raw_read(MASTER_CONFIG_FILE, "xftpd.irc.realname", "xFTPd");
	if(!irccore_server.realname) {
		IRCCORE_DBG("xftpd.irc.realname is not set.");
		return 0;
	}

	irccore_server.ident = config_raw_read(MASTER_CONFIG_FILE, "xftpd.irc.ident", "xFTPd");
	if(!irccore_server.ident) {
		IRCCORE_DBG("xftpd.irc.ident is not set.");
		return 0;
	}

	irc_prefix = config_raw_read(MASTER_CONFIG_FILE, "xftpd.irc.prefix", "!");
	if(!irc_prefix) {
		IRCCORE_DBG("xftpd.irc.prefix is not set.");
		return 0;
	}

	irccore_server.delay = config_raw_read_int(MASTER_CONFIG_FILE, "xftpd.irc.delay", 600);

	/* load all channels */
	for(i=1;;i++) {
		sprintf(buffer, "xftpd.irc.channel(%u).name", i);
		name = config_raw_read(MASTER_CONFIG_FILE, buffer, NULL);
		if(!name) break;

		sprintf(buffer, "xftpd.irc.channel(%u).key", i);
		key = config_raw_read(MASTER_CONFIG_FILE, buffer, NULL);

		sprintf(buffer, "xftpd.irc.channel(%u).blowfish-key", i);
		blowfish_key = config_raw_read(MASTER_CONFIG_FILE, buffer, NULL);

		sprintf(buffer, "xftpd.irc.channel(%u).groups", i);
		groups = config_raw_read(MASTER_CONFIG_FILE, buffer, NULL);

		channel = irc_channel_add(&irccore_server, name, key);
		if(channel) {
			group = groups;
			while(group) {
				ptr = strchr(group, ';');
				if(ptr) {
					*ptr = 0;
					ptr++;
				}
				irc_channel_add_group(channel, group);
				group = ptr;
			}
			channel->use_blowfish = 0;
			if(blowfish_key) {
				if(!blowfish_init(&channel->blowfish, (unsigned char *)blowfish_key, strlen(blowfish_key))) {
					IRCCORE_DBG("Could not initialize blowfish key for channel %s", name);
				} else {
					channel->use_blowfish = 1;
				}
				free(blowfish_key);
				blowfish_key = NULL;
			}
		}

		free(groups);
		free(name);
		free(key);
	}

	IRCCORE_DBG("Loaded %u channel(s).", collection_size(irccore_server.channels));

	return 1;
}

int irccore_server_read_timeout(struct irc_server *server) {

	IRCCORE_DBG("Server connection read error");

	irc_disconnect(server);

	if(!irc_connect(server)) {
		IRCCORE_DBG("Could not reconnect to server");
		main_fatal();
		return 0;
	}

	return 1;
}
int irccore_server_write_timeout(struct irc_server *server) {

	IRCCORE_DBG("Server connection read error");

	irc_disconnect(server);

	if(!irc_connect(server)) {
		IRCCORE_DBG("Could not reconnect to server");
		main_fatal();
		return 0;
	}

	return 1;
}

int irccore_server_connect(int fd, struct irc_server *server) {
	struct signal_callback *s;

	IRCCORE_DBG("Server is now connected.");

	/* set the socket setting */
	socket_set_max_read(fd, IRC_SOCKET_SIZE);
	socket_set_max_write(fd, IRC_SOCKET_SIZE);
	
	secure_connect(&server->secure, fd);

	/*
	s = socket_monitor_signal_add(server->s, server->group, "socket-read", (signal_f)irccore_server_read, server);
	signal_timeout(s, IRC_DATA_TIMEOUT, (timeout_f)irccore_server_read_timeout, server);
	
	s = socket_monitor_signal_add(server->s, server->group, "socket-write", (signal_f)irccore_server_write, server);
	signal_timeout(s, IRC_DATA_TIMEOUT, (timeout_f)irccore_server_write_timeout, server);
	*/
	
	s = secure_signal_add(&server->secure, server->group, "secure-read", (signal_f)irccore_server_secure_read, server);
	signal_timeout(s, IRC_DATA_TIMEOUT, (timeout_f)irccore_server_read_timeout, server);
	
	s = secure_signal_add(&server->secure, server->group, "secure-write", (signal_f)irccore_server_secure_write, server);
	signal_timeout(s, IRC_DATA_TIMEOUT, (timeout_f)irccore_server_write_timeout, server);
	
	s = secure_signal_add(&server->secure, server->group, "secure-resume-recv", (signal_f)irccore_server_secure_read, server);
	s = secure_signal_add(&server->secure, server->group, "secure-resume-send", (signal_f)irccore_server_secure_write, server);
	
	s = secure_signal_add(&server->secure, server->group, "secure-connect", (signal_f)irccore_server_secure_connect, server);
	s = secure_signal_add(&server->secure, server->group, "secure-error", (signal_f)irccore_server_secure_error, server);

	/* if we should use ssl for this server, then start the negotiation right now */
	if(server->use_ssl) {
		IRCCORE_DBG("Starting SSL Negotiation.");
		secure_negotiate(&server->secure);
		return 1;
	}

	return 1;
}

int irccore_server_error(int fd, struct irc_server *server) {

	IRCCORE_DBG("Server connection error");

	irc_disconnect(server);

	if(!irc_connect(server)) {
		IRCCORE_DBG("Could not reconnect to server");
		main_fatal();
		return 0;
	}

	return 1;
}

int irccore_server_close(int fd, struct irc_server *server) {

	IRCCORE_DBG("Server connection closed");

	irc_disconnect(server);

	if(!irc_connect(server)) {
		IRCCORE_DBG("Could not reconnect to server");
		main_fatal();
		return 0;
	}

	return 1;
}

int irc_connect(struct irc_server *server) {

	/* setup the socket */
	server->s = connect_to_host_non_blocking(server->address, server->port);
	if(server->s == -1) {
		IRCCORE_DBG("Socket error");
		return 0;
	}
	server->connected = 0;

	/* hook all signals on the socket */
	socket_monitor_new(server->s, 0, 0);
	socket_monitor_signal_add(server->s, server->group, "socket-connect", (signal_f)irccore_server_connect, server);
	socket_monitor_signal_add(server->s, server->group, "socket-error", (signal_f)irccore_server_error, server);
	socket_monitor_signal_add(server->s, server->group, "socket-close", (signal_f)irccore_server_close, server);

	return 1;
}

static int irc_disconnect_exit_channels(struct collection *c, struct irc_channel *channel, void *param) {

	/* we do not free the channel queue so when we are reconnected the sending will be resumed */

	channel->joined = 0;

	return 1;
}

/* disconnect the bot from the specified server */
void irc_disconnect(struct irc_server *server) {

	/* mark all channels as not joined */
	collection_iterate(server->channels, (collection_f)irc_disconnect_exit_channels, NULL);

	/* empty the server message queue */
	collection_empty(server->queue);

	signal_clear(server->group);
	
	secure_close(&server->secure);

	/* close the socket, mark the server as not connected */
	if(server->s != -1) {
		socket_monitor_fd_closed(server->s);
		close_socket(server->s);
		server->s = -1;
	}

	return;
}

int irccore_init() {

	IRCCORE_DBG("Loading...");

	irc_hooks = collection_new(C_CASCADE);
	
	irccore_server.queue = collection_new(C_CASCADE);
	irccore_server.channels = collection_new(C_CASCADE);

	/* give default values */
	irccore_server.connected = 0;

	irccore_server.address = NULL;
	irccore_server.port = 0;
	irccore_server.s = -1;
	irccore_server.group = collection_new(C_CASCADE);
	
	irccore_server.use_ssl = 0;
	irccore_server.ssl_ciphers = NULL;

	irccore_server.nickname = NULL;
	irccore_server.realname = NULL;
	irccore_server.ident = NULL;

	memset(&irccore_server.buffer[0], 0, sizeof(irccore_server.buffer));
	irccore_server.filled_length = 0;

	irccore_server.delay = 0;
	irccore_server.timestamp = 0;

	if(!irccore_load_config()) {
		IRCCORE_DBG("THE IRC CONFIG COULD NOT BE LOADED, NO SITEBOT AVAILABLE.");
		return 1;
	}
	
	/* setup the secure context */
	secure_setup(&irccore_server.secure,  SECURE_TYPE_CLIENT);
	SSL_CTX_set_cipher_list(irccore_server.secure.ssl_ctx,
		irccore_server.ssl_ciphers ? irccore_server.ssl_ciphers : "ALL");

#ifdef MASTER_WITH_IRC_CLIENT
	if(!irc_connect(&irccore_server)) {
		IRCCORE_DBG("Could not make the irc server connect.");
		return 0;
	}
#else
	IRCCORE_DBG("Compiled without IRC client!");
#endif

	return 1;
}

void irccore_free(struct irc_ctx *ctx) {

	IRCCORE_DBG("Unloading...");

	irc_disconnect(&irccore_server);
	
	/* free all channels */

	/* free all ... */

	tree_destroy(irc_hooks);
	collection_destroy(irc_hooks);
	irc_hooks = NULL;

	return;
}

/* delete all hooks */
int irccore_reload(struct irc_ctx *ctx) {

	IRCCORE_DBG("Reloading...");

	/* remove all handlers in the tree */
	tree_destroy(irc_hooks);

	return 1;
}
