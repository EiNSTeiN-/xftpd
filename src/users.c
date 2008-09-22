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
#include <io.h>
#include <stdio.h>
#include <openssl/md5.h>

#include "collection.h"
#include "constants.h"
#include "config.h"
#include "users.h"
#include "ftpd.h"
#include "time.h"
#include "events.h"

struct collection *users = NULL;


int users_init() {
	struct user_ctx *user;
	struct _finddata_t fd;
	intptr_t handle;
	char *path, *file;

	USERS_DBG("Loading ...");

	/* load all users from file here */
	users = collection_new();

	path = _fullpath(NULL, ".", 0);
	if(!path) {
		USERS_DBG("Memory error");
		return 0;
	}

	file = malloc(strlen(path) + strlen(USERS_FOLDER "user.*") + 1);
	if(!file) {
		USERS_DBG("Memory error");
		free(path);
		return 0;
	}
	sprintf(file, "%s" USERS_FOLDER "user.*", path);
	free(path);

	handle = _findfirst(file, &fd);
	free(file);
	if (handle == -1) {
		USERS_DBG("Couldn't load \"" USERS_FOLDER "user.*\"");
	} else {
		do {
			if(!strcmp(fd.name, ".") || !strcmp(fd.name, "..")) continue;
			if(fd.attrib & _A_SUBDIR) continue;
			if((strlen(fd.name) > 4) && !stricmp(&fd.name[strlen(fd.name)-4], ".tmp")) continue;

			user = user_load(fd.name);
			if(!user) {
				USERS_DBG("Could not load user from file %s", fd.name);
				continue;
			}
		} while(!_findnext(handle, &fd));
		_findclose(handle);
	}

	USERS_DBG("%u user(s) loaded.", collection_size(users));

	return 1;
}

void users_free() {

	USERS_DBG("Unloading ...");

	return;
}

#define valid_hex(c) \
	(((c >= '0') && (c <= '9')) || \
	((c >= 'A') && (c <= 'F')) || \
	((c >= 'a') && (c <= 'f')))

static unsigned char char_to_hex(unsigned char c) {

	if((c >= '0') && (c <= '9')) return c - '0';
	if((c >= 'A') && (c <= 'F')) return c - 'A' + 10;
	if((c >= 'a') && (c <= 'f')) return c - 'a' + 10;

	return 0;
}

/* load a user from file */
struct user_ctx *user_load(char *filename) {
	struct user_ctx *user;
	char *username, *usergroup, *password;
	char password_hash[MD5_DIGEST_LENGTH];
	unsigned int i;
	char *path;
	char *fullpath;

	if(!filename) return NULL;

	path = _fullpath(NULL, ".", 0);
	if(!path) {
		USERS_DBG("Memory error");
		return NULL;
	}

	fullpath = malloc(strlen(path) + strlen(USERS_FOLDER) + strlen(filename) + 1);
	if(!fullpath) {
		USERS_DBG("Memory error");
		free(path);
		return NULL;
	}
	sprintf(fullpath, "%s" USERS_FOLDER "%s", path, filename);
	free(path);

	filename = fullpath;

	username = config_raw_read(filename, "username", NULL);
	if(!username) {
		USERS_DBG("No username field present in %s", filename);
		free(filename);
		return NULL;
	}

	usergroup = config_raw_read(filename, "usergroup", NULL);
	if(!usergroup) {
		USERS_DBG("No usergroup field present in %s", filename);
		free(filename);
		free(username);
		return NULL;
	}

	password = config_raw_read(filename, "password", NULL);
	if(!password) {
		USERS_DBG("No password field present in %s", filename);
		free(filename);
		free(username);
		free(usergroup);
		return NULL;
	}

	if(strlen(password) != (MD5_DIGEST_LENGTH*2)) {
		USERS_DBG("Password is not hexadecimal/md5 in %s", filename);
		free(filename);
		free(username);
		free(usergroup);
		free(password);
		return NULL;
	}

	/* translate the password in hex */
	for(i=0;i<strlen(password);i++) {
		if(!valid_hex(password[i])) {
			USERS_DBG("Password is not hexadecimal/md5 in %s", filename);
			free(filename);
			free(username);
			free(usergroup);
			free(password);
			return NULL;
		}
	}

	memset(password_hash, 0, sizeof(password_hash));

	for(i=0;i<strlen(password);i++) {
		password_hash[i / 2] |= (char_to_hex(password[i]) << (4 * (1-(i % 2))));
	}

	free(password);

	user = malloc(sizeof(struct user_ctx));
	if(!user) {
		USERS_DBG("Memory error");
		free(filename);
		free(username);
		free(usergroup);
		return NULL;
	}

	user->config = config_new(filename, USERS_CONFIG_TIMEOUT);
	free(filename);
	if(!user->config) {
		USERS_DBG("Memory error");
		free(username);
		free(usergroup);
		free(user);
		return NULL;
	}

	/* enabled by default */
	user->disabled = config_read_int(user->config, "disabled", 0);
	user->created = config_read_int(user->config, "created", 0);

	user->username = username;
	user->usergroup = usergroup;

	memcpy(user->password_hash, password_hash, sizeof(password_hash));

	user->clients = collection_new();

	if(!collection_add(users, user)) {
		USERS_DBG("Collection error");
		config_destroy(user->config);
		collection_destroy(user->clients);
		free(username);
		free(usergroup);
		free(user);
		return NULL;
	}

	return user;
}

