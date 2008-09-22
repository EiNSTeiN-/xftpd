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

/*
	FTP Deamon engine
	
	rfc-959 compliant (in progress)
		English: http://www.faqs.org/rfcs/rfc959.html
		French: http://abcdrfc.free.fr/rfc-vf/rfc959.html

    rfc-2389 compliant (in progress)
		English: http://www.wu-ftpd.org/rfc/rfc2389.html
*/
#include <windows.h>
#include <stdio.h>
#include <poll.h>

#include "constants.h"
#include "asprintf.h"
#include "socket.h"
#include "ftpd.h"
#include "collection.h"
#include "main.h"
#include "config.h"
#include "logging.h"
#include "vfs.h"
#include "slaves.h"
#include "time.h"
#include "events.h"
#include "irccore.h"
#include "scripts.h"
#include "sfv.h"
#include "stats.h"
#include "slaveselection.h"
#include "timeout.h"
#include "signal.h"
#include "main.h"
#include "site.h"
#include "nuke.h"
#include "asynch.h"
#include "luainit.h"


/* Config stuff */
unsigned short ftpd_client_port = 0;

unsigned short ftpd_low_data_port = 0;
unsigned short ftpd_high_data_port = 0;
unsigned short ftpd_current_data_port = 0;

char *ftpd_banner = NULL;

/* Context data */
struct collection *ftpd_group = NULL;
int ftpd_fd = -1;

struct collection *clients = NULL;

unsigned long long int ftpd_next_xfer_uid = 0;

static ftpd_command ftpd_client_text_to_command(char *buffer, unsigned int len) {

	if(len < 3) return CMD_UNKNOWN;
	if(!strnicmp(buffer, "PWD", 3)) return CMD_PRINT_WORKING_DIRECTORY;

	if(len < 4) return CMD_UNKNOWN;
	if(!strnicmp(buffer, "QUIT", 4)) return CMD_QUIT;
	if(!strnicmp(buffer, "SYST", 4)) return CMD_SYSTEM;
	if(!strnicmp(buffer, "FEAT", 4)) return CMD_FEATURES;
	if(!strnicmp(buffer, "CWD ", 4)) return CMD_CHANGE_WORKING_DIRECTORY;
	if(!strnicmp(buffer, "CDUP", 4)) return CMD_CHANGE_TO_PARENT_DIRECTORY;
	if(!strnicmp(buffer, "REIN", 4)) return CMD_REINITIALIZE;
	if(!strnicmp(buffer, "PASV", 4)) return CMD_PASSIVE;
	if(!strnicmp(buffer, "ABOR", 4)) return CMD_ABORT;
	if(!strnicmp(buffer, "RMD ", 4)) return CMD_REMOVE_DIRECTORY;
	if(!strnicmp(buffer, "MKD ", 4)) return CMD_MAKE_DIRECTORY;
	if(!strnicmp(buffer, "LIST", 4)) return CMD_LIST;
	if(!strnicmp(buffer, "NLST", 4)) return CMD_NAME_LIST;
	if(!strnicmp(buffer, "STAT", 4)) return CMD_STATUS;
	if(!strnicmp(buffer, "HELP", 4)) return CMD_HELP;
	if(!strnicmp(buffer, "NOOP", 4)) return CMD_NO_OPERATION;
	if(!strnicmp(buffer, "STOU", 4)) return CMD_STORE_UNIQUE;

	if(len < 5) return CMD_UNKNOWN;
	if(!strnicmp(buffer, "USER ", 5)) return CMD_USERNAME;
	if(!strnicmp(buffer, "PASS ", 5)) return CMD_PASSWORD;
	if(!strnicmp(buffer, "CLNT ", 5)) return CMD_CLIENT;
	if(!strnicmp(buffer, "ACCT ", 5)) return CMD_ACCOUNT;
	if(!strnicmp(buffer, "SMNT ", 5)) return CMD_STRUCTURE_MOUNT;
	if(!strnicmp(buffer, "ALLO ", 5)) return CMD_ALLOCATE;
	if(!strnicmp(buffer, "PORT ", 5)) return CMD_DATA_PORT;
	if(!strnicmp(buffer, "TYPE ", 5)) return CMD_REPRESENTATION_TYPE;
	if(!strnicmp(buffer, "STRU ", 5)) return CMD_FILE_STRUCTURE;
	if(!strnicmp(buffer, "MODE ", 5)) return CMD_TRANSFER_MODE;
	if(!strnicmp(buffer, "RETR ", 5)) return CMD_RETRIEVE;
	if(!strnicmp(buffer, "STOR ", 5)) return CMD_STORE;
	if(!strnicmp(buffer, "APPE ", 5)) return CMD_APPEND;
	if(!strnicmp(buffer, "REST ", 5)) return CMD_RESTART;
	if(!strnicmp(buffer, "RNFR ", 5)) return CMD_RENAME_FROM;
	if(!strnicmp(buffer, "RNTO ", 5)) return CMD_RENAME_TO;
	if(!strnicmp(buffer, "DELE ", 5)) return CMD_DELETE;
	if(!strnicmp(buffer, "SITE ", 5)) return CMD_SITE;
	if(!strnicmp(buffer, "SIZE ", 5)) return CMD_SIZE_OF_FILE;
	if(!strnicmp(buffer, "PRET ", 5)) return CMD_PRE_TRANSFER;

	/*
		Special case of "ABOR" that FlashFXP send. Looks like: ÿôÿòÿABOR
		What a broken client.
	*/
	if((strlen(buffer) > 4) && !strnicmp(&buffer[strlen(buffer)-4], "ABOR", 4)) return CMD_BROKEN_ABORT;

	return CMD_UNKNOWN;
}

void ftpd_line_obj_destroy(struct ftpd_collectible_line *l) {
	
	collectible_destroy(l);
	free(l);
	
	return;
}

void ftpd_line_destroy(struct ftpd_collectible_line *l) {
	
	obj_destroy(&l->o);
	return;
}

struct ftpd_collectible_line *ftpd_line_new(char *line) {
	struct ftpd_collectible_line *l;
	unsigned int len;
	
	len = strlen(line);
	l = malloc(sizeof(struct ftpd_collectible_line) + len+1);
	if(!l) {
		FTPD_DBG("Memory error");
		return NULL;
	}
	
	obj_init(&l->o, l, (obj_f)ftpd_line_obj_destroy);
	collectible_init(l);
	
	memcpy(&l->line[0], line, len+1);
	
	return l;
}

/* add a message for the client from lua */
unsigned int ftpd_lua_message(struct ftpd_client_ctx *client, const char *msg) {
	char *tmp, *line;
	struct ftpd_collectible_line *l;
	char *ptr;

	if(!client || !msg) return 0;

	tmp = strdup(msg);
	if(!tmp) {
		FTPD_DBG("Memory error");
		return 0;
	}

	/* add line by line */
	line = tmp;
	do {
		ptr = strchr(line, '\n');
		if(ptr) {
			*ptr = 0;
			ptr++;
		}

		l = ftpd_line_new(line);
		if(l) {
			if(!collection_add(client->messages, l)) {
				FTPD_DBG("Collection error");
			} else {
				collection_movelast(client->messages, l);
			}
		}
		
		line = ptr;
	} while(line && strlen(line));

	free(tmp);

	return 1;
}

/* add a formatted message for the client */
unsigned int ftpd_message(struct ftpd_client_ctx *client, char *format, ...) {
//	char *line;
//	char *ptr;
	int result;
	char *str=NULL;
	va_list args;

	if(!client || !format) return 0;

	va_start(args, format);
	result = vasprintf(&str, format, args);
	if(result == -1) {
		FTPD_DBG("Memory error");
		return 0;
	}
	va_end(args);

	ftpd_lua_message(client, str);

	free(str);

	return 1;
}

/* enqueue a message to be sent to the socket */
static unsigned int ftpd_client_text_enqueue(struct collection *c, char *format, ...) {
	int result;
	char *str = NULL;
	struct ftpd_collectible_line *l;

	va_list args;
	va_start(args, format);
	result = vasprintf(&str, format, args);
	if(result == -1) {
		FTPD_DBG("Memory error");
		return 0;
	}
	va_end(args);
	
	l = ftpd_line_new(str);
	if(l) {
		if(!collection_add(c, l)) {
			FTPD_DBG("Collection error");
		} else {
			collection_movelast(c, l);
		}
	}
	
	free(str);

	return 1;
}

/* parse any inputs from a new client that is not logged */
static unsigned int ftpd_client_parse_unlogged_input(struct ftpd_client_ctx *client, ftpd_command *command, char *ptr) {
	char *Pointer;

	switch(*command) {
	case CMD_USERNAME:
/*		USER <SP> <username> <CRLF>
		  230
		  530
		  500, 501, 421
		  331, 332
*/
		Pointer = strchr(ptr, ' ');
		Pointer++;
		
		FTPD_DIALOG_DBG("[%08x] CMD_USERNAME (%s)", (int)client, Pointer);

		if(client->last_command != CMD_NONE) {
			ftpd_client_text_enqueue(client->messages,
				"421-Invalid USER command at this point.\n"
				"421 Service not available, closing control connection.\n"
			);
			return 0;
		}

		strncpy(client->username, Pointer, sizeof(client->username)-1);

		/* check the client username and copy it */
		ftpd_client_text_enqueue(client->messages, "331 User name okay, need password.");

		break;
	case CMD_PASSWORD:
/*		PASS <SP> <password> <CRLF>
		  230
		  202
		  530
		  500, 501, 503, 421
		  332
*/
		Pointer = strchr(ptr, ' ');
		Pointer++;

		FTPD_DIALOG_DBG("[%08x] CMD_PASSWORD (%s)", (int)client, "hidden" /*Pointer*/);

		if(client->last_command != CMD_USERNAME) {
			ftpd_client_text_enqueue(client->messages,
				"421-Invalid PASS command at this point.\n"
				"421 Service not available, closing control connection.\n"
			);
			return 0;
		}

		{
			if(!collection_size(users)) {

				/* no user exist, create one */
				client->user = user_new(client->username, "xFTPd", Pointer);
				if(!client->user) {
					/* couldn't create new user */
					ftpd_client_text_enqueue(client->messages,
						"530-There is no user account created, and\n"
						"530-  xFTPd is unable to create one for %s\n"
						"530 Not logged in\n",
						client->username
					);
					return 0;
				}

				ftpd_client_text_enqueue(client->messages,
					"230-WELCOME TO xFTPd.\n"
					"230-As there was no user in the database, xFTPd\n"
					"230-  has created an admin account for %s\n",
					client->username
				);
			} else {
				/* try to get the user structure for this username/password */
				client->user = user_get(client->username);
				if(!client->user) {
					/* bad username */
					event_onClientLoginFail(client);
					ftpd_client_text_enqueue(client->messages, "530 Not logged in, %s.\n", client->username);
					return 0;
				}

				if(client->user->disabled || !user_auth(client->user, Pointer)) {
					/* bad password */
					event_onClientLoginFail(client);
					ftpd_client_text_enqueue(client->messages, "530 Not logged in, %s.\n", client->username);
					return 0;
				}

				/* correct the case for the username's copy */
				strcpy(client->username, client->user->username);
			}

			//obj_ref(&client->user->o);

			/* add this client to the user's client list */
			collection_add(client->user->clients, client);

			if(!event_onClientLoginSuccess(client)) {
				//printf("[client:%I64u] rejected by event_onClientLoginSuccess()");
				ftpd_client_text_enqueue(client->messages, "530 Not logged in.");
				return 0;
			}

			/* check if the client has the right password */
			ftpd_client_text_enqueue(client->messages, "230 User logged in, %s.\n", client->user->username);

			//obj_unref(&client->user->o);

			client->logged = 1;

		}

		break;
	case CMD_QUIT:
/*		QUIT <CRLF>
		  221
		  500
*/
		FTPD_DIALOG_DBG("[%08x] CMD_QUIT", (int)client);
		ftpd_client_text_enqueue(client->messages, "221 Service closing control connection.");

		return 0;
	default:

		FTPD_DBG("Unresolved: %s", ptr);

		ftpd_client_text_enqueue(client->messages,
			"421-You should have logged in.\n"
			"421 Service not available, closing control connection.\n"
		);
		return 0;
	}

	return 1;
}

unsigned short ftpd_get_next_data_port() {
	unsigned short port;

	if((ftpd_current_data_port < ftpd_low_data_port) || (ftpd_current_data_port > ftpd_high_data_port))
		ftpd_current_data_port = ftpd_low_data_port;

	port = ftpd_current_data_port;

	ftpd_current_data_port++;

	return port;
}

static const char *format_month(unsigned short month) {

	switch(month) {
	case 1: return "Jan";
	case 2: return "Feb";
	case 3: return "Mar";
	case 4: return "Apr";
	case 5: return "May";
	case 6: return "Jun";
	case 7: return "Jul";
	case 8: return "Aug";
	case 9: return "Sep";
	case 10: return "Oct";
	case 11: return "Nov";
	case 12: return "Dec";
	default:
		{
			FTPD_DBG("Invalid month: %u", month);
		}
	}

	return "Jan";
}

