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

/* virtual file system */

#ifdef WIN32
#include <windows.h>
#endif

#include <stdio.h>

#include "constants.h"

#include "logging.h"
#include "collection.h"
#include "config.h"
#include "vfs.h"
#include "ftpd.h"
#include "time.h"
#include "events.h"
#include "sfv.h"
#include "mirror.h"
#include "nuke.h"
#include "crc32.h"
#include "dir.h"
#include "asprintf.h"
#include "wild.h"

struct collection *sections = NULL;

struct vfs_element *vfs_root = NULL;

static void vfs_obj_destroy(struct vfs_element *element) {
	
	collectible_destroy(element);
	
	//VFS_DBG("destructing %s", element->name);

	/* Before anything, propagate the destruction to all childs */
	if(element->childs) {
		collection_destroy(element->childs);
		element->childs = NULL;
	}

	/*
		Set the vroot of the slave to vfs_root
		We can do this because at this point the
		vroot does not contain any files 
	*/
	if(element->vrooted) {
		element->vrooted->vroot = vfs_root;
		element->vrooted = NULL;
	}

	if(element->nuke) {
		element->nuke->element = NULL;
		element->nuke = NULL;
	}

	if(element->section) {
		section_unlink_root(element->section, element);
		element->section = NULL;
	}

	/* cleanup the data contexts of all leechers
		and from the uploader, if any. */
	if(element->uploader) {
		ftpd_client_cleanup_data_connection(element->uploader);
		element->uploader = NULL;
	}

	if(element->leechers) {
		//collection_iterate(element->leechers, (collection_f)vfs_obj_destroy_leechers, NULL);
		collection_destroy(element->leechers);
		element->leechers = NULL;
	}

	if(element->browsers) {
		//collection_iterate(element->browsers, (collection_f)vfs_obj_destroy_browsers, NULL);
		collection_destroy(element->browsers);
		element->browsers = NULL;
	}

	/* delete the available_from list */
	if(element->available_from) {
		//collection_iterate(element->available_from, (collection_f)vfs_obj_destroy_available_from, element);
		collection_destroy(element->available_from);
		element->available_from = NULL;
	}

	/* delete the available_from list */
	if(element->offline_from) {
		//collection_iterate(element->offline_from, (collection_f)vfs_obj_destroy_offline_from, element);
		collection_destroy(element->offline_from);
		element->offline_from = NULL;
	}

	/* cancel any associated mirrors */
	if(element->mirror_from) {
		//collection_iterate(element->mirror_from, (collection_f)vfs_obj_destroy_mirror, NULL);
		collection_destroy(element->mirror_from);
		element->mirror_from = NULL;
	}
	if(element->mirror_to) {
		//collection_iterate(element->mirror_to, (collection_f)vfs_obj_destroy_mirror, NULL);
		collection_destroy(element->mirror_to);
		element->mirror_to = NULL;
	}

	/* if this is a symlink, we won't delete the file */
	if(element->link_to) {
		//collection_delete(element->link_to->link_from, element);
		element->link_to = NULL;
	}

	/* if this is a file, delete all associated symlinks  */
	if(element->link_from) {
		//collection_iterate(element->link_from, (collection_f)vfs_obj_destroy_symlink, NULL);
		collection_destroy(element->link_from);
		element->link_from = NULL;
	}
	
	/* decrease the size of the file, wich will recursively
		decrease the size of all parent directories */
	vfs_decrease_size(element, element->size);

	/* update the modification timestamp of the parent */
	vfs_modify(element->parent, time_now());
	
	element->parent = NULL;

	/* free some memory */
	if(element->owner) {
		free(element->owner);
		element->owner = NULL;
	}
	if(element->name) {
		free(element->name);
		element->name = NULL;
	}

	if(element->sfv) {
		element->sfv->element = NULL;
		sfv_delete(element->sfv);
		element->sfv = NULL;
	}

	free(element);
	
	//VFS_DBG("destruction finished");

	return;
}

struct vfs_element *vfs_create_root() {
	struct vfs_element *root;

	root = malloc(sizeof(struct vfs_element));
	if(!root) {
		VFS_DBG("Memory error");
		return NULL;
	}

	obj_init(&root->o, root, (obj_f)vfs_obj_destroy);
	//obj_debug(&root->o, 1);
	collectible_init(root);

	/* THE ROOT IS THE ONLY ONE WITH NO PARENT */
	root->parent = NULL;
	root->section = NULL;

	root->type = VFS_FOLDER;
	
	root->name = "/";
	root->owner = NULL;
	
	root->size = 0;
	root->timestamp = 0;

	root->childs = collection_new(C_CASCADE);

	root->offline_from = NULL;
	root->available_from = NULL;

	root->uploader = NULL;
	root->leechers = NULL;

	root->checksum = 0;
	root->sfv = NULL;

	root->mirror_from = NULL;
	root->mirror_to = NULL;

	root->browsers = collection_new(C_CASCADE);

	root->link_from = NULL;
	root->link_to = NULL;

	root->vrooted = NULL;
	root->nuke = NULL;

	root->xfertime = 0;

	//root->destroyed = 0;

	VFS_DBG("Root VFS is now created.");

	return root;
}

#define ENDSWITH(a, b) \
  ((strlen(a) > strlen(b)) && !strcasecmp(&a[strlen(a)-strlen(b)], b))
#define BEGINSWITH(a, b) \
  ((strlen(a) > strlen(b)) && !strncasecmp(a, b, strlen(b)))

