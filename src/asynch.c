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

#include "asynch.h"
#include "slaves.h"
#include "time.h"

static unsigned long long int asynch_next_uid = 0;

static void asynch_obj_destroy(struct slave_asynch_command *cmd) {
	
	collectible_destroy(cmd);

	if(collection_find(cmd->cnx->asynch_queries, cmd)) {
		collection_delete(cmd->cnx->asynch_queries, cmd);
	}
	if(collection_find(cmd->cnx->asynch_response, cmd)) {
		collection_delete(cmd->cnx->asynch_response, cmd);
	}

	free(cmd);

	return;
}

struct slave_asynch_command *asynch_new(
	struct slave_connection *cnx,
	unsigned char command,
	unsigned int timeout,
	unsigned char *data,
	unsigned int data_length,
	unsigned int (*reply_callback)(struct slave_connection *cnx, struct slave_asynch_command *cmd, struct packet *p),
	void *param
) {
	struct slave_asynch_command *cmd;

	if(data_length && !data) {
		ASYNCH_DBG("Data length but no data");
		return NULL;
	}

	cmd = malloc(sizeof(struct slave_asynch_command) + data_length);
	if(!cmd) {
		ASYNCH_DBG("Memory error");
		return NULL;
	}

	obj_init(&cmd->o, cmd, (obj_f)asynch_obj_destroy);
	collectible_init(cmd);
	
	cmd->cnx = cnx;
	//cmd->destroyed = 0;
	cmd->send_time = 0;
	cmd->uid = asynch_next_uid++;
	cmd->timeout = timeout;

	cmd->reply_callback = reply_callback;
	cmd->param = param;

	cmd->command = command;
	cmd->data_length = data_length;
	if(data)
		memcpy(cmd->data, data, data_length);

	/* add the command to the slave's queries list */
	if(!collection_add(cnx->asynch_queries, cmd)) {

		ASYNCH_DBG("Could not add new cmd to asynch queries collection");

		free(cmd);
		return NULL;
	}

	return cmd;
}

/*
	The destroy function calls the callback and destroy the asynch command,
	removing it from the slave's asynch queries.
*/
unsigned int asynch_destroy(struct slave_asynch_command *cmd, struct packet *p) {
	unsigned int success;

	if(!obj_isvalid(&cmd->o)) {

		/* Avoid double-free */

		return 0;
	}

	obj_ref(&cmd->o);
	obj_destroy(&cmd->o);

	/* pass the data to the callback */
	success = (*cmd->reply_callback)(cmd->cnx, cmd, p);

	obj_unref(&cmd->o);

	return success;
}

static int asynch_matcher(struct collection *c, struct slave_asynch_command *cmd, struct packet *p) {

	return (p->uid == cmd->uid);
}

/*
	asynch_match is called when a packet is received from a slave.
	It call the associated callback and then destroy the command.
*/
unsigned int asynch_match(struct slave_connection *cnx, struct packet *p) {
	struct slave_asynch_command *cmd;
	unsigned int success;

	/* search any packet in asynch_response list matching the packet uid */
	cmd = collection_match(cnx->asynch_response, (collection_f)asynch_matcher, p);

	if(!cmd) {
		/* This is not critical, we stay connected when it happens */
		SLAVES_DBG("Received a query that could not be matched: " LLU " / type: %u / length: %u", p->uid, p->type, p->size);
		return 1;
	}

	/* update the slave's lag time */
	cnx->lagtime = timer(cmd->send_time);
	
	/* free the command */
	success = asynch_destroy(cmd, p);

	return success;
}
