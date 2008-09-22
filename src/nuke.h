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

#ifndef __NUKE_H
#define __NUKE_H

#include "constants.h"

#ifndef NO_FTPD_DEBUG
#  define DEBUG_NUKE
#endif

#ifdef DEBUG_NUKE
# ifdef FTPD_DEBUG_TO_CONSOLE
#  define NUKE_DBG(format, arg...) printf("["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg)
# else
#  include "logging.h"
#  define NUKE_DBG(format, arg...) logging_write("debug.log", "["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg)
# endif
#else
#  define NUKE_DBG(format, arg...)
#endif

extern struct collection *nukes;

typedef struct nuke_nukee nuke_nukee;
struct nuke_nukee {

	/* parent nuke structure */
	struct nuke_ctx *nuke;
	
	/*
		we do not reference users here because it
		has to be valid even after the user is deleted
	*/
	char *name;

	/* ammount of credit nuked */
	unsigned long long int ammount;

} __attribute__((packed));

typedef struct nuke_ctx nuke_ctx;
struct nuke_ctx {

	/*
		nuked element, null if it has been deleted
		This is here only for convenience, you should
		use 'path' when possible.
	*/
	struct vfs_element *element;

	/* always point to the full path to the element */
	char *path;

	unsigned long long int timestamp;

	unsigned int multiplier;
	char *nuker;
	char *reason;

	/* struct nuke_nukee */
	struct collection *nukees;

} __attribute__((packed));

int nuke_init();
int nuke_reload();
void nuke_free();

/* Add a nuke and save to file */
struct nuke_ctx *nuke_add(struct vfs_element *element, unsigned int multiplier, char *nuker, char *reason);

/* Add a nukee and save to file */
struct nuke_nukee *nukee_add(struct nuke_ctx *nuke, char *name, unsigned long long int ammount);

/* Fast lookup from path to nuke */
struct nuke_ctx *nuke_get(char *path);

/* Fast lookup from name to nukee */
struct nuke_nukee *nukee_get(struct nuke_ctx *nuke, char *name);

/* Delete a nukee and save to file */
void nukee_del(struct nuke_nukee *nukee);

/* Delete a nuke and save to file */
void nuke_del(struct nuke_ctx *nuke);

/*
	Lookup in the nukes and link the element if it is nuked
	Called	on new dir
			on new file
*/
struct nuke_ctx *nuke_check(struct vfs_element *element);

/*
	Check all nukes and link them to thier elements.
	Called	on ititilalitation,
			after a files list is received from a slave
*/
int nuke_check_all();

/* Dump all nukes to file. */
int nuke_dump_all();

#endif /* __NUKE_H */