int vfs_init() {
	struct vfs_section *section;
	struct dir_ctx *dir;
	char *path;

	VFS_DBG("Loading ...");
	
	if(!vfs_root) {
		vfs_root = vfs_create_root();
		if(!vfs_root)
			return 0;
	}

	sections = collection_new(C_CASCADE);

	path = dir_fullpath(SECTIONS_FOLDER);
	if(!path) {
		VFS_DBG("Memory error");
		return 0;
	}

	SECTIONS_DBG("Loading ...");

	dir = dir_open(path, "section.*");
	if(!dir) {
		SECTIONS_DBG("Couldn't open \"%s\"", path);
	} else {
		do {
			if(dir_attrib(dir) & DIR_SUBDIR) continue;
			if(ENDSWITH(dir_name(dir), ".tmp")) continue;
			
			section = section_load(dir_name(dir));
			if(!section) {
				SECTIONS_DBG("Could not load section from file %s", dir_name(dir));
				continue;
			}

		} while(dir_next(dir));
		dir_close(dir);
	}
	free(path);

	SECTIONS_DBG("%u section(s) loaded", collection_size(sections));
	
	return 1;
}

#undef BEGINSWITH
#undef ENDSWITH

void section_obj_destroy(struct vfs_section *section) {
	
	collectible_destroy(section);

	if(section->slaves) {
		//collection_iterate(section->slaves, (collection_f)unlink_slaves_from_section, section);
		collection_destroy(section->slaves);
		section->slaves = NULL;
	}

	if(section->roots) {
		//collection_iterate(section->roots, (collection_f)unlink_roots_from_section, section);
		collection_destroy(section->roots);
		section->roots = NULL;
	}

	if(section->config) {
		remove(section->config->filename);
		config_close(section->config);
		section->config = NULL;
	}

	if(section->name) {
		free(section->name);
		section->name = NULL;
	}

	free(section);
	
	return;
}

struct vfs_section *section_load(const char *filename) {
//	struct vfs_element *element;
	struct vfs_section *section;
	char *name, *roots, *_slaves, *ptr, *next;
	char *path;
	char *fullpath;

	path = dir_fullpath(SECTIONS_FOLDER);
	if(!path) return NULL;

	fullpath = bprintf("%s/%s", path, filename);
	free(path);
	if(!fullpath) {
		SECTIONS_DBG("Memory error");
		return NULL;
	}
	
	/* get the section name */
	name = config_raw_read(fullpath, "name", NULL);
	if(!name) {
		free(fullpath);
		return NULL;
	}

	/* add the section to the collection */
	section = malloc(sizeof(struct vfs_section));
	if(!section) {
		SECTIONS_DBG("Memory error");
		free(fullpath);
		free(name);
		return NULL;
	}
	
	obj_init(&section->o, section, (obj_f)section_obj_destroy);
	collectible_init(section);

	section->config = config_open(fullpath);
	free(fullpath);
	if(!section->config) {
		SECTIONS_DBG("Memory error");
		free(name);
		free(section);
		return NULL;
	}

	section->name = name;

	collection_add(sections, section);

	/* create the slaves collection */
	section->slaves = collection_new(C_NONE);
	section->roots = collection_new(C_NONE);

	roots = config_read(section->config, "roots", NULL);
	_slaves = config_read(section->config, "slaves", NULL);
	
	/* get the section's roots */
	if(roots) {

		ptr = roots;
		do {
			next = strchr(ptr, ';');
			if(next) {
				*next = 0;
				next++;
			}

			if(*ptr) {
				struct vfs_element *element;

				/* try to find the specified folder in the vfs */
				element = vfs_find_element(vfs_root, ptr);
				if(element) {
					if(!section_link_root(section, element)) {
						SECTIONS_DBG("Could not add %s to %s's roots", ptr, section->name);
					}
				} else {
					/* add the section's folder to the vfs */
					element = vfs_create_folder(vfs_root, ptr, "xFTPd");
					if(!element) {
						SECTIONS_DBG("Can't create %s in root", ptr);
					} else {
						if(!section_link_root(section, element)) {
							SECTIONS_DBG("Could not add %s to %s's roots", ptr, section->name);
						}
					}
				}
			}
			
			if(!next) break;
			ptr = next;
		} while(1);

		free(roots);
	}

	/* add all slaves to the collection */
	if(_slaves) {
		ptr = _slaves;
		do {
			next = strchr(ptr, ';');
			if(next) {
				*next = 0;
				next++;
			}

			if(*ptr) {
				struct slave_ctx *slave;

				slave = slave_get(ptr);
				if(!slave) {
					SECTIONS_DBG("Couldn't find slave %s for section %s", ptr, section->name);
				} else {
					if(!section_link_slave(section, slave)) {
						SECTIONS_DBG("Couldn't add slave %s to section %s", ptr, section->name);
					}
				}
			}
			
			if(!next) break;
			ptr = next;
		} while(1);

		free(_slaves);
	}

	return section;
}

/* associate a section with a folder in the vfs */
struct vfs_section *section_new(const char *name) {
	struct vfs_section *section;
	char filename[128];
	char *path;
	char *fullpath;

	if(!name) {
		SECTIONS_DBG("Parameter error");
		return NULL;
	}

	sprintf(filename, "section." LLU "", time_now());

	path = dir_fullpath(".");
	if(!path) {
		SECTIONS_DBG("Memory error");
		return NULL;
	}