/* prepare the directory listing to be sent to the client */
static unsigned int ftpd_client_make_directory_listing(struct collection *c, struct vfs_element *element, struct ftpd_client_ctx *client) {
	char date[32];
	char type;
	unsigned short year, month, day, hour, minute;
	struct user_ctx *user;
	char *targetpath;

	switch(element->type) {
	case VFS_FOLDER:
		type = 'd';
		break;
	case VFS_LINK:
		type = 'l';
		break;
	default:
		type = '-';
		break;
	}

	/* format the timestamp of the file */
	time_stamp_to_formated(element->timestamp, &day, &month, &year, &hour, &minute);

	if(is_this_year(element->timestamp))
		/* Jan  1 22:43 (of this year) */
		sprintf(date, "%s %2u %02u:%02u", format_month(month), (int)day, (int)hour, (int)minute);
	else {
		/* Jan  1 1971 (of another year) */
		sprintf(date, "%s %2u %04u", format_month(month), (int)day, (int)year);
	}

	user = user_get(element->owner);

	if(element->type == VFS_LINK) {
		targetpath = vfs_get_relative_path(vfs_root, element->link_to);

		ftpd_client_text_enqueue(
			client->data_ctx.data,
			"%cwr-wr-wr- 1 %s %s %I64u %s %s -> %s\r\n",
			type,
			user ? user->username : (element->owner ? element->owner : "xFTPd"),
			user ? user->usergroup : "xFTPd",
			element->size,
			date,
			element->name,
			targetpath ? targetpath : "/"
		);
	} else {
		ftpd_client_text_enqueue(
			client->data_ctx.data,
			"%cwr-wr-wr- 1 %s %s %I64u %s %s\r\n",
			type,
			user ? user->username : (element->owner ? element->owner : "xFTPd"),
			user ? user->usergroup : "xFTPd",
			element->size,
			date,
			element->name
		);
	}

	return 1;
}

/* insert an element in the directory listing that is nonexistant in the vfs
	just so the user can see it without being there */
unsigned int ftpd_inject_listing(struct ftpd_client_ctx *client, char type, const char *name, const char *owner) {
	unsigned short month, day, hour, minute;
	char date[32];
	char ctype;

	if(!name || !client) return 0;

	switch(type) {
	case VFS_FOLDER:
		ctype = 'd';
		break;
	case VFS_FILE:
		ctype = '-';
		break;
	case VFS_LINK:
		ctype = 'l';
		break;
	default:
		return 0;
	}

	/* format the timestamp of "now" */
	time_stamp_to_formated(time_now(), &day, &month, NULL, &hour, &minute);

	/* something like: jan 01 22:43 (of this year) */
	sprintf(date, "%s %02u %02u:%02u", format_month(month), (int)day, (int)hour, (int)minute);

	return ftpd_client_text_enqueue(
		client->data_ctx.data,
		"%cwr-wr-wr- 1 %s xFTPd 0 %s %s\r\n",
		ctype,
		owner ? owner : "xFTPd",
		date,
		name
	);
}

/* this is called on PASV when the connection is passive
	and on STOR/RETR for active connections
	it make sure the file exists and it setup everyting */
static unsigned int setup_file_download(struct ftpd_client_ctx *client, struct vfs_element *container, char *file) {
	struct slave_connection *cnx;
	struct vfs_element *element;

#ifdef FTPD_STOP_WORKING_TIMESTAMP
	if((time_now() / 1000) > FTPD_STOP_WORKING_TIMESTAMP) {
		ftpd_client_text_enqueue(client->messages, "Please visit www.xftpd.com and get a new version!");
		return 0;
	}
#endif

	element = vfs_find_element(container, file);
	if(!element || (element->type != VFS_FILE)) {
		ftpd_client_text_enqueue(client->messages, "File not found or target is not a file.");
		return 0;
	}

	/* file must not have an uploader linked to it */
	if(element->uploader) {
		ftpd_client_text_enqueue(client->messages, "The file is being uploaded.");
		return 0;
	}

	cnx = slaveselection_download(element);
	if(!cnx) {
		ftpd_client_text_enqueue(client->messages, "Slaveselection failed (no transfer slave).");
		
		FTPD_DBG("couldn't find suitable slave for download");
		return 0;
	}

	if(!cnx->ready) {
		ftpd_client_text_enqueue(client->messages, "Slaveselection failed (chosen slave is not ready).");
		
		FTPD_DBG("WARNING: chosen slave is NOT ready.");
		return 0;
	}

	if(client->xfer.cnx) {

		FTPD_DBG("WARNING: XFER's CONNECTION IS NOT NULL for download");
		return 0;
	}

	/* link this upload and the slave */
	client->xfer.cnx = cnx;
	if(!collection_add(cnx->xfers, client)) {
		FTPD_DBG("Collection error");
		client->xfer.cnx = NULL;
		return 0;
	}

	/* link this download and the file */
	client->xfer.element = element;
	if(!collection_add(element->leechers, client)) {
		FTPD_DBG("Collection error");
		collection_delete(cnx->xfers, client);
		client->xfer.cnx = NULL;
		client->xfer.element = NULL;
		return 0;
	}


	/* downloading ... */
	client->xfer.upload = 0;

	/* find a new xfer uid */
	client->xfer.uid = ftpd_next_xfer();
	client->xfer.last_alive = time_now();

	if(!event_onPreDownload(client, element)) {
		ftpd_client_text_enqueue(client->messages, "Transfer rejected by external policy.");
		
		client->xfer.uid = -1;
		collection_delete(element->leechers, client);
		collection_delete(cnx->xfers, client);
		client->xfer.cnx = NULL;
		client->xfer.element = NULL;
		return 0;
	}

	return 1;
}

/* this is called on PASV when the connection is passive
	and on STOR/RETR for active connections
	it make sure the file does not exists and it setup everyting */
static unsigned int setup_file_upload(struct ftpd_client_ctx *client, struct vfs_element *container, char *file) {
	struct slave_connection *cnx;
	struct vfs_element *element;

#ifdef FTPD_STOP_WORKING_TIMESTAMP
	if((time_now() / 1000) > FTPD_STOP_WORKING_TIMESTAMP) {
		ftpd_client_text_enqueue(client->messages, "Please visit www.xftpd.com and get a new version!");
		return 0;
	}
#endif

	element = vfs_find_element(container, file);
	if(element) {
		ftpd_client_text_enqueue(client->messages, "Target file already exist.");

		//FTPD_DBG("upload can't be set up: file already exists");
		return 0;
	}

	cnx = slaveselection_upload(container);
	if(!cnx) {
		ftpd_client_text_enqueue(client->messages, "Slaveselection failed (no transfer slave).");

		FTPD_DBG("no suitable slave for upload");
		return 0;
	}

	if(!cnx->ready) {
		ftpd_client_text_enqueue(client->messages, "Slaveselection failed (chosen slave is not ready).");
		
		FTPD_DBG("WARNING: chosen slave is NOT ready.");
		return 0;
	}

	/* create an empty 0-byte file */
	element = vfs_create_file(container, file, client->username);
	if(!element) {
		ftpd_client_text_enqueue(client->messages, "Could not create target file.");

		FTPD_DBG("can't create the new file (%s)", file);
		return 0;
	}

	nuke_check(element);

	/* Check if the chosen slave's vroot is a parent of 'element' */
	if(!vfs_is_child(cnx->slave->vroot, element)) {
		FTPD_DBG("Trying to upload a file outside the scope %s's vroot", cnx->slave->name);
		ftpd_client_text_enqueue(client->messages, "BUG YOUR SITEOP: CANNOT UPLOAD ON %s AT THIS LOCATION", cnx->slave->name);
		ftpd_client_text_enqueue(client->messages, "   BECAUSE IT IS OUSIDE THE SCOPE OF ITS VROOT.");

		vfs_recursive_delete(element);
		return 0;
	}

	if(client->xfer.cnx) {
		FTPD_DBG("WARNING: XFER's CONNECTION IS NOT NULL for upload");
		
		return 0;
	}

	/* link this upload and the slave */
	client->xfer.cnx = cnx;
	if(!collection_add(cnx->xfers, client)) {
		FTPD_DBG("Collection error");
		client->xfer.cnx = NULL;
		return 0;
	}

	/* link this download and the file */
	client->xfer.element = element;
	element->uploader = client;

	/* uploading ... */
	client->xfer.upload = 1;

	/* find a new xfer uid */
	client->xfer.uid = ftpd_next_xfer();
	client->xfer.last_alive = time_now();

	if(!event_onPreUpload(client, element)) {
		ftpd_client_text_enqueue(client->messages, "Transfer rejected by external policy.");
		
		client->xfer.uid = -1;
		client->xfer.upload = 0;
		element->uploader = NULL;
		client->xfer.element = NULL;
		client->xfer.cnx = NULL;
		collection_delete(cnx->xfers, client);
		vfs_recursive_delete(element);
		return 0;
	}

	return 1;
}

/* terminate and clean any transfer going on between
	the client and the master/slave */
void ftpd_client_cleanup_data_connection(struct ftpd_client_ctx *client) {

	FTPD_DIALOG_DBG("Closing data connection for %08x", (int)client);

	/* cancel the currently assigned asynch command */
	if(client->xfer.cmd) {
		asynch_destroy(client->xfer.cmd, NULL);
		client->xfer.cmd = NULL;
	}

	if(client->xfer.cnx) {
		/* delete this client from the slave xfer list */
		if(collection_find(client->xfer.cnx->xfers, client)) {
			collection_delete(client->xfer.cnx->xfers, client);
		} else {
			FTPD_DBG("Client not found in slave's xfers!");
		}
		client->xfer.cnx = NULL;
	}

	if(client->xfer.element) {
		if(client->xfer.upload) {
			/* client was uploading: delete the file from vfs */
			client->xfer.element->uploader = NULL;
			vfs_recursive_delete(client->xfer.element);
		} else {
			/* client was downloading: delete this client from the element xfer list */
			collection_delete(client->xfer.element->leechers, client);
		}
		client->xfer.element = NULL;
	}

	if(client->data_ctx.group) {
		signal_clear(client->data_ctx.group);
	}

	if(client->data_ctx.fd != -1) {
		close_socket(client->data_ctx.fd);
		socket_monitor_fd_closed(client->data_ctx.fd);
		client->data_ctx.fd = -1;
	}

	/* cleanup data */
	if(client->data_ctx.data) {
		collection_empty(client->data_ctx.data);
	}

	/* common variables */
	client->slave_xfer = 0;
	client->passive = 0;
	client->ready = 0;
	client->ip = 0;
	client->port = 0;

	/* slave xfer variables */
	client->xfer.uid = -1;
	client->xfer.restart = 0;
	client->xfer.upload = 0;
	client->xfer.xfered = 0;
	client->xfer.last_alive = 0;

	FTPD_DIALOG_DBG("Dictonnected client at %08x", (int)client);

	return;
}

/* p is NULL on timeout and on read error */
static unsigned int slave_listen_query_callback(struct slave_connection *cnx, struct slave_asynch_command *cmd, struct packet *p) {
	struct slave_listen_reply *reply;
	struct ftpd_client_ctx *client = (struct ftpd_client_ctx *)cmd->param;

	/* unlink this command */
	client->xfer.cmd = NULL;
	
	/* the client is waiting for an answer */
	if(!p) {

		/* the slave couldn't answer/timeout occured */
		ftpd_client_text_enqueue(client->messages,
			"425-Communication error occured.\n"
			"425 Can't open data connection.\n"
		);
		
		/* cleanup data connection */
		ftpd_client_cleanup_data_connection(client);
		return 1;
	}

	//printf("[slave:%I64u] Slave listen response received (q:%I64u)\n", cnx->uid, p->uid);

	if(p->type == IO_FAILURE) {
		/* general failure status: slave couldn't listen */
		
		/* give its answer to the client */
		ftpd_client_text_enqueue(client->messages,
			"425-The slave explicitly rejected your request\n"
			"425 Can't open data connection.\n"
		);

		/* cleanup data connection */
		ftpd_client_cleanup_data_connection(client);
		return 1;
	}

	if(p->type == IO_SLAVE_LISTENING) {
		/* the slave should have answered with
			the port/ip the client should connect to */
		if((p->size - sizeof(struct packet)) < sizeof(struct slave_listen_reply)) {
			/* protocol error */
			
			ftpd_client_text_enqueue(client->messages,
				"425-Unknown protocol error.\n"
				"425 Can't open data connection.\n"
			);

			/* cleanup data connection */
			ftpd_client_cleanup_data_connection(client);
			return 0;
		}

		client->ready = 1;

		reply = (struct slave_listen_reply *)&p->data;
		/* listen succeed, tell the client */
		ftpd_client_text_enqueue(client->messages, "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u).\n",
			(reply->ip) & 0xff,
			(reply->ip >> 8) & 0xff,
			(reply->ip >> 16) & 0xff,
			(reply->ip >> 24) & 0xff,
			(reply->port >> 8) & 0xff,
			(reply->port) & 0xff
		);

		return 1;
	}

	/* protocol error */
	ftpd_client_text_enqueue(client->messages,
		"425-Unknown protocol error\n"
		"425 Can't open data connection.\n"
	);

	/* cleanup data connection */
	ftpd_client_cleanup_data_connection(client);

	return 0;
}

/* ask the slave to start listening for a client
	the callback will send 200 or 425 to the client */