/* add a user to the users collection */
struct user_ctx *user_new(char *username, char *usergroup, char *password) {
	char password_hash[(MD5_DIGEST_LENGTH * 2) + 1];
	unsigned int i;
	struct user_ctx *user;
	char filename[128];
	char *fullpath;
	char *path;

	if(!username || !usergroup || !password) return NULL;

	sprintf(filename, "user.%I64u", time_now());

	path = _fullpath(NULL, ".", 0);
	if(!path) {
		USERS_DBG("Memory error");
		return NULL;
	}

	fullpath = malloc(strlen(path) + strlen(USERS_FOLDER) + strlen(filename) + 1);
	if(!fullpath) {
		USERS_DBG("Memory error");
		free(path);
		return NULL;
	}
	sprintf(fullpath, "%s" USERS_FOLDER "%s", path, filename);
	free(path);

	user = malloc(sizeof(struct user_ctx));
	if(!user) {
		USERS_DBG("Memory error");
		free(fullpath);
		return NULL;
	}

	user->config = config_new(fullpath, USERS_CONFIG_TIMEOUT);
	free(fullpath);
	if(!user->config) {
		USERS_DBG("Memory error");
		free(user);
		return NULL;
	}

	user->disabled = 0;
	config_write_int(user->config, "disabled", 0);

	user->created = time_now();
	config_write_int(user->config, "created", user->created);

	user->username = strdup(username);
	if(!user->username) {
		USERS_DBG("Memory error");
		config_destroy(user->config);
		free(user);
		return NULL;
	}
	config_write(user->config, "username", username);

	user->usergroup = strdup(usergroup);
	if(!user->usergroup) {
		USERS_DBG("Memory error");
		config_destroy(user->config);
		free(user->username);
		free(user);
		return NULL;
	}
	config_write(user->config, "usergroup", usergroup);

	user->clients = collection_new();

	MD5(password, strlen(password), user->password_hash);

	memset(password_hash, 0, sizeof(password_hash));
	for(i=0;i<MD5_DIGEST_LENGTH;i++) {
		sprintf(&password_hash[i*2], "%02x", user->password_hash[i] & 0xff);
	}
	config_write(user->config, "password", password_hash);

	collection_add(users, user);

	/* write user infos to file */
	config_save(user->config);

	USERS_DBG("Created user %s", user->username);

	event_onNewUser(user);

	return user;
}

static int user_get_matcher(struct collection *c, struct user_ctx *user, char *username) {

	return !stricmp(user->username, username);
}

/* return a user from its username */
struct user_ctx *user_get(char *username) {

	if(!username) return NULL;

	return collection_match(users, (collection_f)user_get_matcher, username);
}

/*
	Below is convenience functions that keep the config file and
	the user structure synchronized.
*/

int user_set_group(struct user_ctx *user, char *group) {

	if(!user || !group) {
		USERS_DBG("Params error");
		return 0;
	}

	if(user->usergroup) {
		free(user->usergroup);
	}

	user->usergroup = strdup(group);
	if(!user->usergroup) {
		USERS_DBG("Memory error");
		return 0;
	}
	
	config_write(user->config, "usergroup", group);
	config_save(user->config);

	return 1;
}

int user_set_password(struct user_ctx *user, char *password) {
	char password_hash[(MD5_DIGEST_LENGTH * 2) + 1];
	unsigned int i;

	if(!user || !password) {
		USERS_DBG("Params error");
		return 0;
	}

	MD5(password, strlen(password), user->password_hash);

	memset(password_hash, 0, sizeof(password_hash));
	for(i=0;i<MD5_DIGEST_LENGTH;i++) {
		sprintf(&password_hash[i*2], "%02x", user->password_hash[i] & 0xff);
	}
	
	config_write(user->config, "password", password_hash);
	config_save(user->config);

	return 1;
}

/* return 1 if the password is valid for the specified user */
unsigned int user_auth(struct user_ctx *user, char *password) {
	char password_hash[MD5_DIGEST_LENGTH];

	if(!user || !password) return 0;

	MD5(password, strlen(password), password_hash);

	return !memcmp(user->password_hash, password_hash, MD5_DIGEST_LENGTH);
}

static unsigned int kick_clients_callback(struct collection *c, struct ftpd_client_ctx *client, void *param) {

	ftpd_client_destroy(client);
	collection_delete(c, client);

	return 1;
}

/* kick all the user's clients out */
static void kick_clients(struct user_ctx *user) {

	if(!user) return;

	collection_iterate(user->clients, (collection_f)kick_clients_callback, NULL);

	return;
}

/* return 1 if the user was deleted */
unsigned int user_disable(struct user_ctx *user, unsigned int disabled) {

	if(!user) return 0;
	
	/* if we are disabling the user then
		we must kick all the user's
		clients out */
	if(disabled) {
		kick_clients(user);
	}

	user->disabled = disabled;

	/* write user infos to file */
	config_write_int(user->config, "disabled", disabled);
	config_save(user->config);

	return 0;
}

/* return 1 if the user was deleted */
unsigned int user_del(struct user_ctx *user) {

	if(!user) return 0;

	event_onDeleteUser(user);
	
	/* kick all the user's clients out */
	kick_clients(user);

	/* delete the user file */
	if(user->config) {
		remove(user->config->filename);
		config_destroy(user->config);
		user->config = NULL;
	}

	USERS_DBG("User %s deleted", user->username);

	collection_delete(users, user);
	
	/* free the user structure */
	collection_destroy(user->clients);
	free(user->username);
	free(user->usergroup);
	free(user);

	return 1;
}