	fullpath = malloc(strlen(path) + strlen(SECTIONS_FOLDER) + strlen(filename) + 1);
	if(!fullpath) {
		SECTIONS_DBG("Memory error");
		free(path);
		return NULL;
	}
	sprintf(fullpath, "%s" SECTIONS_FOLDER "%s", path, filename);
	free(path);


	section = malloc(sizeof(struct vfs_section));
	if(!section) {
		SECTIONS_DBG("Memory error");
		free(fullpath);
		return NULL;
	}
	
	obj_init(&section->o, section, (obj_f)section_obj_destroy);
	collectible_init(section);

	section->config = config_open(fullpath);
	free(fullpath);
	if(!section->config) {
		SECTIONS_DBG("Memory error");
		free(section);
		return NULL;
	}

	section->name = strdup(name);
	if(!section->name) {
		SECTIONS_DBG("Memory error");
		config_close(section->config);
		free(section);
		return NULL;
	}
	config_write(section->config, "name", name);

	section->roots = collection_new(C_NONE);
	section->slaves = collection_new(C_NONE);
	collection_add(sections, section);

	config_save(section->config);

	SECTIONS_DBG("Section %s added", section->name);

	return section;
}

static unsigned int section_get_matcher(struct collection *c, struct vfs_section *section, char *name) {

	return !strcasecmp(name, section->name);
}

/* return a section by its name */
struct vfs_section *section_get(const char *name) {

	if(!name) return NULL;

	return collection_match(sections, (collection_f)section_get_matcher, (char *)name);
}

static unsigned int build_slaves_string(struct collection *c, void *item, void *param) {
	struct {
		char *slaves;
	} *ctx = param;
	struct slave_ctx *slave = item;
	char *_slaves;

	if(!ctx->slaves)
		ctx->slaves = strdup(slave->name);
	else {
		_slaves = malloc(strlen(ctx->slaves)+1+strlen(slave->name)+1);
		if(!_slaves) {
			SECTIONS_DBG("Memory error");
			return 1;
		}
		sprintf(_slaves, "%s;%s", ctx->slaves, slave->name);
		free(ctx->slaves);
		ctx->slaves = _slaves;
	}

	return 1;
}

static unsigned int build_roots_string(struct collection *c, void *item, void *param) {
	struct {
		char *roots;
	} *ctx = param;
	struct vfs_element *element = item;
	char *roots, *root;

	if(!ctx->roots)
		ctx->roots = vfs_get_relative_path(vfs_root, element);
	else {
		root = vfs_get_relative_path(vfs_root, element);
		if(!root) {
			SECTIONS_DBG("Memory error");
			return 1;
		}
		roots = malloc(strlen(ctx->roots)+1+strlen(root)+1);
		if(!roots) {
			SECTIONS_DBG("Memory error");
			return 1;
		}
		sprintf(roots, "%s;%s", ctx->roots, root);
		free(root);
		free(ctx->roots);
		ctx->roots = roots;
	}

	return 1;
}

unsigned int section_dump(struct vfs_section *section) {
	struct {
		char *buffer;
	} ctx = { NULL };

	if(!section) return 0;

	if(!collection_size(section->slaves)) {
		config_write(section->config, "slaves", "");
	} else {
		collection_iterate(section->slaves, (collection_f)build_slaves_string, &ctx);
		if(ctx.buffer) {
			config_write(section->config, "slaves", ctx.buffer);
			free(ctx.buffer);
			ctx.buffer = NULL;
		}
	}

	if(!collection_size(section->roots)) {
		config_write(section->config, "roots", "");
	} else {
		collection_iterate(section->roots, (collection_f)build_roots_string, &ctx);
		if(ctx.buffer) {
			config_write(section->config, "roots", ctx.buffer);
			free(ctx.buffer);
			ctx.buffer = NULL;
		}
	}

	config_save(section->config);

	return 1;
}
/*
static unsigned int unlink_slaves_from_section(struct collection *c, struct slave_ctx *slave, struct vfs_section *section) {

	section_unlink_slave(section, slave);

	return 1;
}

static unsigned int unlink_roots_from_section(struct collection *c, struct vfs_element *root, struct vfs_section *section) {

	section_unlink_root(section, root);

	return 1;
}
*/
unsigned int section_destroy(struct vfs_section *section) {

	if(!section) {
		SECTIONS_DBG("section == NULL");
		return 0;
	}

	obj_destroy(&section->o);

	return 1;
}

unsigned int section_del(struct vfs_section *section) {

	if(!section) {
		SECTIONS_DBG("section == NULL");
		return 0;
	}

	if(section->config && section->config->filename) {
		remove(section->config->filename);
	}
	
	section_destroy(section);

	return 1;
}

unsigned int section_link_slave(struct vfs_section *section, struct slave_ctx *slave) {

	if(!section || !slave) {
		SECTIONS_DBG("Params error");
		return 0;
	}

	if(collection_find(section->slaves, slave) ||
			collection_find(slave->sections, section)) {
		SECTIONS_DBG("Slave already added to section.");
		return 0;
	}

	collection_add(section->slaves, slave);
	collection_add(slave->sections, section);

	section_dump(section);

	return 1;
}

unsigned int section_unlink_slave(struct vfs_section *section, struct slave_ctx *slave) {

	if(!section || !slave) {
		SECTIONS_DBG("Params error");
		return 0;
	}

	if(!collection_find(section->slaves, slave) &&
			!collection_find(slave->sections, section)) {
		SECTIONS_DBG("Slave is not added to section.");
		return 0;
	}

	collection_delete(section->slaves, slave);
	collection_delete(slave->sections, section);

	section_dump(section);

	return 1;
}

