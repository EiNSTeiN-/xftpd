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


#include "io.h"
#include "slaves.h"
#include "asynch.h"
#include "config.h"

#include "update.h"

static unsigned int update_query_callback(struct slave_connection *cnx, struct slave_asynch_command *cmd, struct packet *p) {
	
	UPDATE_DBG("" LLU ": Update response received: update failed ?", cmd->uid);
	
	return 1;
}

/* Send an update packet to all slaves */
int update_slave(struct slave_connection *cnx, const char *filename) {
	struct slave_asynch_command *cmd;
	unsigned int size;
	char *buffer;
	
	/* Load the file to memory. */
	buffer = config_load_file(filename, &size);
	if(!buffer) {
		UPDATE_DBG("Could not load %s from disk!", filename);
		return 0;
	}
	
	/* Send the update packet to the slave. */
	cmd = asynch_new(cnx, IO_UPDATE, -1, (unsigned char *)buffer, size, update_query_callback, NULL);
	free(buffer);
	if(!cmd) {
		UPDATE_DBG("Could NOT build update buffer for update!");
		return 0;
	}
	
	UPDATE_DBG("Update query was sent without problem!");
	
	return 1;
}