static unsigned int make_slave_listen_query(struct ftpd_client_ctx *client) {
	struct slave_asynch_command *cmd;
	struct slave_listen_request data;
		
	FTPD_DIALOG_DBG("%I64u: Querying the slave to listen", client->xfer.uid);
	data.xfer_uid = client->xfer.uid;

	cmd = asynch_new(client->xfer.cnx, IO_SLAVE_LISTEN, MASTER_ASYNCH_TIMEOUT, (void*)&data, sizeof(data), slave_listen_query_callback, client);
	if(!cmd) return 0;

	client->xfer.cmd = cmd;
	client->xfer.last_alive = time_now();

	return 1;
}

/* simply return the current xfer uid and increment for the next xfer */
unsigned long long int ftpd_next_xfer() {
	return ftpd_next_xfer_uid++;
}

/* p is NULL on timeout and on read error */
static unsigned int slave_transfer_query_callback(struct slave_connection *cnx, struct slave_asynch_command *cmd, struct packet *p) {
	struct ftpd_client_ctx *client = (struct ftpd_client_ctx *)cmd->param;
	struct slave_transfer_reply *reply;

	/* unlink this command */
	client->xfer.cmd = NULL;

	/* the client is waiting for an answer */
	FTPD_DIALOG_DBG("%I64u: Transfer response received.", cmd->uid);

	if(!p) {
		/* the slave couldn't answer/timeout occured */
		ftpd_client_text_enqueue(client->messages, "451 Requested action aborted.");

		/* cleanup data connection */
		ftpd_client_cleanup_data_connection(client);
		return 1;
	}

	reply = (struct slave_transfer_reply *)&p->data;

	if(p->type == IO_FAILURE) {
		/* general failure status: slave couln't transfer the file */
		
		/* there are much more place where a failed download
			may end up, but we call the onTransferFail callback
			only when the slave explicitly reply with IO_FAILURE */
		event_onTransferFail(client, &client->xfer);
		
		/* give its answer to the client */
		ftpd_client_text_enqueue(client->messages,
			"451-The slave could not complete the request.\n"
			"451 Requested action aborted.\n"
		);

		/* cleanup data connection */
		ftpd_client_cleanup_data_connection(client);
		return 1;
	}

	if(!client->xfer.element) {

		FTPD_DBG("ERROR: NO ELEMENT");

		ftpd_client_cleanup_data_connection(client);
		return 1;
	}

	if(p->type == IO_SLAVE_TRANSFERED) {
		/* the slave should have answered with
			the port/ip the client should connect to */
		if((p->size - sizeof(struct packet)) < sizeof(struct slave_transfer_reply)) {
			/* protocol error */
			
			ftpd_client_text_enqueue(client->messages,
				"451 Requested action aborted. Unknown protocol error.\n"
			);

			/* cleanup data connection */
			ftpd_client_cleanup_data_connection(client);
			return 0;
		}

		/* update new size in vfs */
		if(client->xfer.upload) {
			/* client was uploading: set size and timestamp */
			vfs_set_size(client->xfer.element, reply->filesize);
			vfs_set_checksum(client->xfer.element, reply->checksum);
			vfs_modify(client->xfer.element, time_now());

			/* set the xfer time from the xfer structure */
			client->xfer.element->xfertime = timer(client->xfer.timestamp);
			
			/* IMPORTANT: mark the file as online before calling the onTransferSucess event */
			slave_mark_online_from(cnx, client->xfer.element);
		}

		client->xfer.xfered = reply->xfersize;
		client->xfer.checksum = reply->checksum;

		ftpd_client_text_enqueue(client->messages, "Transfer from %s is now complete\n",client->xfer.cnx->slave->name);

		if(!event_onTransferSuccess(client, &client->xfer)) {
			ftpd_client_text_enqueue(client->messages,
				"451-Transfer rejected by external policy (file will be deleted).\n"
				"451 Requested action aborted.");
			ftpd_wipe(client->xfer.element);
			client->xfer.element = NULL;
			ftpd_client_cleanup_data_connection(client);
			return 1;
		}
		
		/* Transfer is Complete */

		/* give 226 to the client */
		ftpd_client_text_enqueue(client->messages, "226 Closing data connection.");

		/* unlink the file from the data context before
			closing it because if we don't, the file will
			get deleted. */
		if(client->xfer.upload) {
			client->xfer.element->uploader = NULL;

			/* if the file was .sfv then request its infos */
			if((strlen(client->xfer.element->name) > 4) &&
				!stricmp(&client->xfer.element->name[strlen(client->xfer.element->name)-4], ".sfv")) {
				if(!make_sfv_query(client->xfer.cnx, client->xfer.element)) {
					FTPD_DBG("Could not make query for sfv file %s", client->xfer.element->name);
				}
			}
		} else {
			/* client was downloading: delete this client from the element xfer list */
			collection_delete(client->xfer.element->leechers, client);
		}
		client->xfer.element = NULL;

		ftpd_client_cleanup_data_connection(client);

		return 1;
	}

	/* protocol error */
	ftpd_client_text_enqueue(client->messages,
		"451-Unknown protocol error.\n"
		"451 Requested action aborted.\n"
	);

	/* cleanup data connection */
	ftpd_client_cleanup_data_connection(client);
	return 0;
}

struct slave_transfer_request *ftpd_transfer(
	unsigned long long int uid,
	struct vfs_element *root,
	struct vfs_element *file,
	unsigned int ip,
	unsigned short port,
	char passive,
	char upload,
	unsigned long long int restart,
	unsigned int *length
) {
	struct slave_transfer_request *data;
	char *filename;

	if(!vfs_is_child(root, file)) {
		FTPD_DBG("file is not child of root");
		return 0;
	}

	filename = vfs_get_relative_path(root, file);
	if(!filename) {
		FTPD_DBG("Memory error");
		return 0;
	}

	*length = sizeof(struct slave_transfer_request) + strlen(filename) + 1;
	data = malloc(*length);
	if(!data) {
		FTPD_DBG("Memory error");
		free(filename);
		return NULL;
	}

	data->xfer_uid = uid;
	data->ip = ip;
	data->port = port;
	data->passive = passive;
	data->upload = upload;
	data->restart = restart;
	strcpy(data->filename, filename);
	free(filename);

	return data;
}

/* ask a slave to start transfering the
	file specified by ctx->xfer.element
	the callback will give 200 or 425 to the client*/
static unsigned int make_slave_transfer_query(struct ftpd_client_ctx *client) {
	struct slave_transfer_request *data;
	struct slave_asynch_command *cmd;
	unsigned int length;
	
	FTPD_DIALOG_DBG("%I64u: Querying slave transfer", client->xfer.uid);

	data = ftpd_transfer(client->xfer.uid, client->xfer.cnx->slave->vroot, client->xfer.element,
		client->ip, client->port, client->passive, client->xfer.upload, client->xfer.restart, &length);
	if(!data) return 0;

	cmd = asynch_new(client->xfer.cnx, IO_SLAVE_TRANSFER, INFINITE, (void*)data, length, slave_transfer_query_callback, client);
	free(data);
	if(!cmd) return 0;

	client->xfer.cmd = cmd;
	client->xfer.timestamp = time_now();
	client->xfer.last_alive = time_now();

	return 1;
}

/* ask the slave to delete a file */
static unsigned int ftpd_wipe_send_delete_query(struct collection *c, struct slave_connection *cnx, struct vfs_element *element) {

	slave_delete_file(cnx, element);

	return 1;
}

/* add the file to the delete log */
static unsigned int ftpd_wipe_queue_delete_query(struct collection *c, struct slave_ctx *slave, struct vfs_element *element) {

	slave_offline_delete(slave, element, 1);

	return 1;
}

static int ftpd_wipe_child(struct collection *c, struct vfs_element *child, void *param) {

	ftpd_wipe(child);

	return 1;
}

/*
	Wipe an element from the vfs and from all slaves it's available from.
	the element may be a folder, in wich case all its childs
	will get deleted.
*/
unsigned int ftpd_wipe(struct vfs_element *element) {

	if(!element) {
		FTPD_DBG("Params error");
		return 0;
	}

	obj_ref(&element->o);
	
	//VFS_DBG("Wiping element(%08x) %s\\%s", (int)element, element->parent ? element->parent->name : "", element->name);
	
	if(element->type == VFS_FOLDER) {

		/* this is a folder, delete its childs */
		collection_iterate(element->childs, (collection_f)ftpd_wipe_child, NULL);
	} else if(element->type == VFS_FILE) {
		/* to delete a file, we will remove it from our vfs and we
			will tell the slaves to delete it aswell. if any slave cannot
			delete it, the file will just reappear the next time the
			slave is restarted (it is the easiest and fastest way). */
		collection_iterate(element->available_from, (collection_f)ftpd_wipe_send_delete_query, element);

		/* queue the delete query for all slaves from wich it is unavailable */
		collection_iterate(element->offline_from, (collection_f)ftpd_wipe_queue_delete_query, element);
	}

	/* delete the element */
	vfs_recursive_delete(element);

	obj_unref(&element->o);

	return 1;
}

/* ask the slave to delete a file */
static unsigned int ftpd_wipe_from_send_delete_query(struct collection *c, struct slave_connection *cnx, void *param) {
	struct {
		struct vfs_element *element;
		struct slave_ctx *slave;
	} *ctx = param;

	if(cnx->slave == ctx->slave) {
		slave_delete_file(cnx, ctx->element);
	}

	return 1;
}

/* add the file to the delete log */
static unsigned int ftpd_wipe_from_queue_delete_query(struct collection *c, struct slave_ctx *slave, void *param) {
	struct {
		struct vfs_element *element;
		struct slave_ctx *slave;
	} *ctx = param;

	if(slave == ctx->slave) {
		slave_offline_delete(slave, ctx->element, 1);
	}

	return 1;
}

static int ftpd_wipe_from_child(struct collection *c, struct vfs_element *child, struct slave_ctx *slave) {

	ftpd_wipe_from(child, slave);

	return 1;
}

/*
	Act just as ftpd_wipe, except it wipes only files from a specific slave.
	Files are only deleted from the vfs when they are no longer online from
	anywhere, not mirrored from anywhere and not offline from anywhere.
*/
unsigned int ftpd_wipe_from(struct vfs_element *element, struct slave_ctx *slave) {

	if(!element) return 0;

	obj_ref(&element->o);
	
	if(element->type == VFS_FOLDER) {

		/* this is a folder, delete its childs */
		collection_iterate(element->childs, (collection_f)ftpd_wipe_from_child, slave);

		/* delete the folder once it has no childs */
		if(!collection_size(element->childs)) {
			vfs_recursive_delete(element);
		}
	} else if(element->type == VFS_FILE) {
		struct {
			struct vfs_element *element;
			struct slave_ctx *slave;
		} ctx = { element, slave };
		
		collection_iterate(element->available_from, (collection_f)ftpd_wipe_from_send_delete_query, &ctx);
		
		collection_iterate(element->offline_from, (collection_f)ftpd_wipe_from_queue_delete_query, &ctx);

		if(!collection_size(element->available_from) &&
				!collection_size(element->offline_from) &&
				!collection_size(element->mirror_to)) {
			/* completely offline, delete it */
			vfs_recursive_delete(element);
		}
	}

	obj_unref(&element->o);

	return 1;
}

struct xfer_ctx *ftpd_lua_client_to_xfer(struct ftpd_client_ctx *client) {

	if(client->xfer.uid == -1) return NULL;

	return &client->xfer;
}

static unsigned int build_data_for_client(struct collection *c, struct ftpd_collectible_line *l, void *param) {
	struct {
		char *buffer;
		struct ftpd_client_ctx *client;
	} *ctx = param;

	sprintf(&((ctx->buffer)[strlen(ctx->buffer)]), "%s", l->line);

	ftpd_line_destroy(l);

	return 1;
}

int get_data_length(struct collection *c, struct ftpd_collectible_line *l, unsigned int *length) {

	*length += strlen(l->line)+1;

	return 1;
}

int ftpd_client_data_write(int fd, struct ftpd_client_ctx *client) {
	struct {
		char *buffer;
		struct ftpd_client_ctx *client;
	} ctx = { NULL, client };
	unsigned int length = 0;

	if(!client->ready) {
		return 1;
	}

	collection_iterate(client->data_ctx.data, (collection_f)get_data_length, &length);
	if(!length) {
		ftpd_client_text_enqueue(client->messages, "226 Closing data connection.");
		ftpd_client_cleanup_data_connection(client);
		return 0;
	}

	ctx.buffer = malloc(length);
	if(!ctx.buffer) {
		FTPD_DBG("Memory error (%u)", length);
		return 0;
	}
	memset(ctx.buffer, 0, length);

	collection_iterate(client->data_ctx.data, (collection_f)build_data_for_client, &ctx);

	send(fd, ctx.buffer, strlen(ctx.buffer), 0);
	free(ctx.buffer);
	ctx.buffer = NULL;

	/* shutdown the socket and wait for the graceful disconnection */
	shutdown(fd, SD_SEND);

	/* clear this callback because we're finished */
	signal_clear_with_filter(client->data_ctx.group, "socket-write", (void *)fd);

	return 1;
}

int ftpd_client_data_close(int fd, struct ftpd_client_ctx *client) {

	/* Closed gracefully */
	
	FTPD_DIALOG_DBG("Data connection closed gracefully.");

	ftpd_client_text_enqueue(client->messages, "226 Closing data connection.");
	ftpd_client_cleanup_data_connection(client);

	return 1;
}