unsigned int section_link_root(struct vfs_section *section, struct vfs_element *root) {

	if(!section || !root) return 0;

	if(root->type != VFS_FOLDER) {
		SECTIONS_DBG("Root is not a VFS_FOLDER");
		return 0;
	}

	if(root->section == section) {
		SECTIONS_DBG("Section already has this root");
		return 1;
	}

	if(root->section) {
		SECTIONS_DBG("Root already has a section");
		return 0;
	}

	collection_add(section->roots, root);
	root->section = section;

	section_dump(section);

	return 1;
}

unsigned int section_unlink_root(struct vfs_section *section, struct vfs_element *root) {

	if(!section || !root) return 0;

	if(root->type != VFS_FOLDER) {
		SECTIONS_DBG("Root is not a VFS_FOLDER");
		return 0;
	}

	if(root->section != section) {
		SECTIONS_DBG("Root is not added to this section");
		return 1;
	}

	collection_delete(section->roots, root);
	root->section = NULL;

	section_dump(section);

	return 1;
}

/* return the nearest element with a section mapped to it */
struct vfs_element *vfs_get_section_element(struct vfs_element *element) {

	if(!element) return NULL;

	while(1) {
		/* we have the nearest folder with a mapped section */
		if(element->section) break;
		if(!element->parent) return NULL;
		element = element->parent;
	}

	return element;
}

/* return the nearest section available for upload given any vfs element */
struct vfs_section *vfs_get_section(struct vfs_element *element) {

	if(!element) return NULL;

	element = vfs_get_section_element(element);

	return element ? element->section : NULL;
}


void vfs_free() {

	VFS_DBG("Unloading ...");

	/* free the sections collection */

	/* recursively free the whole file system */

	return;
}


/*
	The 'namesum' system allows a faster seeking of folders because we
	compare all characters of the name string only once in the best case,
	at the opposite of in all cases with a normal search.
*/

static unsigned int get_child_by_namesum_matcher(struct collection *c, struct vfs_element *element, void *param) {
	struct {
		const char *name;
		unsigned int namesum;
	} *ctx = param;

	if(ctx->namesum == element->namesum) {
		return (!strcasecmp(ctx->name, element->name));
	}

	return 0;
}

/* return any of the container's child by its name */
struct vfs_element *vfs_get_child_by_namesum(struct vfs_element *container, const char *name, unsigned int namesum) {
	struct {
		const char *name;
		unsigned int namesum;
	} ctx = { name, namesum };
	
	if(!container->childs) return NULL;

	return collection_match(container->childs, (collection_f)get_child_by_namesum_matcher, &ctx);
}

static unsigned int get_child_by_namesum_n_matcher(struct collection *c, struct vfs_element *element, void *param) {
	struct {
		const char *name;
		unsigned int namesum;
		unsigned int n;
	} *ctx = param;

	if(ctx->namesum == element->namesum) {
		return (!strncasecmp(ctx->name, element->name, ctx->n) && (strlen(element->name) == ctx->n));
	}

	return 0;
}

/* return any of the container's child by its name- but compare only the n first caracters */
struct vfs_element *vfs_get_child_by_namesum_n(struct vfs_element *container, const char *name, unsigned int namesum, unsigned int n) {
	struct {
		const char *name;
		unsigned int namesum;
		unsigned int n;
	} ctx = { name, namesum, n };

	if(!container->childs) return NULL;
	return collection_match(container->childs, (collection_f)get_child_by_namesum_n_matcher, &ctx);
}

/*
  Trim the name so it looks like "abc/def/ghi"
  Once a name is trimmed, it is passed to a vfs_secure* function
  that will never trim it again. It allows faster lookup since
  only the first function trims the name and the resulting string
  is used in-place by all other functions.
*/
char *vfs_trim_name(const char *name) {
	unsigned int i,j,k;
	char *normalized, *ptr, *ptr2;
	unsigned int namelen;
	
	/* sanity check */
	if(!name) {
		VFS_DBG("Param error");
		return NULL;
	}

	namelen = strlen(name);
	if(!namelen) {
		VFS_DBG("Param error");
		return NULL;
	}

	if(((*name) == '/' || ((*name) == '\\')) && !(*(name+1))) {
		return strdup("/");
	}

	/* skip prefixed / or \ */
	while((*name == '/') || (*name == '\\')) {
		name++;
		namelen--;
	}

	normalized = malloc(namelen+1);
	if(!normalized) {
		VFS_DBG("Memory error");
		return NULL;
	}
	memcpy(normalized, name, namelen+1);

	/* From now on, we can modify the name ... */

	/* turn \ into / */
	for(i=0;i<namelen;i++) {
		if(normalized[i] == '\\') {
			normalized[i] = '/';
		}
	}

	/* remove tailing / */
	while(namelen && (normalized[namelen-1] == '/')) {
		namelen--;
		normalized[namelen] = 0;
	}

	/* collapse multiple / */
	for(i=0;i<namelen;i++) {
		if(normalized[i] == '/') {
			for(j=i;normalized[j] == '/';j++);
			if(j != i) {
				for(k=i+1;j<namelen+1;k++,j++) {
					normalized[k] = normalized[j];
				}
			}
		}
	}

	/* normalized now looks like folder/subfolder/subsubfolder
		next thing we have to do is to collapse any ".." folder
		because some dumb ftp clients doesn't handle CDUP
	*/
	if(!strcasecmp(normalized, "..") || !strncasecmp(normalized, "../", 3)) {
		VFS_DBG("Illegal path");
		free(normalized);
		return NULL;
	}
	ptr2 = strchr(normalized, '/');
	if(ptr2) {
		ptr2++;
		ptr = normalized;
		do {
			if(!strchr(ptr2, '/')) {
				// ptr2 points to last folder in path
				if(!strcasecmp(ptr2, "..")) {
					// got a ..
					*ptr = 0;
				}
				break;
			} else {
				if(!strncasecmp(ptr2, "../", 3)) {
					// got a ../
					ptr2 = strchr(ptr2, '/')+1;
					j = strlen(ptr2)+1;
					for(i=0;i<j;i++) {
						*(ptr+i) = *(ptr2+i);
					}
					if(!strcasecmp(normalized, "..") || !strncasecmp(normalized, "../", 3)) {
						VFS_DBG("Illegal path");
						free(normalized);
						return NULL;
					}
					ptr2 = strchr(normalized, '/');
					if(!ptr2) break;
					ptr2++;
					ptr = normalized;
				} else {
					ptr = ptr2;
					ptr2 = strchr(ptr2, '/')+1;
				}
			}
		} while(ptr && ptr2);
	}
	
	/* remove tailing / again */
	while(namelen && (normalized[namelen-1] == '/')) {
		namelen--;
		normalized[namelen] = 0;
	}
	
	/* no empty name allowed */
	if(!namelen) {
		free(normalized);
		normalized = strdup("/");
	}

	return normalized;
}

