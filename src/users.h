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

#ifndef __USERS_H
#define __USERS_H

#include "constants.h"

#include "debug.h"
#if defined(DEBUG_USERS)
# define USERS_DBG(format, arg...) { _DEBUG_CONSOLE(format, ##arg) _DEBUG_FILE(format, ##arg) }
#else
# define USERS_DBG(format, arg...)
#endif

#include <openssl/md5.h>

#include "collection.h"

int users_init();
void users_free();

typedef struct user_ctx user_ctx;
struct user_ctx {
	struct obj o;
	struct collectible c;

	struct config_file *config;

	unsigned long long int created; /* users's creation time */

	char *username;
	char *usergroup;

	/* the password is stored as a MD5 hash */
	char password_hash[MD5_DIGEST_LENGTH];

	char disabled; /* 1 if the user is disabled */

	struct collection *clients;
} __attribute__((packed));

extern struct collection *users;

struct user_ctx *user_new(char *username, char *usergroup, char *password);
struct user_ctx *user_get(char *username);
struct user_ctx *user_load(char *filename);

unsigned int user_dump(struct user_ctx *user);
unsigned int user_auth(struct user_ctx *user, char *password);
unsigned int user_disable(struct user_ctx *user, char disabled);
unsigned int user_delete(struct user_ctx *user);

int user_set_group(struct user_ctx *user, char *group);
int user_set_password(struct user_ctx *user, char *password);

#endif /* __USERS_H */