int ftpd_client_data_error(int fd, struct ftpd_client_ctx *client) {

	/* Closed with error */
	
	FTPD_DBG("Data connection closed with error.");

	ftpd_client_text_enqueue(client->messages, "425 Can't open data connection (socket error).");
	ftpd_client_cleanup_data_connection(client);
	
	FTPD_DBG("Data connection was cleaned up.");

	return 1;
}

int ftpd_client_data_write_timeout(struct ftpd_client_ctx *client) {
	
	FTPD_DBG("Data write timed out");
	
	ftpd_client_text_enqueue(client->messages, "425 Can't open data connection (timed out).");
	ftpd_client_cleanup_data_connection(client);
	
	return 0;
}

int ftpd_client_data_connect_timeout(struct ftpd_client_ctx *client) {
	
	FTPD_DBG("Data connection timed out");
	
	ftpd_client_text_enqueue(client->messages, "425 Can't open data connection (timed out).");
	ftpd_client_cleanup_data_connection(client);
	
	return 0;
}

int ftpd_client_data_connect(int fd, struct ftpd_client_ctx *client) {
	struct signal_callback *s;

	if(client->passive) {
		/* swap sockets */
		
		FTPD_DIALOG_DBG("[%08x] Accepting connection on socket.", (int)client);

		/*if(client->data_ctx.fd != -1) {
			FTPD_DBG("ERROR! Accepting ");
		}*/
		client->data_ctx.fd = accept(fd, NULL, 0);

		signal_clear(client->data_ctx.group);

		close_socket(fd);
		socket_monitor_fd_closed(fd);

		if(client->data_ctx.fd == -1) {
			FTPD_DBG("Could not accept data connection.");
			ftpd_client_cleanup_data_connection(client);
			return 0;
		}
		socket_current++;

		/* enable linger so we'll perform hard abort on closesocket() */
		socket_linger(client->data_ctx.fd, 0);
		
		socket_monitor_new(client->data_ctx.fd, 1, 1);
		socket_monitor_signal_add(client->data_ctx.fd, client->data_ctx.group, "socket-error", (signal_f)ftpd_client_data_error, client);
		socket_monitor_signal_add(client->data_ctx.fd, client->data_ctx.group, "socket-close", (signal_f)ftpd_client_data_close, client);
	} else {
		signal_clear_with_filter(client->data_ctx.group, "socket-connect", (void *)client->data_ctx.fd);
	}

	FTPD_DIALOG_DBG("[%08x] Data connection established.", (int)client);

	/* set the correct parameters for the new socket */
	socket_set_max_read(client->data_ctx.fd, FTPD_CLIENT_DATA_SOCKET_SIZE);
	socket_set_max_write(client->data_ctx.fd, FTPD_CLIENT_DATA_SOCKET_SIZE);

	/* connect only the "write" signal, since we don't need the "read" event. */
	s = socket_monitor_signal_add(client->data_ctx.fd, client->data_ctx.group, "socket-write", (signal_f)ftpd_client_data_write, client);
	signal_timeout(s, DATA_CONNECTION_TIMEOUT, (timeout_f)ftpd_client_data_write_timeout, client);

	return 1;
}