/*
	Return a copy of the strign at position 'left' to position 'right' (incl.) from 's'.
	Does not check for validity of the parameters.
*/
char *vfs_strndup(const char *s, unsigned int left, unsigned int right) {
	char *copy;
	unsigned int namelen = (right-left)+1;

	copy = malloc(namelen+1);
	if(!copy) {
		VFS_DBG("Memory error!");
		return NULL;
	}

	memcpy(copy, s+left, namelen);
	copy[namelen] = 0;

	return copy;
}

#define IS_MAJ(_c) (((_c) >= 'A') && ((_c) <= 'Z'))

/*
	The name sum is calculated without the ending zero for more
	flexibility (less string movement)
*/
unsigned int vfs_namesum(const char *name, unsigned int length) {
	unsigned int i;
	unsigned int sum;
	unsigned char m;

	crc32_init(&sum);
	for(i=0;i<length;i++) {
		if(IS_MAJ(name[i])) {
			m = (name[i] - 'A') + 'a';
			crc32_add(&sum, (void *)&m, 1);
		} else {
			crc32_add(&sum, (void *)&name[i], 1);
		}
	}
	crc32_close(&sum);

	return sum;
}

#undef IS_MAJ

/* find an element from a secure (trimmed) name */
struct vfs_element *vfs_secure_find_element(struct vfs_element *container, const char *path) {
	struct vfs_element *element;
	char *ptr = NULL;
	unsigned int namelen = 0;
	unsigned int namesum;

	/* if there's still some '/' in the name
		it must be a relative path so we'll
		continue searching recursively */
	ptr = strchr(path, '/');
	if(ptr) {
		namelen = (ptr-path);
		ptr++;
	}
	else {
		namelen = strlen(path);
	}
	
	/* find the element in this container */
	//if(ptr) {
	namesum = vfs_namesum(path, namelen);
	element = vfs_get_child_by_namesum_n(container, path, namesum, namelen);
	/*} else {
		namesum = vfs_namesum(path, strlen(namesum));
		element = vfs_get_child_by_name(container, path);
	}*/

	if(!element) {
		/* child not found. */
		return NULL;
	}

	if(!ptr) {
		/* was the last level ... */
		return element;
	}

	return vfs_secure_find_element(element, ptr);
}

/*
recursively search for an element matching the path relative to the specified base
input example:
  /abc/123/file.doc
or:
  /abc/123/folder/
*/
struct vfs_element *vfs_raw_find_element(struct vfs_element *container, const char *path) {
	struct vfs_element *element;
	char *trimmed_path;

	if(!container) {
		VFS_DBG("Param error: container");
		return NULL;
	}	
	if(!path) {
		VFS_DBG("Param error: path");
		return NULL;
	}

	/* normalize the folder name */
	trimmed_path = vfs_trim_name(path);
	if(!trimmed_path) {
		VFS_DBG("Memory error");
		return NULL;
	}

	if(!strcmp(trimmed_path, "/")) {
		free(trimmed_path);
		return container;
	}

	element = vfs_secure_find_element(container, trimmed_path);
	free(trimmed_path);

	return element;
}

/*
recursively search for an element matching the path relative to the specified base
input example:
  /abc/123/file.doc
or:
  /abc/123/folder/
and follow any symlinks
*/
struct vfs_element *vfs_find_element(struct vfs_element *container, const char *path) {
	struct vfs_element *element;

	element = vfs_raw_find_element(container, path);
	if(!element) return NULL;

	while(element->link_to) {
		element = element->link_to;
	}

	return element;
}

