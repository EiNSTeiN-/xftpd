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

#ifndef __VFS_H
#define __VFS_H

#include "constants.h"

#ifndef NO_FTPD_DEBUG
#  define DEBUG_VFS
#  define DEBUG_SECTIONS
#endif

#ifdef DEBUG_VFS
# include "logging.h"
# ifdef FTPD_DEBUG_TO_CONSOLE
#  define VFS_DBG(format, arg...) printf("["__FILE__ ":\t\t%d ]\t" format "\n", __LINE__, ##arg)
# else
#  define VFS_DBG(format, arg...) logging_write("debug.log", "["__FILE__ ":\t\t%d ]\t" format "\n", __LINE__, ##arg)
# endif
#else
#  define VFS_DBG(format, arg...)
#endif

#ifdef DEBUG_SECTIONS
# include "logging.h"
# ifdef FTPD_DEBUG_TO_CONSOLE
#  define SECTIONS_DBG(format, arg...) printf("["__FILE__ ":\t\t%d ]\t" format "\n", __LINE__, ##arg)
# else
#  define SECTIONS_DBG(format, arg...) logging_write("debug.log", "["__FILE__ ":\t\t%d ]\t" format "\n", __LINE__, ##arg)
# endif
#else
#  define SECTIONS_DBG(format, arg...)
#endif

#include "slaves.h"
#include "collection.h"

int vfs_init();
void vfs_free();

extern struct vfs_element *vfs_root;

/* sections will be displayed in the root directory */
typedef struct vfs_section vfs_section;
struct vfs_section {
	char *name; /* of the section */
	
	struct config_file *config;

	struct collection *roots; /* vfs_element that this section is linked to */
	struct collection *slaves; /* slaves_ctx mapped to this section */
} __attribute__((packed));

typedef enum {
	VFS_FILE,
	VFS_FOLDER,
	VFS_LINK,
} vfs_type;

/* TODO?
struct vfs_conflict {
	unsigned long long int size;
	unsigned long long int timestamp;

	struct slave_connection *from; // conflicts are removed when slaves are turned offline.
} __attribute__((packed));
*/

/* represents any member of the file system */
typedef struct vfs_element vfs_element;
struct vfs_element {
	struct obj o;

	/* these are mostly set be the vfs functions */
	struct vfs_section *section;	/* section */
	struct vfs_element *parent;	/* parent element */
	vfs_type type;				/* folder, file, ... */
	char *name;					/* element name */

	/*
		Keep the name's checksum to allow a much faster
		search. Little trick that makes the whole process
		of adding a file to the vfs almost twice faster.
	*/
	unsigned int namesum;		

	char *owner;				/* element's owner */

	/* these must be set by the caller */
	unsigned long long int size;/* element's size */
	unsigned long long int timestamp; /* modification timestamp */
	unsigned long long int xfertime; /* transfer time. not kept across boots. */
	unsigned int checksum; /* checksum for that file, 0 if unknown */

	/* these are to keep track of the internal state */
	struct collection *childs;	/* sub-elements */
	
	struct collection *offline_from; /* collection of struct slave_ctx
						containing wich slave the file is offline from */	
	struct collection *available_from; /* collection of struct slave_connection
						containing wich connections the file is available from */
	
	struct ftpd_client_ctx *uploader;	/* only one uploader allowed at a time per slave */
	struct collection *leechers; /* collection of _ftpd_client_context downloading the file */

	struct sfv_ctx *sfv; /* structure wich holds the sfv informations.
							this is stored at folder level, so even when files
							are not yet uploaded the information will still be present */

	/* tracking for mirror operations */
	struct collection *mirror_from; /* (outgoing) mirrors from this slave */
	struct collection *mirror_to; /* (incoming) mirrors to this slave */

	/* clients that are currently browsing the folder */
	struct collection *browsers;

	/* symlinks structures */
	struct collection *link_from; /* from which symlink this file is pointed from */
	struct vfs_element *link_to; /* to wich file this symlink points */

	struct slave_ctx *vrooted; /* slave of wich this directory is the vroot */

	struct nuke_ctx *nuke; /* nuke information on the file/folder */

	//int destroyed; /* avoid double-frees */
} __attribute__((packed));

char *vfs_trim_name(const char *name);

struct vfs_element *vfs_find_element(struct vfs_element *container, const char *path);
struct vfs_element *vfs_raw_find_element(struct vfs_element *container, const char *path);

struct vfs_element *vfs_get_child_by_name(struct vfs_element *container, const char *name);
char *vfs_get_relative_path(struct vfs_element *container, struct vfs_element *element);
unsigned int vfs_is_child(struct vfs_element *parent, struct vfs_element *child);

struct vfs_element *vfs_create_folder(struct vfs_element *container, const char *name, const char *owner);
struct vfs_element *vfs_create_file(struct vfs_element *container, const char *name, const char *owner);
struct vfs_element *vfs_create_symlink(struct vfs_element *container, struct vfs_element *target, const char *name, const char *owner);

unsigned int vfs_delete(struct vfs_element *container, char *path, unsigned int recursive);
unsigned int vfs_recursive_delete(struct vfs_element *element);

unsigned int vfs_increase_size(struct vfs_element *element, unsigned long long int size);
unsigned int vfs_decrease_size(struct vfs_element *element, unsigned long long int size);
unsigned int vfs_modify(struct vfs_element *element, unsigned long long int timestamp);
unsigned int vfs_set_size(struct vfs_element *element, unsigned long long int size);

struct vfs_element *vfs_get_section_element(struct vfs_element *element);
struct vfs_section *vfs_get_section(struct vfs_element *element);

unsigned int vfs_set_checksum(struct vfs_element *element, unsigned int checksum);
unsigned int vfs_get_checksum(struct vfs_element *element);

struct vfs_element *vfs_create_root();

const char *vfs_lua_get_relative_path(struct vfs_element *container, struct vfs_element *element);

/* apis for sections management */
extern struct collection *sections;

struct vfs_section *section_load(const char *filename);
struct vfs_section *section_new(const char *name/*, struct vfs_element *element*/);
struct vfs_section *section_get(const char *name);

unsigned int section_dump(struct vfs_section *section);
unsigned int section_del(struct vfs_section *section);

unsigned int section_link_slave(struct vfs_section *section, struct slave_ctx *slave);
unsigned int section_unlink_slave(struct vfs_section *section, struct slave_ctx *slave);

unsigned int section_link_root(struct vfs_section *section, struct vfs_element *root);
unsigned int section_unlink_root(struct vfs_section *section, struct vfs_element *root);

#endif /* __VFS_H */