/* reply to any command to a logged client */
static unsigned int ftpd_client_parse_logged_input(struct ftpd_client_ctx *client, ftpd_command *command, char *ptr) {
	char *Pointer;

	Pointer = strchr(ptr, ' ');
	if(Pointer) Pointer++;

	switch(*command) {
	case CMD_USERNAME:
	case CMD_PASSWORD:
		ftpd_client_text_enqueue(client->messages,
			"503-You are already logged in.\n"
			"503 Bad sequence of commands.\n"
		);

		break;
	case CMD_SYSTEM:

		FTPD_DIALOG_DBG("[%08x] CMD_SYSTEM", (int)client);

		ftpd_client_text_enqueue(client->messages, "215 UNIX Type: L8.");
		break;
	case CMD_FEATURES:
		
		FTPD_DIALOG_DBG("[%08x] CMD_FEATURES", (int)client);

		ftpd_client_text_enqueue(client->messages,
			"211-Extension supported:\n"
			" CLNT\n"
			" SIZE\n"
			" PRET\n"
			"211 End\n"
		);

		break;
	case CMD_CLIENT:

		FTPD_DIALOG_DBG("[%08x] CMD_CLIENT (%s)", (int)client, Pointer);

		/* WE DON'T CARE, but thanks anyway */
		ftpd_client_text_enqueue(client->messages, "200 Noted.");
		break;
	case CMD_SITE:
/*
		SITE <SP> <string> <CRLF>
          200
          202
          500, 501, 530
*/
		FTPD_DIALOG_DBG("[%08x] CMD_SITE (%s)", (int)client, Pointer);

		if(site_handle(client, Pointer)) {
			ftpd_client_text_enqueue(client->messages, "200-Command OK.\n");
		} else {
			ftpd_client_text_enqueue(client->messages, "500-One or more error occured.\n");
		}
		/*
		ftpd_client_text_enqueue(client->messages,
			"202-No SITE command supported yet.\n"
			"202 Command not implemented, superfluous at this site.\n"
		);
		*/

		break;

/*******************************************
 *******************************************

			BARELY SUPPORTED

*******************************************
*******************************************/
	case CMD_ACCOUNT:
		FTPD_DIALOG_DBG("[%08x] CMD_ACCOUNT (%s)", (int)client, Pointer);
		ftpd_client_text_enqueue(client->messages, 
			"202-No account needed.\n"
			"202 Command not implemented, superfluous at this site.\n"
		);

		break;
	case CMD_STRUCTURE_MOUNT:
		FTPD_DIALOG_DBG("[%08x] CMD_STRUCTURE_MOUNT (%s)", (int)client, Pointer);
		ftpd_client_text_enqueue(client->messages,
			"202-Intentionally left unsupported: coder too lazy.\n"
			"202 Command not implemented, superfluous at this site.\n"
		);

		break;
	case CMD_REINITIALIZE:
		FTPD_DIALOG_DBG("[%08x] CMD_REINITIALIZE", (int)client);
		ftpd_client_text_enqueue(client->messages,
			"502-Intentionally left unsupported: coder too lazy.\n"
			"502 Command not implemented.\n"
		);

		break;
	case CMD_STORE_UNIQUE:
		FTPD_DIALOG_DBG("[%08x] CMD_STORE_UNIQUE", (int)client);
		ftpd_client_text_enqueue(client->messages,
			"450-You must specify a filename.\n"
			"450 Requested file action not taken.\n"
		);
		break;
	case CMD_STATUS:
		FTPD_DIALOG_DBG("[%08x] CMD_STATUS (%s)", (int)client, (Pointer ? Pointer : ""));
		ftpd_client_text_enqueue(client->messages, "211 Server status is OK.");
		break;
	case CMD_HELP:
		FTPD_DIALOG_DBG("[%08x] CMD_HELP (%s)", (int)client, (Pointer ? Pointer : ""));
		/* tell the user to read the famous manual */
		ftpd_client_text_enqueue(client->messages, "214 RTFM");
		break;
	case CMD_NO_OPERATION:
		FTPD_DIALOG_DBG("[%08x] CMD_NO_OPERATION", (int)client);
		ftpd_client_text_enqueue(client->messages, "200 Command okay.");
		break;
	case CMD_ALLOCATE:
		FTPD_DIALOG_DBG("[%08x] CMD_ALLOCATE (%s)", (int)client, Pointer);
		ftpd_client_text_enqueue(client->messages,
			"202-No allocation needed. You should begin transfer right away.\n"
			"202 Command not implemented, superfluous at this site.\n"
		);
		break;


/*******************************************
 *******************************************

			DATA CONTEXT OPERATIONS

*******************************************
*******************************************/
	case CMD_REPRESENTATION_TYPE:
/*
		TYPE <SP> <type-code> <CRLF>
          200
          500, 501, 504, 421, 530
*/
		FTPD_DIALOG_DBG("[%08x] CMD_REPRESENTATION_TYPE (%s)", (int)client, Pointer);

		{
			char Type = *Pointer;

			/* set the correct data type */
			if(Type == 'A') {
				client->type = TYPE_ASCII;
				goto __type_success;
			}

			if(Type == 'I') {
				client->type = TYPE_IMAGE;
				goto __type_success;
			}

			/* Do not accept EBCDIC representation type */
			/* Do not accept Local-Byte representation type */
		}

//__type_error:
		ftpd_client_text_enqueue(client->messages, "504 Command not implemented for that parameter.");
		break;
__type_success:
		ftpd_client_text_enqueue(client->messages, "200 Command okay.");
		break;
	case CMD_FILE_STRUCTURE:
/*
		STRU <SP> <structure-code> <CRLF>
          200
          500, 501, 504, 421, 530
*/
		FTPD_DIALOG_DBG("[%08x] CMD_FILE_STRUCTURE (%s)", (int)client, Pointer);

		if(*(Pointer+1) != 0)
			goto __structure_error;

		/* File (no record structure) */
		if(*Pointer == 'F') {
			client->structure = STRUCTURE_FILE;
			goto __structure_success;
		}

		/* Record structure */
		/*if(*Pointer == 'R') {
			client->structure = STRUCTURE_RECORD;
			goto __structure_success;
		}*/
		
		/* Page structure */
		/*if(*Pointer == 'P') {
			client->structure = STRUCTURE_PAGE;
			goto __structure_success;
		}*/

__structure_error:
		ftpd_client_text_enqueue(client->messages, "504 Command not implemented for that parameter.");
		break;
__structure_success:
		ftpd_client_text_enqueue(client->messages, "200 Command okay.");
		break;
	case CMD_TRANSFER_MODE:
/*
		MODE <SP> <mode-code> <CRLF>
          200
          500, 501, 504, 421, 530
*/
		FTPD_DIALOG_DBG("[%08x] CMD_TRANSFER_MODE (%s)", (int)client, Pointer);

		if(*(Pointer+1) != 0)
			goto __mode_error;

		/* Stream */
		if(*Pointer == 'S') {
			client->mode = MODE_STREAM;
			goto __mode_success;
		}

		/* Block */
		/*if(*Pointer == 'B') {
			client->mode = MODE_BLOCK;
			goto __mode_success;
		}*/
		
		/* Compressed */
		/*if(*Pointer == 'C') {
			client->mode = MODE_COMPRESSED;
			goto __mode_success;
		}*/

__mode_error:
		ftpd_client_text_enqueue(client->messages, "504 Command not implemented for that parameter.");
		break;
__mode_success:
		ftpd_client_text_enqueue(client->messages, "200 Command okay.");
		break;

/*******************************************
 *******************************************

			DIRECTORY OPERATIONS

*******************************************
*******************************************/
	case CMD_PRINT_WORKING_DIRECTORY:
/*
		PWD <CRLF>
          257
          500, 501, 502, 421, 550
*/
		FTPD_DIALOG_DBG("[%08x] CMD_PRINT_WORKING_DIRECTORY", (int)client);

		{
			char *directory;

			directory = vfs_get_relative_path(vfs_root, client->working_directory);
			if(directory) {
				ftpd_client_text_enqueue(client->messages, "257 \"%s\" is current directory\n", directory);
				free(directory);
			} else {
				return 0;
			}
		}

		break;
	case CMD_CHANGE_WORKING_DIRECTORY:
/*
		CWD <SP> <pathname> <CRLF>
          250
          500, 501, 502, 421, 530, 550
*/
		FTPD_DIALOG_DBG("[%08x] CMD_CHANGE_WORKING_DIRECTORY (%s)", (int)client, Pointer);

		{
			struct vfs_element *newdir, *container;

			/* some dumb clients (ultrafxp ...) use "CWD ." as a kind of NOOP */
			if(!stricmp(Pointer, ".")) {

				/* Change the current command to reflect what's really going on. */
				*command = CMD_NO_OPERATION;

				if(!event_onPreChangeDir(client, client->working_directory)) {
					ftpd_client_text_enqueue(client->messages, "550-CWD rejected by external policy.\n"
												"550 Requested action not taken.");
					break;
				}

				ftpd_client_text_enqueue(client->messages, "250 Requested file action okay, completed.");
				break;
			}

			if(strchr(Pointer, '/') || strchr(Pointer, '\\'))
				container = vfs_root;
			else
				container = client->working_directory;

			newdir = vfs_find_element(container, Pointer);
			if(!newdir || (newdir->type != VFS_FOLDER)) {
				ftpd_client_text_enqueue(client->messages, "550 Requested action not taken.");
				break;
			}

			obj_ref(&newdir->o);

			if(!event_onPreChangeDir(client, newdir)) {
				ftpd_client_text_enqueue(client->messages, "550-CWD rejected by external policy.\n"
											"550 Requested action not taken.");

				obj_unref(&newdir->o);
				break;
			}

			if(!client->working_directory) {
				/* bam. */
				obj_unref(&newdir->o);
				return 0;
			}

			/* remove the client from the current working directory's browsers list */
			collection_delete(client->working_directory->browsers, client);
			client->working_directory = NULL;

			/* add the client to the newdir's browsers list */
			if(!collection_add(newdir->browsers, client)) {
				FTPD_DBG("Collection error");
				obj_unref(&newdir->o);
				return 0;
			}
			client->working_directory = newdir;

			ftpd_client_text_enqueue(client->messages, "250 Requested file action okay, completed.");
			event_onChangeDir(client, client->working_directory);
			obj_unref(&newdir->o);
		}

		break;
	case CMD_CHANGE_TO_PARENT_DIRECTORY:
/*
       CDUP <CRLF>
          200
          500, 501, 502, 421, 530, 550
*/
		FTPD_DIALOG_DBG("[%08x] CMD_CHANGE_TO_PARENT_DIRECTORY", (int)client);

		if(client->working_directory->parent) {
			if(!event_onPreChangeDir(client, client->working_directory->parent)) {
				ftpd_client_text_enqueue(client->messages, "550-CWD rejected by external policy.\n"
											"550 Requested action not taken.");
				break;
			}

			/* remove the client from the current working directory's browsers list */
			collection_delete(client->working_directory->browsers, client);

			if(!collection_add(client->working_directory->parent->browsers, client)) {
				client->working_directory = NULL;
				return 0;
			}

			client->working_directory = client->working_directory->parent;
		}

		ftpd_client_text_enqueue(client->messages, "250 Requested file action okay, completed.");

		event_onChangeDir(client, client->working_directory);

		break;
	case CMD_REMOVE_DIRECTORY:
/*
		RMD  <SP> <pathname> <CRLF>
          250
          500, 501, 502, 421, 530, 550
*/
		FTPD_DIALOG_DBG("[%08x] CMD_REMOVE_DIRECTORY (%s)", (int)client, Pointer);

		{
			struct vfs_element *container, *element;

			if(strchr(Pointer, '/') || strchr(Pointer, '\\'))
				container = vfs_root;
			else
				container = client->working_directory;

			element = vfs_find_element(container, Pointer);
			if(!element) {
				ftpd_client_text_enqueue(client->messages, "550-Could not find directory.");
				ftpd_client_text_enqueue(client->messages, "550 Requested action not taken.");
				break;
			}

			if(element->type != VFS_FOLDER) {
				ftpd_client_text_enqueue(client->messages, "550-This is not a directory.");
				ftpd_client_text_enqueue(client->messages, "550 Requested action not taken.");
				break;
			}

			if(collection_size(element->childs)) {
				ftpd_client_text_enqueue(client->messages, "550-This directory is not empty.");
				ftpd_client_text_enqueue(client->messages, "550 Requested action not taken.");
				break;
			}

			obj_ref(&element->o);

			if(!event_onPreRemoveDir(client, element)) {
				ftpd_client_text_enqueue(client->messages,
					"550-RMD rejected by external policy.\n"
					"550 Requested action not taken."
				);

				obj_unref(&element->o);
				break;
			}

			event_onRemoveDir(client, element);

			if(!vfs_recursive_delete(element)) {
				ftpd_client_text_enqueue(client->messages, "550 Requested action not taken.");
			} else {
				ftpd_client_text_enqueue(client->messages, "250 Requested file action okay, completed.");
			}
			

			obj_unref(&element->o);
		}

		break;
	case CMD_MAKE_DIRECTORY:
/*
		MKD  <SP> <pathname> <CRLF>
          257
          500, 501, 502, 421, 530, 550
*/
		FTPD_DIALOG_DBG("[%08x] CMD_MAKE_DIRECTORY (%s)", (int)client, Pointer);

		{
			struct vfs_element *newdir, *container;

			if(strchr(Pointer, '/') || strchr(Pointer, '\\'))
				container = vfs_root;
			else
				container = client->working_directory;

			newdir = vfs_find_element(container, Pointer);
			if(newdir) {
				ftpd_client_text_enqueue(client->messages, "550-Directory already exist.");
				ftpd_client_text_enqueue(client->messages, "550 Requested action not taken.");
				break;
			}

			/*
				The client may create a whole tree all at
				once. So Here we've got to create each
				directories separately because of the
				possible filters that may reject thier
				creation, so we'll have to call the
				onPreMakeDir and onMakeDir events on
				every of them.
			*/
			{
				char *trimmedname;
				char *ptr, *next;

				trimmedname = vfs_trim_name(Pointer);
				if(!trimmedname) {
					ftpd_client_text_enqueue(client->messages, "550 Requested action not taken.");
					break;
				}

				ptr = trimmedname;
				do {
					next = strchr(ptr, '/');
					if(next) {
						*next = 0;
						next++;
					}

					newdir = vfs_find_element(container, ptr);
					if(!newdir) {

						/* Directory does not exist, process with onPreMakeDir and onMakeDir */

						newdir = vfs_create_folder(container, ptr, client->username);
						if(!newdir) {
							ftpd_client_text_enqueue(client->messages, "550 Requested action not taken.");
							break;
						} else {
							obj_ref(&newdir->o);

							nuke_check(newdir);

							if(!event_onPreMakeDir(client, newdir)) {
								ftpd_client_text_enqueue(client->messages,
									"550-MKD rejected by external policy.\n"
									"550 Requested action not taken.\n"
								);
								vfs_recursive_delete(newdir);
								obj_unref(&newdir->o);
								break;
							}
							vfs_modify(newdir, time_now());
							ftpd_client_text_enqueue(client->messages, "250 Requested file action okay, completed.");
							event_onMakeDir(client, newdir);

							obj_unref(&newdir->o);
						}
					}
					container = newdir;

					ptr = next;
				} while(next);

				free(trimmedname);
				trimmedname = NULL;
			}
		}

		break;
	case CMD_RENAME_FROM:
/*
		RNFR <SP> <pathname> <CRLF>
          450, 550
          500, 501, 502, 421, 530
          350
*/
		FTPD_DIALOG_DBG("[%08x] CMD_RENAME_FROM (%s)", (int)client, Pointer);

		ftpd_client_text_enqueue(client->messages, "550-You should not move that.");
		ftpd_client_text_enqueue(client->messages, "550 Requested action not taken.");
		break;
	case CMD_RENAME_TO:
/*
		RNTO <SP> <pathname> <CRLF>
          250
          532, 553
          500, 501, 502, 503, 421, 530
*/
		FTPD_DIALOG_DBG("[%08x] CMD_RENAME_TO (%s)", (int)client, Pointer);

		ftpd_client_text_enqueue(client->messages, "550-You should not move that.");
		ftpd_client_text_enqueue(client->messages, "550 Requested action not taken.");

		break;


/*******************************************
 *******************************************

			FILE OPERATIONS

*******************************************
*******************************************/
	case CMD_SIZE_OF_FILE:

		FTPD_DIALOG_DBG("[%08x] CMD_SIZE_OF_FILE (%s)", (int)client, Pointer);

		{
			struct vfs_element *container, *element;

			/* setup the target directory, and check if it exists */
			if(strchr(Pointer, '/') || strchr(Pointer, '\\'))
				container = vfs_root; /* relative to root */
			else
				container = client->working_directory; /* relative to current */

			element = vfs_find_element(container, Pointer);
			if(!element || element->type != VFS_FILE) {
				ftpd_client_text_enqueue(client->messages,
					"550 No such file.\n"
				);
				break;
			}

			ftpd_client_text_enqueue(client->messages, "213 %I64u\n", element->size);
		}

		break;
	case CMD_RESTART:
/*
		REST <SP> <marker> <CRLF>
          500, 501, 502, 421, 530
          350
*/
		FTPD_DIALOG_DBG("[%08x] CMD_RESTART (%s)", (int)client, Pointer);

		{
			/* the client must issue a RETR command after this one */
			unsigned long long int i;

			i = strtoull(Pointer, NULL, 10);

			client->xfer.restart = i;

			ftpd_client_text_enqueue(client->messages, "350 Requested file action pending further information.");
		}

		break;
	case CMD_PRE_TRANSFER:
/*
		PRET LIST [ignored arguments]			not needed
		PRET NLST [ignored arguments]			not needed
		PRET RETR <file> [ignored arguments]
		PRET APPE <file> [ignored arguments]
		PRET STOR <file> [ignored arguments]
		PRET STOU [ignored arguments]			not supported
*/
		FTPD_DIALOG_DBG("[%08x] CMD_PRE_TRANSFER (%s)", (int)client, Pointer);

		{
			struct vfs_element *container;

			/* cleanup the xfer struct, just in case */
			ftpd_client_cleanup_data_connection(client);

			/* setup the target directory, and check if file exists */
			if(strchr(Pointer, '/') || strchr(Pointer, '\\'))
				container = vfs_root; /* relative to root */
			else
				container = client->working_directory; /* relative to current */

			/* process the command, depending ... */
			if(!strnicmp(Pointer, "LIST", 4) || !strnicmp(Pointer, "NLST", 4)) {
				/* not needed but reply something gentle anyway */
				ftpd_client_text_enqueue(client->messages,
					"200 OK, next transfer will be from master.\n"
				);
				break;
			} else if(!strnicmp(Pointer, "APPE", 4)) {
				ftpd_client_text_enqueue(client->messages,
					"550 Requested action not taken. APPE is not acceptable.\n"
				);
				break;
			} else if(!strnicmp(Pointer, "RETR", 4)) {
				/* file MUST EXIST and MUST BE AVAILABLE from ONE OR MORE slave */

				/* skip the first param */
				Pointer = strchr(Pointer,' ');
				if(!Pointer) {
					ftpd_client_text_enqueue(client->messages,
						"503-Bad sequence of commands. You should use PRET <RETR/STOR> <file>\n"
					);
					break;
				}
				Pointer++;

				/* fill the struct xfer_client assigned to the client */
				if(!setup_file_download(client, container, Pointer)) {
					ftpd_client_text_enqueue(client->messages,
						"550 Requested action not taken. File operation error occured.\n"
					);
					break;
				}

				client->slave_xfer = 1;

				/* give 200 to the client */
				ftpd_client_text_enqueue(client->messages,
					"200 OK, next transfer will be from %s.\n", client->xfer.cnx->slave->name
				);

				//FTPD_DIALOG_DBG("[%08x] 4", (int)client);
			} else if(!strnicmp(Pointer, "STOR", 4)) {
				/* file must NOT EXIST and ONE OR MORE slave must be available  */

				/* skip the first param */
				Pointer = strchr(Pointer,' ');
				if(!Pointer) {
					ftpd_client_text_enqueue(client->messages,
						"503 Bad sequence of commands. You should use PRET <RETR/STOR> <file>\n"
					);
					break;
				}
				Pointer++;

				/* fill the struct xfer_client assigned to the client */
				if(!setup_file_upload(client, container, Pointer)) {
					ftpd_client_text_enqueue(client->messages,
						"550 Requested action not taken. File operation error occured.\n"
					);
					break;
				}

				client->slave_xfer = 1;

				/* give 200 to the client */
				ftpd_client_text_enqueue(client->messages,
					"200 OK, next transfer will be from %s.\n", client->xfer.cnx->slave->name
				);
			} else {
				/* unknown PRET command */
				ftpd_client_text_enqueue(client->messages,
					"503 Bad sequence of commands. Supported arguments for PRET are RETR/STOR.\n"
				);
				break;
			}
		}

		break;
	case CMD_PASSIVE:
	{
		unsigned short port = 0;
		unsigned int ip = 0;
/*
		PASV <CRLF>
          227
          500, 501, 502, 421, 530
*/
		FTPD_DIALOG_DBG("[%08x] CMD_PASSIVE", (int)client);

		/*
			Some FTP clients are sending multiple PASV in a row, or sometimes
			a combination of PASV/PORTs without sending ABOR between, so we
			must check that.
		*/
		
		if(client->data_ctx.fd != -1) {
			FTPD_DBG("Broken FTP client, tries to open multiple connections...");
			ftpd_client_text_enqueue(client->messages, "227 Broken FTP client.\n");
			ftpd_client_cleanup_data_connection(client);
		}

		if(client->last_command == CMD_PASSIVE) {
			FTPD_DBG("Client sent CMD_PASSIVE fucking twice!");
			ftpd_client_text_enqueue(client->messages, "227 Your broken FTP client sent PASV twice in a row.\n");
		}

		if(client->slave_xfer) {
			
			/* we are about to open data connection
				for RETR/STOR on a slave */

			client->passive = 1;

			/* tell the chosen slave to listen */
			/* the callback will give its answer to the client */
			/* the callback will set client->xfer.ready */
			if(!make_slave_listen_query(client)) {
				ftpd_client_text_enqueue(client->messages,
					"425-Internal memory error.\n"
					"425 Can't open data connection.\n"
				);
			}
		} else {
			/*
				we are about to open data connection,
					1. for a LIST command
					 OR
					2. the client doesn't handle PRET.
			*/

			/* cleanup to be sure. BOGUS WITH FtpRush */
			//ftpd_client_cleanup_data_connection(client);

			if(client->data_ctx.fd != -1) {
				/*
					Some clients seems to send CMD_PASSIVE twice,
					I cannot figure a good goddamn reason why ...
				*/
				FTPD_DBG("ERROR: fucking socket is not -1!");
				ftpd_client_text_enqueue(client->messages, "227 Your broken client fucking sent PASV twice in a row.\n");
				ftpd_client_text_enqueue(client->messages, "425 Can't open data connection.");
				ftpd_client_cleanup_data_connection(client);
				return 1;
			}

			client->passive = 1;

			ip = socket_local_address(client->fd);
			port = ftpd_get_next_data_port();

			ftpd_client_text_enqueue(client->messages, "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u).\n",
				(ip) & 0xff,
				(ip >> 8) & 0xff,
				(ip >> 16) & 0xff,
				(ip >> 24) & 0xff,
				(port >> 8) & 0xff,
				(port) & 0xff
			);

			/* create the socket and start listening */
			client->data_ctx.fd = create_listening_socket(port);

			if(client->data_ctx.fd == -1) {
				FTPD_DBG("Could not create listening socket for data connection");
				ftpd_client_text_enqueue(client->messages, "425 Can't open data connection.");
				ftpd_client_cleanup_data_connection(client);
				break;
			}

			/* hook all signals to the newly created socket */
			socket_monitor_new(client->data_ctx.fd, 0, 1);
			{
				struct signal_callback *s;
				
				s = socket_monitor_signal_add(client->data_ctx.fd, client->data_ctx.group, "socket-connect", (signal_f)ftpd_client_data_connect, client);
				signal_timeout(s, DATA_CONNECTION_TIMEOUT, (timeout_f)ftpd_client_data_connect_timeout, client);
				
				socket_monitor_signal_add(client->data_ctx.fd, client->data_ctx.group, "socket-error",
					(signal_f)ftpd_client_data_error, client);
				socket_monitor_signal_add(client->data_ctx.fd, client->data_ctx.group, "socket-close",
					(signal_f)ftpd_client_data_close, client);
			}

			FTPD_DIALOG_DBG("Now listening ...");
		}

		break;
	}
	case CMD_DATA_PORT:
/*
		PORT <SP> <host-port> <CRLF>
          200
          500, 501, 421, 530
*/
		FTPD_DIALOG_DBG("[%08x] CMD_DATA_PORT (%s)", (int)client, Pointer);

		/*
			Some FTP clients are sending multiple PASV in a row, or sometimes
			a combination of PASV/PORTs without sending ABOR between, so we
			must check that.
		*/

		if(client->data_ctx.fd != -1) {
			FTPD_DBG("Broken FTP client, tries to open multiple connections...");
			ftpd_client_text_enqueue(client->messages, "227 Broken FTP client.\n");
			ftpd_client_cleanup_data_connection(client);
		}

		{
			unsigned char h1, h2, h3, h4;
			unsigned char p1, p2;

			unsigned long long int i;
			char *v = Pointer;

			v = strchr(Pointer, ',');
			if(!v) goto __port_error;
			*v = 0;
			v++;
			i = strtoull(Pointer, NULL, 10);
			Pointer = v;
			if(i > 255) goto __port_error;
			h1 = (char)(i & 0xff);

			v = strchr(Pointer, ',');
			if(!v) goto __port_error;
			*v = 0;
			v++;
			i = strtoull(Pointer, NULL, 10);
			Pointer = v;
			if(i > 255) goto __port_error;
			h2 = (char)(i & 0xff);

			v = strchr(Pointer, ',');
			if(!v) goto __port_error;
			*v = 0;
			v++;
			i = strtoull(Pointer, NULL, 10);
			Pointer = v;
			if(i > 255) goto __port_error;
			h3 = (char)(i & 0xff);

			v = strchr(Pointer, ',');
			if(!v) goto __port_error;
			*v = 0;
			i = strtoull(Pointer, NULL, 10);
			v++;
			Pointer = v;
			if(i > 255) goto __port_error;
			h4 = (char)(i & 0xff);

			v = strchr(Pointer, ',');
			if(!v) goto __port_error;
			*v = 0;
			v++;
			i = strtoull(Pointer, NULL, 10);
			Pointer = v;
			if(i > 255) goto __port_error;
			p1 = (char)(i & 0xff);

			i = strtoull(Pointer, NULL, 10);
			if(i > 255) goto __port_error;
			p2 = (char)(i & 0xff);
			
			/* BOGUS WITH FtpRush */
			//ftpd_client_cleanup_data_connection(client);

			client->passive = 0;

			client->ip  = (h1);
			client->ip |= (h2 << 8);
			client->ip |= (h3 << 16);
			client->ip |= (h4 << 24);

			client->port  = (p2);
			client->port |= (p1 << 8);

			ftpd_client_text_enqueue(client->messages,
				"200-Will use data host %u.%u.%u.%u on port %u\n"
				"200 Command okay.\n",
				(client->ip) & 0xff,
				(client->ip >> 8) & 0xff,
				(client->ip >> 16) & 0xff,
				(client->ip >> 24) & 0xff,
				client->port
			);
			break;
		}
__port_error:
		ftpd_client_text_enqueue(client->messages, "501 Syntax error in parameters or arguments.");
		break;
	case CMD_LIST:
	{
		struct vfs_element *container, *element;
/*
		LIST [<SP> <pathname>] <CRLF>
          125, 150
             226, 250
             425, 426, 451
          450
          500, 501, 502, 421, 530
*/
		FTPD_DIALOG_DBG("[%08x] CMD_LIST (%s)", (int)client, (Pointer ? Pointer : ""));

		if((client->last_command != CMD_DATA_PORT) && (client->last_command != CMD_PASSIVE)) {
			ftpd_client_text_enqueue(client->messages,
				"503-You should have done PORT/PASV before LIST.\n"
				"503 Bad sequence of commands.\n"
			);
			ftpd_client_cleanup_data_connection(client);
			break;
		}

		if(Pointer && *Pointer == '-') {
			/* skip any parameters */
			Pointer = strchr(Pointer,' ');
			if(Pointer) Pointer++;
		}
		
		/* if the pathname parameter is given, it can be absolute
		   or relative to the current user's working directory */

		/* setup the target directory, and check if it exists */
		if(Pointer) {
			if(strchr(Pointer, '/') || strchr(Pointer, '\\'))
				container = vfs_root; /* relative to root */
			else
				container = client->working_directory; /* relative to current */

			element = vfs_find_element(container, Pointer);
			if(!element || (element->type != VFS_FOLDER)) {
				ftpd_client_text_enqueue(client->messages,
					"450-Target directory does not exist.\n"
					"450 Requested file action not taken.\n"
				);
				/* cleanup data context */
				ftpd_client_cleanup_data_connection(client);
				break;
			}
		} else element = client->working_directory;

		/* process with the actual listing */
		if(!client->passive) {
			if(client->data_ctx.fd != -1) {
				FTPD_DBG("ERROR: fucking socket not -1");
				ftpd_client_text_enqueue(client->messages, "425 You have a fucking broken FTP client.");
				ftpd_client_text_enqueue(client->messages, "425 Can't open data connection.");
				ftpd_client_cleanup_data_connection(client);
				break;
			}

			/* establish a connection to the host */
			client->data_ctx.fd = connect_to_ip_non_blocking(client->ip, client->port);

			if(client->data_ctx.fd == -1) {
				ftpd_client_text_enqueue(client->messages, "425 Can't open data connection.");
				ftpd_client_cleanup_data_connection(client);
				break;
			}

			/* hook all signals to the newly created socket */
			socket_monitor_new(client->data_ctx.fd, 0, 0);
			{
				struct signal_callback *s;
				
				s = socket_monitor_signal_add(client->data_ctx.fd, client->data_ctx.group, "socket-connect", (signal_f)ftpd_client_data_connect, client);
				signal_timeout(s, DATA_CONNECTION_TIMEOUT, (timeout_f)ftpd_client_data_connect_timeout, client);
				
				socket_monitor_signal_add(client->data_ctx.fd, client->data_ctx.group, "socket-error",
					(signal_f)ftpd_client_data_error, client);
				socket_monitor_signal_add(client->data_ctx.fd, client->data_ctx.group, "socket-close",
					(signal_f)ftpd_client_data_close, client);
			}
			
			ftpd_client_text_enqueue(client->messages, "150 File status okay; about to open data connection.");
		}
		else {
			ftpd_client_text_enqueue(client->messages, "125 Data connection already open; transfer starting.");
		}

		/* store the whole list to be transfered */
		collection_iterate(element->childs, (collection_f)ftpd_client_make_directory_listing, client);

		/*
			the onList event is called so any script can
			inject files & folders in the directory listing
		*/
		event_onList(client, element);

		client->ready = 1;

		break;
	}
	case CMD_NAME_LIST:
/*
		NLST [<SP> <pathname>] <CRLF>
          125, 150
             226, 250
             425, 426, 451
          450
          500, 501, 502, 421, 530
*/
		FTPD_DIALOG_DBG("[%08x] CMD_NAME_LIST (%s)", (int)client, (Pointer ? Pointer : ""));

		ftpd_client_text_enqueue(client->messages,
			"425 NLST is not supported.\n"
		);

		ftpd_client_cleanup_data_connection(client);

		break;
	case CMD_RETRIEVE:
/*
		RETR <SP> <pathname> <CRLF>
          125, 150
             (110)
             226, 250
             425, 426, 451
          450, 550
          500, 501, 421, 530
*/
		FTPD_DIALOG_DBG("[%08x] CMD_RETRIEVE (%s)", (int)client, Pointer);

		if((client->last_command != CMD_DATA_PORT) && (client->last_command != CMD_PASSIVE) && (client->last_command != CMD_RESTART))
			goto __retr_error;

		if(client->passive && !client->slave_xfer) {
			/* can't use RETR if PRET was not done */
			ftpd_client_text_enqueue(client->messages,
				"425-You should have done PRET before STOR.\n"
				"425-******************************************\n"
				"425-**** If your client does not support\n"
				"425-**** PRET, you may disable the use\n"
				"425-**** of passive mode for file transfer\n"
				"425-******************************************");
			goto __retr_error;
		}

		{
			struct vfs_element *container;

			/* setup the target directory, and check if it exists */
			if(Pointer) {
				if(strchr(Pointer, '/') || strchr(Pointer, '\\'))
					container = vfs_root; /* relative to root */
				else
					container = client->working_directory; /* relative to current */
			} else container = client->working_directory;

			if(client->passive) {
				if(!client->ready) {
					/* TODO: send "hard abort" to the slave */
					FTPD_DBG("Data connection was not ready with passive connection");
					goto __retr_error;
				}
			} else if(!client->xfer.element) {
				/* check for the file validity and availability */
				if(!setup_file_download(client, container, Pointer)) {
					FTPD_DBG("Could not setup file download");
					goto __retr_error;
				}
			}

			/* tell the slave to start sending data */
			if(!make_slave_transfer_query(client)) {
				FTPD_DBG("Could not make the slave transfer query");
				ftpd_client_text_enqueue(client->messages, "Could not make the transfer query to the slave\n", Pointer);
				goto __retr_error;
			}

			/*if(client->passive)
				ftpd_client_text_enqueue(client->messages, "125 Data connection already open; transfer starting.");
			else*/
				ftpd_client_text_enqueue(client->messages, "150 Opening ASCII mode data connection for %s from %s\r\n", Pointer, client->xfer.cnx->slave->name);
		}

		break;
__retr_error:
		ftpd_client_text_enqueue(client->messages, "425 Can't open data connection.");
		ftpd_client_cleanup_data_connection(client);
		break;
	case CMD_STORE:
/*
		STOR <SP> <pathname> <CRLF>
          125, 150
             (110)
             226, 250
             425, 426, 451, 551, 552
          532, 450, 452, 553
          500, 501, 421, 530
*/
		FTPD_DIALOG_DBG("[%08x] CMD_STORE (%s)", (int)client, Pointer);

		/*if((client->last_command != CMD_DATA_PORT) && (client->last_command != CMD_PASSIVE))
			goto __stor_error;*/

		if(client->passive && !client->slave_xfer) {
			/* can't use STOR if PRET was not done */
			ftpd_client_text_enqueue(client->messages,
				"425-You should have done PRET before STOR.\n"
				"425-******************************************\n"
				"425-**** If your client does not support  ****\n"
				"425-**** PRET, you may disable the use of ****\n"
				"425-**** passive mode for file transfer   ****\n"
				"425-******************************************\n"
			);
			goto __stor_error;
		}

		{
			struct vfs_element *container;

			/* setup the target directory, and check if it exists */
			if(Pointer) {
				if(strchr(Pointer, '/') || strchr(Pointer, '\\'))
					container = vfs_root; /* relative to root */
				else
					container = client->working_directory; /* relative to current */
			} else container = client->working_directory;

			if(client->passive) {
				/* is the slave is already listening ? */
				if(!client->ready) {
					/* TODO: send "hard abort" to the slave */
					ftpd_client_text_enqueue(client->messages, "Already ready.");
					goto __stor_error;
				}
			} else if(!client->xfer.element) { /* rushftp do PRET STOR even on non-pasv transfers */
				if(!setup_file_upload(client, container, Pointer)) {
					ftpd_client_text_enqueue(client->messages, "Cannot setup file for upload.");
					goto __stor_error;
				}
			}

			/* tell the slave to start receiving data */
			if(!make_slave_transfer_query(client)) {
				ftpd_client_text_enqueue(client->messages, "Cannot make slave transfer query.");
				goto __stor_error;
			}

			/*if(client->passive)
				ftpd_client_text_enqueue(client->messages, "125 Data connection already open; transfer starting.");
			else*/
				ftpd_client_text_enqueue(client->messages, "150 Opening ASCII mode data connection for %s on %s\r\n", Pointer, client->xfer.cnx->slave->name);
		}

		break;
__stor_error:
		ftpd_client_text_enqueue(client->messages, "425 Can't open data connection.");
		ftpd_client_cleanup_data_connection(client);
		break;
	case CMD_APPEND:
		FTPD_DIALOG_DBG("[%08x] CMD_APPEND (%s)", (int)client, Pointer);

		ftpd_client_cleanup_data_connection(client);
		/* intentionally left unsupported because files
			may be served by more than one slave */
		ftpd_client_text_enqueue(client->messages, "502 Command not implemented. You should not use APPE.");
		break;
	case CMD_DELETE:
/*
		DELE <SP> <pathname> <CRLF>
          250
          450, 550
          500, 501, 502, 421, 530
*/
		FTPD_DIALOG_DBG("[%08x] CMD_DELETE (%s)", (int)client, Pointer);

		{
			struct vfs_element *container, *element;

			/* setup the target directory, and check if it exists */
			if(strchr(Pointer, '/') || strchr(Pointer, '\\'))
				container = vfs_root; /* relative to root */
			else
				container = client->working_directory; /* relative to current */

			element = vfs_find_element(container, Pointer);
			if(!element || element->type != VFS_FILE) {
				ftpd_client_text_enqueue(client->messages,
					"550 No such file.\n"
				);
				break;
			}
			
			obj_ref(&element->o);

			/* give a chance to cancel the download */
			if(!event_onPreDelete(client, element)) {
				ftpd_client_text_enqueue(client->messages,
					"450-DELE rejected by external policy (file still exist).\n"
					"450 Requested file action not taken.\n"
				);
				obj_unref(&element->o);
				break;
			}

			/* if the file is .sfv, then clean the parent's sfv structure */
			/* this method does not handle the case where many sfv are in the same folder,
				it will remove the whole sfv */
			if((strlen(element->name) > 4) && !stricmp(&element->name[strlen(element->name)-4], ".sfv")) {
				sfv_delete(element->parent->sfv);
				element->parent->sfv = NULL;
			}

			/* TODO: advertise the user who was uploading/downloading, if any */

			/* advertise the deletion of the file */
			event_onDelete(client, element);

			/* call the ftpd deletion function to complete the operation */
			ftpd_wipe(element);

			ftpd_client_text_enqueue(client->messages, "250 Requested file action okay, completed.");
			
			obj_unref(&element->o);
		}


		break;
	case CMD_ABORT:
/*
		ABOR <CRLF>
          225, 226
          500, 501, 502, 421
*/
		FTPD_DIALOG_DBG("[%08x] CMD_ABORT", (int)client);

		/* send "hard abort" to the slave */
		/* the slave should stop transfering soon */
		
		ftpd_client_cleanup_data_connection(client);

		ftpd_client_text_enqueue(client->messages, "226 ABOR command successful.");

		break;
	case CMD_BROKEN_ABORT:
/*
		ABOR <CRLF>
          225, 226
          500, 501, 502, 421
*/
		FTPD_DIALOG_DBG("[%08x] CMD_BROKEN_ABORT", (int)client);

		/* send "hard abort" to the slave */
		/* the slave should stop transfering soon */
		
		ftpd_client_cleanup_data_connection(client);

		ftpd_client_text_enqueue(client->messages, "226 Broken FTP client.");
		ftpd_client_text_enqueue(client->messages, "226 ABOR command successful.");

		break;
	case CMD_QUIT:
/*
		QUIT <CRLF>
          221
          500
*/
		FTPD_DIALOG_DBG("[%08x] CMD_QUIT", (int)client);
		ftpd_client_text_enqueue(client->messages, "221 Service closing control connection.");

		/* return an error so we get disconnected the proper way */
		return 0;
	default:
		/* if the client is logged, send a "unknown command" message */
		
		FTPD_DBG("Unresolved: \"%s\"", ptr);
		ftpd_client_text_enqueue(client->messages, "502 Command not implemented \"%s\"", ptr);
		
		/* Whatever. Cleanup this shitty client's data connection. */
		ftpd_client_cleanup_data_connection(client);
		
		break;
	}

	return 1;
}