unsigned int vfs_get_relative_path_length(struct vfs_element *container, struct vfs_element *element) {
	struct vfs_element *current;
//	char *buffer;
	unsigned int length = 0;
//	unsigned int namelen;

	if(!container) {
		VFS_DBG("Param error: no container");
		return 0;
	}
	if(!element) {
		VFS_DBG("Param error: no element");
		return 0;
	}

	if(!vfs_is_child(container, element)) {
		VFS_DBG("element is not a child of container, cannot get relative path!");
		return 0;
	}
	
	current = element;
	do {
		length += strlen(current->name) + 2;
		current = current->parent;
	} while(current && (current != container));

	return length;;
}

char *vfs_get_relative_path(struct vfs_element *container, struct vfs_element *element) {
	struct vfs_element *current;
	char *buffer;
	unsigned int length = 0;
	unsigned int currlen = 0;
	unsigned int namelen;

	if(!container) {
		VFS_DBG("Param error: no container");
		return NULL;
	}
	if(!element) {
		VFS_DBG("Param error: no element");
		return NULL;
	}

	if(!vfs_is_child(container, element)) {
		VFS_DBG("element is not a child of container, cannot get relative path!");
		return NULL;
	}
	
	current = element;
	do {
		length += strlen(current->name) + 2;
		current = current->parent;
	} while(current && (current != container));

	buffer = malloc(length);
	if(!buffer) {
		VFS_DBG("Memory error");
		return NULL;
	}

	currlen = length-1;
	buffer[currlen] = 0;
	current = element;
	do {
		namelen = strlen(current->name);
		memcpy(&buffer[currlen-namelen], current->name, namelen);

		currlen -= namelen;
		if(buffer[currlen] != '/') {
			/* if there's not already one, set a / in front of the name */
			buffer[currlen-1] = '/';
			currlen--;
		}

		current = current->parent;
	} while(current && (current != container));

	/*
		we have to move the path at the beginning of the buffer,
		at this point it's still at the complete end of the buffer
		because we printed it recursively.
	*/
	memmove(buffer, &buffer[currlen], length-currlen);

	return buffer;
}

struct vfs_element *vfs_secure_create_folder(struct vfs_element *container, const char *name, const char *owner);

/*
	Create a file inside the specified container.
	Work recursively, if name is /x/y/z/w.dat then
	the whole tree will be created with the same
	specified owner.
*/
struct vfs_element *vfs_create_file(struct vfs_element *container, const char *name, const char *owner) {
	struct vfs_element *element;
	unsigned int namesum;
	char *trimmed_name;
	char *ptr;

	if(!container || !name || !owner) return NULL;

	/* follow the symlink if needed */
	if(container->type != VFS_FOLDER) {
		VFS_DBG("create_file called on non-VFS_FOLDER type");
		return NULL;
	}

	/* normalize the folder name */
	trimmed_name = vfs_trim_name(name);
	if(!trimmed_name) {
		VFS_DBG("Memory error");
		return NULL;
	}
	if(!strcmp(trimmed_name, "/")) {
		free(trimmed_name);
		return NULL;
	}

	/* if there is some / in the name, there is
		a folder name before the file name so
		we will need create the folder before
		creating the file */
	ptr = strchr(trimmed_name, '/');
	if(ptr) {
		while(strchr(ptr+1, '/')) ptr = strchr(ptr+1, '/');
		*ptr = 0;
		ptr++;

		/* create the container at this level */
		container = vfs_secure_create_folder(container, trimmed_name, owner);
		*(ptr-1) = '/';

		if(!container) {
			VFS_DBG("Could not create sub-element.");
			free(trimmed_name);
			return NULL;
		}
	}
	else
		ptr = trimmed_name;

	namesum = vfs_namesum(ptr, strlen(ptr));

	element = vfs_get_child_by_namesum(container, ptr, namesum);
	if(element) {
		if(element->type != VFS_FILE) {
			VFS_DBG("Trying to create a file that already exist as non-VFS_FILE.");
			free(trimmed_name);
			return NULL;
		}
		free(trimmed_name);
		return element;
	}

	element = malloc(sizeof(struct vfs_element));
	if(!element) {
		VFS_DBG("Memory error");
		free(trimmed_name);
		return NULL;
	}

	obj_init(&element->o, element, (obj_f)vfs_obj_destroy);
	collectible_init(element);

	element->namesum = namesum;
	element->name = strdup(ptr);
	free(trimmed_name);

	if(!element->name) {
		VFS_DBG("Memory error");
		free(element);
		return NULL;
	}

	/* files don't have childs */
	element->childs = NULL;

	element->available_from = collection_new(C_NONE);
	element->offline_from = collection_new(C_NONE);

	/* don't have an uploader yet */
	element->uploader = NULL;

	element->leechers = collection_new(C_CASCADE);
	element->mirror_from = collection_new(C_CASCADE);
	element->mirror_to = collection_new(C_CASCADE);
	element->link_from = collection_new(C_CASCADE);
	collection_add(container->childs, element);
	
	element->parent = container;
	element->type = VFS_FILE;
	element->section = NULL;
	element->owner = (owner && *owner) ? strdup(owner) : NULL;
	element->size = 0;
	element->timestamp = 0;
	element->sfv = NULL;
	element->checksum = 0;
	element->link_to = NULL;
	element->browsers = NULL;
	element->vrooted = NULL;
	element->nuke = NULL;
	element->xfertime = 0;
	//element->destroyed = 0;

	return element;
}

/* create a symlink in the vfs ith a secure (trimmed) name */
struct vfs_element *vfs_secure_create_symlink(struct vfs_element *container, struct vfs_element *target, char *name, const char *owner) {
	struct vfs_element *element;
	unsigned int namesum;
	char *ptr;

