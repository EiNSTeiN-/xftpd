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

/*
	manage the (almost) real-time stats between master and slaves
*/

#include "constants.h"
#include "collection.h"
#include "time.h"
#include "slaves.h"
#include "stats.h"
#include "ftpd.h"
#include "logging.h"
#include "mirror.h"
#include "main.h"
#include "asynch.h"

static int probe_stats_update_xfer(struct collection *c, struct ftpd_client_ctx *client, void *param) {
	struct {
		struct slave_connection *cnx;
		struct stats_xfer *stats;
	} *ctx = param;

	if((client->xfer.uid != -1) && (client->xfer.uid == ctx->stats->uid)) {

		/*
			update the timestamp because the client is xfering
			and we don't want him to get disconnected
		*/
		client->last_timestamp = time_now();

		/* update the xfered size */
		client->xfer.xfered = ctx->stats->xfered;
		client->xfer.last_alive = time_now();

#ifdef GROW_FILE_SIZES_ON_TRANSFER
		if(client->xfer.upload && client->xfer.element) {
			/* set the current file size if the user is uploading it */
			vfs_set_size(client->xfer.element, client->xfer.xfered);
		}
#endif

		return 0;
	}

	return 1;
}

static int probe_stats_update_mirror(struct collection *c, struct mirror_ctx *mirror, void *param) {
	struct {
		struct slave_connection *cnx;
		struct stats_xfer *stats;
	} *ctx = param;

	if(mirror->uid == ctx->stats->uid) {
		if(mirror->source.cnx == ctx->cnx) {
			mirror->source.xfered = ctx->stats->xfered;
			mirror->source.last_alive = time_now();
		}
		if(mirror->target.cnx == ctx->cnx) {
			mirror->target.xfered = ctx->stats->xfered;
			mirror->target.last_alive = time_now();

#ifdef GROW_FILE_SIZES_ON_TRANSFER
			/* Update the filesize here if needed */
			vfs_set_size(mirror->target.file, mirror->target.xfered);
#endif

		}
		return 0;
	}

	return 1;
}

/* receive the stats response from the slaves */
/* return 0 to disconnect the slave */
static unsigned int probe_stats_callback(struct slave_connection *cnx, struct slave_asynch_command *cmd, struct packet *p) {
	struct stats_global *gstats;
	struct stats_xfer *xstats;
	unsigned int length;
	unsigned int xfers_count, i;
	struct {
		struct slave_connection *cnx;
		struct stats_xfer *stats;
	} ctx = { cnx, NULL };

	/* if timeout or protocol error then exit */
	if(!p || (p->type != IO_STATS)) {
		STATS_DBG("%I64u: Stats query failed", cmd->uid);
		return 0;
	}

	/* first thing in the packet is the
		global stats : diskfree, disktotal, ... */
	length = (p->size - sizeof(struct packet));
	gstats = (struct stats_global *)&p->data[0];

	/* protocol error */
	if(length < sizeof(struct stats_global)) {
		STATS_DBG("Protocol error");
		return 0;
	}

	cnx->diskfree = gstats->diskfree;
	cnx->disktotal = gstats->disktotal;
	
	/* protocol error */
	if(((length - sizeof(struct stats_global)) % sizeof(struct stats_xfer)) != 0) {
		STATS_DBG("Protocol error");
		return 0;
	}

	/* second thing is an array of xfer stats
		wich give infos about the progression
		status of all xfers */
	xfers_count = ((length - sizeof(struct stats_global)) / sizeof(struct stats_xfer));
	xstats = (struct stats_xfer *)&p->data[sizeof(struct stats_global)];

	for(i=0;i<xfers_count;i++) {
		ctx.stats = &xstats[i];
		/* the stat may be about a xfer or a mirror, no way to know */
		collection_iterate(cnx->xfers, (collection_f)probe_stats_update_xfer, &ctx);
		collection_iterate(cnx->mirror_from, (collection_f)probe_stats_update_mirror, &ctx);
		collection_iterate(cnx->mirror_to, (collection_f)probe_stats_update_mirror, &ctx);
	}

	return 1;
}

static int probe_stats_on_slave(struct collection *c, struct slave_connection *cnx, void *param) {
	struct slave_asynch_command *cmd;

	/* put some time between now and the last probe */
	if(timer(cnx->statstime) < FTPD_STATS_PROBE_TIME) {
		return 1;
	}

	cnx->statstime = time_now();

	/* enqueue a IO_STATS packet */
	cmd = asynch_new(cnx, IO_STATS, MASTER_ASYNCH_TIMEOUT, NULL, 0, probe_stats_callback, NULL);
	if(!cmd) {
		STATS_DBG("Memory error");
		return 1;
	}

	return 1;
}


/* query the stats for all slaves, according
	to thier last probe time */
unsigned int probe_stats() {

	collection_iterate(connected_slaves, (collection_f)probe_stats_on_slave, NULL);

	return 1;
}