/* process input from client */
/* ctx->iobuf must be filled with at least one line */
static unsigned int ftpd_client_process_input(struct ftpd_client_ctx *client) {
	ftpd_command command;
	unsigned int ret = 1, i;
	char *ptr, *next_ptr;

	ptr = client->iobuf;

	/* process line by line */
	while(strchr(ptr, '\n')) {
		next_ptr = strchr(ptr, '\n');
		*next_ptr = 0;
		next_ptr++;

		for(i=0;i<strlen(ptr);i++) {
			if(ptr[i] == '\r') ptr[i] = 0;
		}

		if(!strlen(ptr)) {
			ptr = next_ptr;
			continue;
		}

		/* translate the text command into a command number */
		command = ftpd_client_text_to_command(ptr, strlen(ptr));

		if(!client->logged) {
			/* parse the input if the client is not logged */
			ret = ftpd_client_parse_unlogged_input(client, &command, ptr);
		} else {
			/* parse the input if the client is logged */
			ret = ftpd_client_parse_logged_input(client, &command, ptr);
		}

		if(!obj_isvalid(&client->o)) {
			/* deleted during processing the command ... */
			return 0;
		}

		/* store the last command's number */
		client->last_command = command;

		if(!ret) break;

		ptr = next_ptr;
	}

	/* move the rest of the buffer to the beginning */
	memmove(client->iobuf, ptr, strlen(ptr));
	client->filledsize = strlen(ptr);
	client->iobuf[client->filledsize] = 0;

	return ret;
}