	if(!container || !name || !owner) return NULL;

	/* follow the symlink if needed */
	if(container->type != VFS_FOLDER) {
		VFS_DBG("Trying to create symlink in non-VFS_FOLDER container.");
		return NULL;
	}

	/* if there is some / in the name, there is
		a folder name before the file name so
		we will need create the folder before
		creating the file */
	ptr = strchr(name, '/');
	if(ptr) {
		while(strchr(ptr+1, '/')) ptr = strchr(ptr+1, '/');
		*ptr = 0;
		ptr++;

		/* create the base folder */
		container = vfs_secure_create_folder(container, name, owner);
		*(ptr-1) = '/';

		if(!container) {
			VFS_DBG("Could not create sub-element.");
			//free(name);
			return NULL;
		}
	}
	else
		ptr = name;

	namesum = vfs_namesum(ptr, strlen(ptr));

	element = vfs_get_child_by_namesum(container, ptr, namesum);
	if(element) {
		if(element->type != VFS_LINK) {
			VFS_DBG("Trying to create a file that already exist as non-VFS_LINK.");
			//free(trimmed_name);
			return NULL;
		}
		return element;
	}

	element = malloc(sizeof(struct vfs_element));
	if(!element) {
		VFS_DBG("Memory error");
		//free(name);
		return NULL;
	}

	obj_init(&element->o, element, (obj_f)vfs_obj_destroy);
	collectible_init(element);

	/* for a symlink, we need the name & the link_to only */

	element->namesum = namesum;
	element->name = strdup(ptr);
	//free(name);
	if(!element->name) {
		VFS_DBG("Memory error");
		free(element);
		return NULL;
	}
	
	element->owner = strdup(owner);
	if(!element->owner) {
		VFS_DBG("Memory error");
		free(element->name);
		free(element);
		return NULL;
	}

	element->parent = container;
	element->section = NULL;
	element->type = VFS_LINK;
	element->size = 0;
	element->timestamp = 0;
	element->childs = NULL;
	element->offline_from = NULL;	
	element->available_from = NULL;
	element->uploader = NULL;
	element->leechers = NULL;
	element->checksum = 0;
	element->sfv = NULL;
	element->mirror_from = NULL;
	element->mirror_to = NULL;
	element->link_from = NULL;
	element->link_to = target;
	element->browsers = NULL;
	element->vrooted = NULL;
	element->nuke = NULL;
	element->xfertime = 0;
	//element->destroyed = 0;

	collection_add(container->childs, element);
	collection_add(target->link_from, element);

	return element;
}

struct vfs_element *vfs_create_symlink(struct vfs_element *container, struct vfs_element *target, const char *name, const char *owner) {
	struct vfs_element *element;
	char *trimmed_name;

	if(!container || !name || !owner) return NULL;

	/* follow the symlink if needed */
	if(container->type != VFS_FOLDER) {
		VFS_DBG("Trying to create symlink in non-VFS_FOLDER container.");
		return NULL;
	}

	/* normalize the folder name */
	trimmed_name = vfs_trim_name(name);
	if(!trimmed_name) {
		VFS_DBG("Memory error");
		return NULL;
	}
	if(!strcmp(trimmed_name, "/")) {
		free(trimmed_name);
		return container;
	}

	/*element = vfs_raw_find_element(container, name);
	if(element) {
		return element;
	}*/

	element = vfs_secure_create_symlink(container, target, trimmed_name, owner);
	free(trimmed_name);

	return element;
}

/* create a folder with a secure (trimmed) name */
struct vfs_element *vfs_secure_create_folder(struct vfs_element *container, const char *name, const char *owner) {
	struct vfs_element *element;
	unsigned int namesum;
	unsigned int namelen = 0;
	char *ptr = NULL;

	ptr = strchr(name, '/');
	if(ptr) {
		namelen = (ptr-name);
		ptr++;
	}
	else
		namelen = strlen(name);

	/* check if the folder we want exists in this one */
	//if(ptr) {
		namesum = vfs_namesum(name, namelen);
		element = vfs_get_child_by_namesum_n(container, name, namesum, namelen);
	/*} else {
		element = vfs_get_child_by_name(container, name);
	}*/

	if(element) {
		if(element->type != VFS_FOLDER) {
			VFS_DBG("Trying to create folder wich already exists as non-VFS_FOLDER type.");
			//if(ptr) free(name);
			return NULL;
		}
	} else {

		/* create the element now */
		element = malloc(sizeof(struct vfs_element));
		if(!element) {
			VFS_DBG("Memory error");
			//if(ptr) free(name);
			return NULL;
		}

		obj_init(&element->o, element, (obj_f)vfs_obj_destroy);
		collectible_init(element);

		//if(ptr) {
			element->namesum = namesum;
			element->name = vfs_strndup(name, 0, namelen-1);
		//} else {
			//element->name = strdup(name);
		//}
		if(!element->name) {
			VFS_DBG("Memory error");
			//if(ptr) free(name);
			free(element);
			return NULL;
		}

		/* folders don't have leechers nor uploaders */
		element->leechers = NULL;
		element->uploader = NULL;

		element->childs = collection_new(C_CASCADE);
		element->link_from = collection_new(C_CASCADE);
		element->browsers = collection_new(C_CASCADE);
		collection_add(container->childs, element);

		/* folders are not available for download */
		element->available_from = NULL;
		
		element->parent = container;
		element->type = VFS_FOLDER;
		element->section = NULL;
		element->owner = (owner && *owner) ? strdup(owner) : strdup("xFTPd");
		element->size = 0;
		element->timestamp = 0;
		element->sfv = NULL;
		element->checksum = 0;
		element->mirror_from = NULL;
		element->mirror_to = NULL;
		element->offline_from = NULL;
		element->link_to = NULL;
		element->vrooted = NULL;
		element->nuke = NULL;
		element->xfertime = 0;
		//element->destroyed = 0;
	}

	if(!ptr) {
		return element;
	}

	/* if 'ptr' is not null, there is another folder to create inside this one */
	element = vfs_secure_create_folder(element, ptr, owner);

	return element;
}

/*
	create a folder in the specified container
	work recursively, if name is /x/y/z/w then
	the whole tree will be created with the same
	user as the owner
*/
struct vfs_element *vfs_create_folder(struct vfs_element *container, const char *name, const char *owner) {
	struct vfs_element *element;
	char *trimmed_name;

	if(!container || !name || !owner) return NULL;

	if(container->type != VFS_FOLDER) {
		VFS_DBG("Trying to create folder in non-VFS_FOLDER container.");
		return NULL;
	}

	/* normalize the folder name */
	trimmed_name = vfs_trim_name(name);
	if(!trimmed_name) {
		VFS_DBG("Memory error");
		return NULL;
	}

	if(!strcmp(trimmed_name, "/")) {
		free(trimmed_name);
		return container;
	}

	/*element = vfs_secure_find_element(container, name);
	if(element) {
		return element;
	}*/

	element = vfs_secure_create_folder(container, trimmed_name, owner);
	free(trimmed_name);

	return element;
}
/*
static unsigned int recursive_deletion_callback(struct collection *c, struct vfs_element *element, void *param) {

	vfs_recursive_delete(element);

	return 1;
}
*/
/*
	This deletion routine will be called on every of this element's
	childs.

	There's basicly three way this function get called.
	1. From the ftpd (DELE send by client)
	2. From this function, recursively.
	3. From LUA (wiping from irc, from site ...)
*/
unsigned int vfs_recursive_delete(struct vfs_element *element) {

	/* if there's no parent then we're deleting the root OR
		we're recursively deleting this file into one of
		the sub-functions called by this one, so we'd
		better not continue either ways */
	if(!element->parent) {
		VFS_DBG("ELEMENT HAS NO PARENT: CANNOT DELETE THE ROOT ELEMENT");
		return 0;
	}
	
	/* void all collections */
	if(element->childs) collection_void(element->childs);
	if(element->offline_from) collection_void(element->offline_from);
	if(element->available_from) collection_void(element->available_from);
	if(element->leechers) collection_void(element->leechers);
	if(element->mirror_from) collection_void(element->mirror_from);
	if(element->mirror_to) collection_void(element->mirror_to);
	if(element->browsers) collection_void(element->browsers);
	if(element->link_from) collection_void(element->link_from);

	//VFS_DBG("signaling %s for destruction", element->name);
	
	obj_destroy(&element->o);

	return 1;
}

/* delete any vfs element by its path */
unsigned int vfs_delete(struct vfs_element *container, char *path, unsigned int recursive) {
	struct vfs_element *element;

	/* find the specified element */
	element = vfs_find_element(container, path);
	if(!element) {
		VFS_DBG("Element could not be found.");
		return 0;
	}

	/* return an error if the deletion is not recursive and childs are present */
	if(!recursive && collection_size(element->childs)) {
		VFS_DBG("Element is not empty !");
		return 0;
	}

	/* delete the element */
	vfs_recursive_delete(element);

	return 1;
}

/* set the size of an element and all its parent elements */
unsigned int vfs_set_size(struct vfs_element *element, unsigned long long int size) {

	vfs_decrease_size(element, element->size);
	vfs_increase_size(element, size);

	return 1;
}

/* increase the size of an element and all its parent elements */
unsigned int vfs_increase_size(struct vfs_element *element, unsigned long long int size) {

	element->size += size;
	if(element->parent) vfs_increase_size(element->parent, size);

	return 1;
}

/* decrease the size of an element and all its parent elements */
unsigned int vfs_decrease_size(struct vfs_element *element, unsigned long long int size) {

	element->size -= size;
	if(element->parent) vfs_decrease_size(element->parent, size);

	return 1;
}

/* set the new timestamp for the element and all its parent elements */
unsigned int vfs_modify(struct vfs_element *element, unsigned long long int timestamp) {

	if(!element) {
		VFS_DBG("Params error");
		return 0;
	}
	
	/* don't allow to set an older
		timestamp than what it is right now */
	if(timestamp < element->timestamp) {
		return 0;
	}

	element->timestamp = timestamp;
	if(element->parent) vfs_modify(element->parent, timestamp);

	return 1;
}

/* set the checksum for an element */
unsigned int vfs_set_checksum(struct vfs_element *element, unsigned int checksum) {

	if(!element) return 0;

	element->checksum = checksum;

	return 1;
}

/* set the checksum for an element */
unsigned int vfs_get_checksum(struct vfs_element *element) {

	if(!element) return 0;

	return element->checksum;
}

/* return 1 if 'child' is a child of 'parent' */
unsigned int vfs_is_child(struct vfs_element *parent, struct vfs_element *child) {
	struct vfs_element *current;
	
	current = child;
	while(current) {
		if(current == parent) {
			/* 'child' is a child of 'parent' */
			return 1;
		}
		current = current->parent;
	}

	return 0;
}