const char *client_address(struct ftpd_client_ctx *client) {

	if(!client) return "unknown";

	return socket_formated_peer_address(client->fd);
}

static void ftpd_client_obj_destroy(struct ftpd_client_ctx *client) {
	
	collectible_destroy(client);

	/* free the data context */
	ftpd_client_cleanup_data_connection(client);

	/* call the event_onClientDisconnect event iif the client was logged */
	if(client->user && client->logged) {
		event_onClientDisconnect(client);
	}

	if(client->volatile_config) {
		config_destroy(client->volatile_config);
		client->volatile_config = NULL;
	}

	/*if(client->user) {
		collection_delete(client->user->clients, client);*/
		client->user = NULL;
	//}

	/* free the message collection,
		this is freed after the data context because some callback
		functions may still need to add messages */
	if(client->messages) {
		/*while(collection_size(client->messages)) {
			void *first = collection_first(client->messages);
			free(first);
			collection_delete(client->messages, first);
		}*/
		collection_destroy(client->messages);
		client->messages = NULL;
	}

	/* cleanup_data_connection frees the content of this collection */
	if(client->data_ctx.data) {
		collection_destroy(client->data_ctx.data);
		client->data_ctx.data = NULL;
	}

	if(client->data_ctx.group) {
		signal_clear(client->data_ctx.group);
		collection_destroy(client->data_ctx.group);
		client->data_ctx.group = NULL;
	}

	if(client->group) {
		signal_clear(client->group);
		collection_destroy(client->group);
		client->group = NULL;
	}

	/* free the client's socket */
	if(client->fd != -1) {
		socket_monitor_fd_closed(client->fd);
		close_socket(client->fd);
		client->fd = -1;
	}

	/* free the io buffer */
	free(client->iobuf);
	client->iobuf = NULL;

	/* delete the collection entry */
	//collection_delete(clients, client);

	/* free the client context */
	free(client);

	return;
}

/* close the communication between a client and the master */
void ftpd_client_destroy(struct ftpd_client_ctx *client) {
	
	collection_void(client->data_ctx.group);
	collection_void(client->data_ctx.data);
	collection_void(client->messages);
	collection_void(client->group);

	obj_destroy(&client->o);

	return;
}

int ftpd_client_close(int fd, struct ftpd_client_ctx *client) {

	FTPD_DIALOG_DBG("Client connection closed with %s", socket_formated_peer_address(fd));

	ftpd_client_destroy(client);

	return 1;
}

int ftpd_client_error(int fd, struct ftpd_client_ctx *client) {

	if(!client->connected) {
		FTPD_DBG("Client connection error");

		ftpd_client_destroy(client);
	} else {

		/*
			Connection was made, so this is a shitty client sending
			Out Of Band data. (ahem, FlashFXP.)
		*/

		ftpd_client_text_enqueue(client->messages, "Your FTP client is SHIT.");
		
		FTPD_DBG("Client's FTP client is SHIT. Sending OOB data.");

		FTPD_DBG("There's %u bytes available.", socket_avail(fd));

		{
			char buffer[128];
			int ret;

			do {
				ret = recv(fd, buffer, sizeof(buffer), MSG_OOB);
				FTPD_DBG("There was %d bytes of OOB data received.", ret);
			} while(ret > 0);
		}

		/* Reset the data connection just for the trouble. */
		ftpd_client_cleanup_data_connection(client);
	}

	return 1;
}

int ftpd_client_read_timeout(struct ftpd_client_ctx *client) {
	unsigned long long int time;

	time = timer(client->last_timestamp);

	if(time > FTPD_CLIENT_TIMEOUT) {
		FTPD_DIALOG_DBG("Client was disconnected (read timeout)");
		ftpd_client_destroy(client);
		return 0;
	} else {
		//FTPD_DBG("Client read timeout but only %I64u elapsed.", time);
	}
	
	return 1;
}

int ftpd_client_read(int fd, struct ftpd_client_ctx *client) {
	int avail, read;
	unsigned int ret;

	avail = socket_avail(fd);

	if(avail > (client->buffersize - client->filledsize)-1)
		avail = (client->buffersize - client->filledsize)-1;

	read = recv(fd, &client->iobuf[client->filledsize], avail, 0);
	if(read <= 0) {
		FTPD_DBG("read error (%d) for %u bytes", read, avail);
		ftpd_client_destroy(client);
		return 0;
	}

	client->filledsize += read;

	/* make sure we're zero-terminated */
	client->iobuf[client->filledsize] = 0;

	obj_ref(&client->o);
	/*
		This is a protected call. If the client is deleted
		during the processing of this function, it'll not
		crash the program
	*/
	ret = ftpd_client_process_input(client);

	if(!ret) {
		FTPD_DBG("Client disconnected because of what he sent at %s.", socket_formated_peer_address(fd));

		ftpd_client_destroy(client);
		obj_unref(&client->o);
		return 0;
	}

	if(client->last_command != CMD_NO_OPERATION) {
		client->last_timestamp = time_now();
	}

	/* Check the timeout on client connection */
	ftpd_client_read_timeout(client);

	obj_unref(&client->o);

	return 1;
}

/* try to get the reply code from any message in the collection */
static unsigned int get_replycode_callback(struct collection *c, struct ftpd_collectible_line *l, unsigned int *replycode) {
	unsigned int n;
	char s[4];

	*replycode = 0;

	if((l->line[3] != '-') && (l->line[3] != ' '))
		return 1;

	snprintf(s, 3, l->line);
	s[3] = 0;

	n = atoi(s);
	if(!n)
		return 1;

	*replycode = n;

	return 0;
}

/* build the reply buffer */
static unsigned int build_buffer_callback(struct collection *c, struct ftpd_collectible_line *l, void *param) {
	char *ptr;
	struct {
		unsigned int count;
		unsigned int current;
		unsigned int replycode;
		char *buffer;
	} *ctx = param;
	char s[4];
	char *line = l->line;

	ctx->current++;

	ptr = strchr(line, '\n');
	if(ptr) {
		*ptr = 0;
		ptr++;
		if(!strlen(ptr)) ptr = NULL;
	}

	/* skip any reply code */
	if((line[3] == '-') || (line[3] == ' ')) {
		snprintf(s, 3, line);
		s[3] = 0;
		if(atoi(s)) line += 4;
	}

	/* prepare the buffer */
	if(ctx->buffer) {
		ctx->buffer = realloc(ctx->buffer, strlen(ctx->buffer) + 4 + strlen(line) + 3);
		if(!ctx->buffer) {
			FTPD_DBG("Memory error");
			return 0;
		}
	} else {
		ctx->buffer = malloc(4 + strlen(line) + 3);
		if(!ctx->buffer) {
			FTPD_DBG("Memory error");
			return 0;
		}
		*(ctx->buffer) = 0;
	}

	/* append to the buffer */
	if((ctx->current == ctx->count) && !ptr) 
		sprintf(&ctx->buffer[strlen(ctx->buffer)], "%u %s\r\n", ctx->replycode, line);
	else
		sprintf(&ctx->buffer[strlen(ctx->buffer)], "%u-%s\r\n", ctx->replycode, line);

	line = ptr;
	/* loop the rest of the message */
	while(line && strlen(line)) {
		ptr = strchr(line, '\n');
		if(ptr) {
			*ptr = 0;
			ptr++;
			if(!strlen(ptr)) ptr = NULL;
		}

		snprintf(s, 3, line);
		s[3] = 0;

		if(((line[3] == '-') || (line[3] == ' ')) && atoi(s)) {
			line += 4;

			ctx->buffer = realloc(ctx->buffer, strlen(ctx->buffer) + 4 + strlen(line) + 3);
			if(!ctx->buffer) {
				FTPD_DBG("Memory error");
				return 0;
			}

			/* append to the buffer */
			if((ctx->current == ctx->count) && !ptr) 
				sprintf(&ctx->buffer[strlen(ctx->buffer)], "%u %s\r\n", ctx->replycode, line);
			else
				sprintf(&ctx->buffer[strlen(ctx->buffer)], "%u-%s\r\n", ctx->replycode, line);
		} else {
			/* there was no reply code, don't add one */
			ctx->buffer = realloc(ctx->buffer, strlen(ctx->buffer) + strlen(line) + 3);
			if(!ctx->buffer) {
				FTPD_DBG("Memory error");
				return 0;
			}

			/* append to the buffer */
			sprintf(&ctx->buffer[strlen(ctx->buffer)], "%s\r\n", line);
		}

		line = ptr;
	}

	//free(item);
	//collection_delete(c, item);
	ftpd_line_destroy(l);

	return 1;
}

/* send a message on a socket right away */
static int ftpd_client_send(struct ftpd_client_ctx *client, char *str) {
	int size;

	size = send(client->fd, str, strlen(str), 0);
	if(size != strlen(str)) {
		
		FTPD_DBG("WARNING: sent size mismatch line length");
		return 0;
	}

	return 1;
}

/*
	because of the way we handle the messages to be sent, we
	may have any kind of message in the messages collection
	so we will normalize them, build a big buffer and send it
*/
static int ftpd_client_process_output(struct ftpd_client_ctx *client) {
	struct {
		unsigned int count;
		unsigned int current;
		unsigned int replycode;
		char *buffer;
	} ctx = { 0, 0, 0, NULL };

	ctx.count = collection_size(client->messages);
	if(!ctx.count)
		return 1;

	/* try to get the reply code */
	collection_iterate(client->messages, (collection_f)get_replycode_callback, &ctx.replycode);
	if((ctx.replycode > 999) || (ctx.replycode < 100)) {
		/* can't get replycode ... */
		FTPD_DBG("Could not get valid reply code (%u is invalid)", ctx.replycode);
		return 0;
	}

	/* build a big buffer with all replies */
	collection_iterate(client->messages, (collection_f)build_buffer_callback, &ctx);
	if(!ctx.buffer) {
		FTPD_DBG("Could not build a message buffer");
		return 0;
	}

	/* send the buffer */
	if(!ftpd_client_send(client, ctx.buffer)) {
		FTPD_DBG("Could not send message to client");
		free(ctx.buffer);
		return 0;
	}
	free(ctx.buffer);

	return 1;
}

int ftpd_client_write_timeout(struct ftpd_client_ctx *client) {
	unsigned long long int time;

	time = timer(client->last_timestamp);

	if(time > FTPD_CLIENT_TIMEOUT) {
		FTPD_DIALOG_DBG("Client was disconnected (write timeout)");
		ftpd_client_destroy(client);
		return 0;
	} else {
		//FTPD_DBG("Client write timeout but only %I64u elapsed.", time);
	}
	
	return 1;
}


int ftpd_client_write(int fd, struct ftpd_client_ctx *client) {

	obj_ref(&client->o);

	if(!client->connected) {
		unsigned int ip;

		FTPD_DIALOG_DBG("Sending greetings to client ...");
		client->connected = 1;

		ip = socket_peer_address(client->fd);

		/* say hello */
		ftpd_client_text_enqueue(client->messages, "220-%s\n", ftpd_banner);
		ftpd_client_text_enqueue(client->messages, "220-Connection accepted from %u.%u.%u.%u.\n",
			(ip) & 0xff, (ip >> 8) & 0xff,
			(ip >> 16) & 0xff, (ip >> 24) & 0xff
		);

		client->timestamp = time_now();
		client->last_timestamp = time_now();

		/* call the onClientConnect event */
		if(!event_onClientConnect(client)) {
			FTPD_DBG("Connection refused for new client at %u.%u.%u.%u",
				(ip) & 0xff, (ip >> 8) & 0xff,
				(ip >> 16) & 0xff, (ip >> 24) & 0xff
			);
			ftpd_client_destroy(client);
			obj_unref(&client->o);
			return 0;
		}
		ftpd_client_text_enqueue(client->messages, "220 Service ready for new user.");

	} else {

		/* Check the timeout on client connection */
		if(!ftpd_client_write_timeout(client)) {
			FTPD_DIALOG_DBG("Client connection timed out before writing.");
			ftpd_client_destroy(client);
			obj_unref(&client->o);
			return 0;
		}

		/* if there's data to send in the message buffer, send it */
		if(!ftpd_client_process_output(client)) {
			FTPD_DBG("Could not process output to client.");
			ftpd_client_destroy(client);
			obj_unref(&client->o);
			return 0;
		}
	}

	obj_unref(&client->o);
	return 1;
}

struct ftpd_client_ctx *ftpd_client_new(int fd) {
	struct ftpd_client_ctx *client;

	client = malloc(sizeof(struct ftpd_client_ctx));
	if(!client) {
		FTPD_DBG("Memory error");
		return NULL;
	}

	/* setup the i/o buffer */
	obj_init(&client->o, client, (obj_f)ftpd_client_obj_destroy);
	collectible_init(client);

	client->buffersize = FTPD_BUFFER_SIZE;
	client->iobuf = malloc(client->buffersize);
	if(!client->iobuf) {
		FTPD_DBG("Memory error");
		free(client);
		return NULL;
	}
	memset(client->iobuf, 0, client->buffersize);
	client->filledsize = 0;

	client->volatile_config = config_new(NULL, 0);
	if(!client->volatile_config) {
		free(client->iobuf);
		FTPD_DBG("Memory error");
		free(client);
		return NULL;
	}

	/* setup the message collection */
	client->messages = collection_new(C_CASCADE);
	client->group = collection_new(C_CASCADE);

	/* setup the default values for the data context */
	client->data_ctx.fd = -1;
	client->data_ctx.data = collection_new(C_CASCADE);
	client->data_ctx.group = collection_new(C_CASCADE);

	/* setup the working directory */
	client->working_directory = vfs_root;
	collection_add(client->working_directory->browsers, client);
	client->user = NULL;

	client->last_timestamp = time_now();
	client->fd = fd;
	client->connected = 0;
	client->logged = 0;
	client->last_command = CMD_NONE;
	memset(&client->username, 0, sizeof(client->username));

	/* common variables */
	client->slave_xfer = 0;
	client->passive = 0;
	client->ready = 0;
	client->ip = 0;
	client->port = 0;

	client->type = TYPE_ASCII;
	client->structure = STRUCTURE_FILE;
	client->mode = MODE_STREAM;

	/* slave xfer variables */
	client->xfer.uid = -1;
	client->xfer.restart = 0;
	client->xfer.upload = 0;
	client->xfer.cmd = NULL;
	client->xfer.element = NULL;
	client->xfer.cnx = NULL;
	client->xfer.xfered = 0;
	client->xfer.last_alive = time_now();

	/* hook all signals on the socket */
	socket_monitor_new(fd, 1, 1);
	socket_monitor_signal_add(fd, client->group, "socket-close", (signal_f)ftpd_client_close, client);
	socket_monitor_signal_add(fd, client->group, "socket-error", (signal_f)ftpd_client_error, client);
	
	{
		struct signal_callback *s;
		
		s = socket_monitor_signal_add(fd, client->group, "socket-read", (signal_f)ftpd_client_read, client);
		signal_timeout(s, FTPD_CLIENT_TIMEOUT, (timeout_f)ftpd_client_read_timeout, client);
		
		s = socket_monitor_signal_add(fd, client->group, "socket-write", (signal_f)ftpd_client_write, client);
		signal_timeout(s, FTPD_CLIENT_TIMEOUT, (timeout_f)ftpd_client_write_timeout, client);
	}

	/* add the context to the clients collection */
	if(!collection_add(clients, client)) {
		FTPD_DBG("Collection error");
		ftpd_client_destroy(client);
		return NULL;
	}

	return client;
}

static unsigned long long int ftpd_connections = 0;

int ftpd_connect(int fd, void *param) {
	struct ftpd_client_ctx *client = NULL;
	int client_fd;

/*
	FTPD_DBG("Stats [%I64u / %I64u:%u / %u / %u:%u] - Last Cycle [%I64u]",
		ftpd_connections,
		socket_current,
		socket_monitors ? collection_size(socket_monitors) : 0,
		collection_size(clients),
		lua_getgccount(L),
		lua_getgcthreshold(L),
		main_cycle_time
	);
*/
	/* connect to the client */
	client_fd = accept(fd, NULL, 0);
	if(client_fd == -1) {
		FTPD_DBG("FTPD socket could not accept new client");
		return 0;
	}
	socket_current++;

	ftpd_connections++;
	
	/* register the new client */
	client = ftpd_client_new(client_fd);
	if(!client) {
		FTPD_DBG("Could not add the new client");
		close_socket(client_fd);
		return 0;
	}

	/* enable linger so we'll perform hard abort on closesocket() */
	socket_linger(client_fd, 0);

	/* set the proper socket options */
	socket_set_max_read(client_fd, FTPD_CLIENT_SOCKET_SIZE);
	socket_set_max_write(client_fd, FTPD_CLIENT_SOCKET_SIZE);

	FTPD_DIALOG_DBG("Ftpd client connected from %s", socket_formated_peer_address(client_fd));

	return 1;
}

int ftpd_error(int fd, void *param) {

	FTPD_DBG("ftpd socket error");

	main_fatal();

	return 1;
}

int ftpd_close(int fd, void *param) {

	FTPD_DBG("ftpd socket closed");

	main_fatal();

	return 1;
}

int ftpd_load_config() {
	unsigned int v;

	/* read ftpd port */
	v = config_raw_read_int(MASTER_CONFIG_FILE, "xftpd.port", FTPD_PORT);
	if(v & 0xffff0000) {
		FTPD_DBG("xftpd.port: valid port range is 1-65535, defaulting to %u", (short)FTPD_PORT);
		v = FTPD_PORT;
	}
	ftpd_client_port = (short)v;

	/* read low data port */
	v = config_raw_read_int(MASTER_CONFIG_FILE, "xftpd.low_data_port", FTPD_LOW_DATA_PORT);
	if(v & 0xffff0000) {
		FTPD_DBG("xftpd.low_data_port: valid port range is 1-65535. value is reset to default.");
		v = FTPD_LOW_DATA_PORT;
	}
	ftpd_low_data_port = (short)v;

	/* read high data port */
	v = config_raw_read_int(MASTER_CONFIG_FILE, "xftpd.high_data_port", FTPD_HIGH_DATA_PORT);
	if(v & 0xffff0000) {
		FTPD_DBG("xftpd.high_data_port: valid port range is 1-65535. value is reset to default.");
		v = FTPD_HIGH_DATA_PORT;
	}
	ftpd_high_data_port = (short)v;

	/* data port range sanity check */
	if(ftpd_low_data_port > ftpd_high_data_port) {
		FTPD_DBG("data port range is invalid, reseting to default.");
		ftpd_low_data_port = FTPD_LOW_DATA_PORT;
		ftpd_high_data_port = FTPD_HIGH_DATA_PORT;
	}

	/* read the server name */
	ftpd_banner = config_raw_read(MASTER_CONFIG_FILE, "xftpd.banner", "Welcome to this xFTPd Server");

	return 1;
}

int ftpd_init() {

	FTPD_DBG("Loading ...");

	/* init the context to its default values */
	ftpd_fd = -1;
	ftpd_banner = NULL;
	ftpd_group = NULL;
	clients = NULL;
	ftpd_current_data_port = 0;
	ftpd_next_xfer_uid = 0;

	if(!ftpd_load_config()) {
		FTPD_DBG("Could not load ftpd config");
		return 0;
	}

	clients = collection_new(C_CASCADE);
	ftpd_group = collection_new(C_CASCADE);

	/* create our main socket */
	ftpd_fd = create_listening_socket(ftpd_client_port);
	if(ftpd_fd == -1) {
		FTPD_DBG("Could not create listening socket on port %u", ftpd_client_port);
		return 0;
	}

	FTPD_DBG("Listening on port %u for new clients", ftpd_client_port);

	/* register the "error", "close", "connect" signals */
	socket_monitor_new(ftpd_fd, 0, 1);
	socket_monitor_signal_add(ftpd_fd, ftpd_group, "socket-error", (signal_f)ftpd_error, NULL);
	socket_monitor_signal_add(ftpd_fd, ftpd_group, "socket-close", (signal_f)ftpd_close, NULL);
	socket_monitor_signal_add(ftpd_fd, ftpd_group, "socket-connect", (signal_f)ftpd_connect, NULL);

	return 1;
}

void ftpd_free() {

	FTPD_DBG("Unloading...");

	if(ftpd_banner) {
		free(ftpd_banner);
		ftpd_banner = NULL;
	}

	if(ftpd_group) {
		signal_clear(ftpd_group);
		collection_destroy(ftpd_group);
		ftpd_group = NULL;
	}

	/* close the listening socket */
	if(ftpd_fd != -1) {
		socket_monitor_fd_closed(ftpd_fd);
		close_socket(ftpd_fd);
		ftpd_fd = -1;
	}

	/* we have to do it in two times */
	if(clients) {
		/*collection_void(clients);
		while(collection_size(clients)) {
			void *first = collection_first(clients);
			ftpd_client_destroy(first);
			collection_delete(clients, first);
		}*/
		collection_destroy(clients);
		clients = NULL;
	}

	return;
}

/* reload the ftpd & call all other reload subroutines */
int ftpd_reload() {

	FTPD_DBG("Reloading...");

	ftpd_load_config();

	return 1;
}

