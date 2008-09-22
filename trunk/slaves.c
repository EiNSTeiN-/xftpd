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
#include <stdio.h>
#include <poll.h>
#include <stdio.h>
#include <fcntl.h>
#include <io.h>

#include "main.h"
#include "collection.h"
#include "config.h"
#include "constants.h"
#include "logging.h"
#include "io.h"
#include "socket.h"
#include "slaves.h"
#include "fsd.h"
#include "vfs.h"
#include "ftpd.h"
#include "events.h"
#include "time.h"
#include "sfv.h"
#include "mirror.h"
#include "main.h"
#include "signal.h"
#include "nuke.h"
#include "asynch.h"

struct collection *slaves = NULL; /* slave_ctx structures */

struct collection *connecting_slaves = NULL; /* slave_connection waiting to be identified */
struct collection *connected_slaves = NULL; /* slave_connection waiting ready for i/o */

static unsigned int slaves_port = 0;
static struct collection *slaves_group = NULL;
static int slaves_fd = -1;

static unsigned int slaves_use_encryption = 0;

static unsigned int slaves_use_compression = 0;
static unsigned int slaves_compression_threshold = 500;

int slaves_load_config() {
	struct slave_ctx *slave;
	struct _finddata_t fd;
	intptr_t handle;
	char *path, *file;

	path = _fullpath(NULL, ".", 0);
	if(!path)
		return 0;

	file = malloc(strlen(path) + strlen(SLAVES_FOLDER "slave.*") + 1);
	if(!file) {
		free(path);
		return 0;
	}
	sprintf(file, "%s" SLAVES_FOLDER "slave.*", path);
	free(path);

	handle = _findfirst(file, &fd);
	free(file);
	if (handle == -1) {
		SLAVES_DBG("Couldn't load \"" SLAVES_FOLDER "slave.*\"");
	} else {
		do {
			if(!strcmp(fd.name, ".") || !strcmp(fd.name, "..")) continue;
			if(fd.attrib & _A_SUBDIR) continue;
			if((strlen(fd.name) > 4) && !stricmp(&fd.name[strlen(fd.name)-4], ".tmp")) continue;
			if((strlen(fd.name) > 10) && !stricmp(&fd.name[strlen(fd.name)-10], ".deletelog")) continue;
			if((strlen(fd.name) > 9) && !stricmp(&fd.name[strlen(fd.name)-9], ".fileslog")) continue;

			slave = slave_load(fd.name);
			if(!slave) {
				SLAVES_DBG("Could not load file: %s", fd.name);
				continue;
			}
		} while(!_findnext(handle, &fd));
		_findclose(handle);
	}

	SLAVES_DBG("Added %u slaves", collection_size(slaves));

	slaves_use_encryption = config_raw_read_int(MASTER_CONFIG_FILE, "xftpd.encryption", SLAVES_USE_ENCRYPTION);
	slaves_use_compression = config_raw_read_int(MASTER_CONFIG_FILE, "xftpd.compression", SLAVES_USE_COMPRESSION);
	slaves_compression_threshold = config_raw_read_int(MASTER_CONFIG_FILE, "xftpd.compression.threshold", SLAVES_COMPRESSION_THRESHOLD);

	/* prepare the slave socket */
	slaves_port = config_raw_read_int(MASTER_CONFIG_FILE, "xftpd.slaves.port", SLAVES_PORT);

	return 1;
}

static int slave_element_usage_callback(struct collection *c, struct vfs_element *child, void *param) {
	struct {
		struct slave_connection *cnx;
		unsigned int size;
	} *ctx = param;
	
	/* recursive call on every childs. */
	ctx->size += slave_usage_from(ctx->cnx, child);
	
	return 1;
}

unsigned int slave_usage_from(struct slave_connection *cnx, struct vfs_element *element) {
	struct {
		struct slave_connection *cnx;
		unsigned int size;
	} ctx = { cnx, 0 };
	
	if(element->type == VFS_FOLDER) {
		collection_iterate(element->childs, (collection_f)slave_element_usage_callback, &ctx);
	}
	else if(element->type == VFS_FILE) {
		if(collection_find(element->available_from, cnx)) {
			ctx.size += element->size;
		}
	}
	
	return ctx.size;
}

const char *slave_address(struct slave_connection *cnx) {

	if(!cnx) {
		SLAVES_DBG("Called slave_address with cnx == NULL");
		return "unknown";
	}

	return socket_formated_peer_address(cnx->io.fd);
}

/* add all files in the fileslog to the vfs */
unsigned int slave_load_fileslog(struct slave_ctx *slave) {
	unsigned int fsize, current, line;
	char *buffer;
	//int f;

	char *ptr, *next;
	char *path, *_size, *_time, *owner, *_checksum;
	unsigned long long int size, time;
	unsigned int checksum;
	struct vfs_element *file;

	unsigned long long int t;

	/* read line by line and add all files to the vfs */
	/* TODO: optionally use compression */

	buffer = config_load_file(slave->fileslog, &fsize);
	if(!buffer) {
		//SLAVES_DBG("Could not open fileslog for %s", slave->name);
		return 0;
	}

	t = time_now();

	current=0;
	next = buffer;
	while(current<fsize) {
		ptr = next;

		for(line=0;current+(line+1)<fsize;line++) {
			if((ptr[line] == '\r') || (ptr[line] == '\n')) {
				ptr[line] = 0;
				break;
			}
		}

		current += (line+1);
		next = ptr + (line+1);

		if(!line)
			continue;

		/* extract all infos from the file */

		path = ptr;
		ptr = strchr(ptr, ';');
		if(!ptr || !(*path)) continue;
		*ptr = 0; ptr++;

		_size = ptr;
		ptr = strchr(ptr, ';');
		if(!ptr || !(*_size)) continue;
		*ptr = 0; ptr++;

		_time = ptr;
		ptr = strchr(ptr, ';');
		if(!ptr || !(*_time)) continue;
		*ptr = 0; ptr++;

		owner = ptr;
		ptr = strchr(ptr, ';');
		if(!ptr || !(*owner)) continue;
		*ptr = 0; ptr++;

		_checksum = ptr;
		if(!(*_checksum)) continue;

		size = _atoi64(_size);
		time = _atoi64(_time);
		checksum = atoi(_checksum);

		file = vfs_create_file(slave->vroot, path, owner);
		if(!file) {
			SLAVES_DBG("COULD NOT CREATE FILE: %s", path);
			continue;
		}

		vfs_set_size(file, size);
		if(time > time_now()) {
			time = time_now();
		}

		vfs_modify(file, time);

		vfs_set_checksum(file, checksum);

		//SLAVES_DBG("Making %s available from %s", file->name, slave->name);
		if(!collection_add_first(slave->offline_files, file)) {
			SLAVES_DBG("Collection error");
			continue;
		}
		if(!collection_add_first(file->offline_from, slave)) {
			collection_delete(slave->offline_files, file);
			SLAVES_DBG("Collection error");
			continue;
		}
	}

	free(buffer);

	t = timer(t);
	SLAVES_DBG("Fileslog for %s loaded in %I64u ms", slave->name, t);

	return 1;
}

unsigned int slave_dump_fileslog_callback(struct collection *c, void *item, void *param) {
	struct vfs_element *file = item;
	char *path;
	struct {
		struct slave_ctx *slave;
		FILE *f;
	} *ctx = param;

	path = vfs_get_relative_path(ctx->slave->vroot, file);
	if(!path) {
		SLAVES_DBG("Memory error");
		return 1;
	}

	logging_write_file(ctx->f, "%s;%I64u;%I64u;%s;%u\n",
		path, file->size, file->timestamp, file->owner, file->checksum);

	free(path);

	return 1;
}

/* dump all files from the slave to fileslog */
unsigned int slave_dump_fileslog(struct slave_ctx *slave) {
	//unsigned long long int time;
	struct {
		struct slave_ctx *slave;
		FILE *f;
	} ctx = { slave, NULL };

	if(!slave) return 0;

	//time = time_now();

	ctx.f = fopen(slave->fileslog, "w");
	if(!ctx.f) {
		SLAVES_DBG("Could not open %s for writing", slave->fileslog);
		return 0;
	}

	/* iterate the available_files list of this connection and print everything to file  */
	collection_iterate(slave->offline_files, slave_dump_fileslog_callback, &ctx);
	
	if(slave->cnx) {
		collection_iterate(slave->cnx->available_files, slave_dump_fileslog_callback, &ctx);
	}

	fclose(ctx.f);

	/*time = timer(time);

	if(time) {
		SLAVES_DBG("Fileslog dumped in %I64u ms for %s", time, slave->name);
	}*/

	return 1;
}

/* add to the availability list & remove from the offline list */
unsigned int slave_mark_online_from(struct slave_connection *cnx, struct vfs_element *file) {

	//SLAVES_DBG("Making %s online from %s", file->name, cnx->slave->name);

	if(!collection_add_first(cnx->available_files, file)) {
		SLAVES_DBG("Collection error");
		return 1;
	}
	if(!collection_add_first(file->available_from, cnx)) {
		SLAVES_DBG("Collection error");
		collection_delete(cnx->available_files, file);
		return 1;
	}

	if(cnx->slave) {
		if(collection_find(cnx->slave->offline_files, file)) {
			collection_delete(cnx->slave->offline_files, file);
		}
		if(collection_find(file->offline_from, cnx->slave)) {
			collection_delete(file->offline_from, cnx->slave);
		}
	}

	return 1;
}

/* remove from the availability list & add to the offline list */
unsigned int slave_mark_offline_from(struct slave_ctx *slave, struct vfs_element *file) {
	
	//SLAVES_DBG("Making %s offline from %s", file->name, slave->name);

	if(slave->cnx) {
		if(collection_find(slave->cnx->available_files, file)) {
			collection_delete(slave->cnx->available_files, file);
		}
		if(collection_find(file->available_from, slave->cnx)) {
			collection_delete(file->available_from, slave->cnx);
		}
	}

	if(!collection_add_first(slave->offline_files, file)) {
		SLAVES_DBG("Collection error");
		return 1;
	}
	if(!collection_add_first(file->offline_from, slave)) {
		SLAVES_DBG("Collection error");
		collection_delete(slave->offline_files, file);
		return 1;
	}

	return 1;
}

/* enqueue a file for deletion in the deletelog */
unsigned int slave_offline_delete(struct slave_ctx *slave, struct vfs_element *file, int log) {
	char *path;

	/* element is not a file */
	if(file->type != VFS_FILE) {
		SLAVES_DBG("Called slaves_offline_delete with non-VFS_FILE type");
		return 0;
	}

	/* delete the connection from the offline_from list */
	collection_delete(slave->offline_files, file);
	collection_delete(file->offline_from, slave);

	/* get the full path to the file */
	if(log) {
		path = vfs_get_relative_path(slave->vroot, file);
		if(!path) return 0;

		logging_write(slave->deletelog, "%s\n", path);

		free(path);
	}

	return 1;							  
}

unsigned int slave_remove_deletelog_files(struct slave_ctx *slave) {
	unsigned int fsize, current, line;
	struct vfs_element *file;
	char *buffer;

	char *next, *ptr;
	unsigned int count = 0;

	/* read line by line and add all files to the vfs */
	/* the following infos should be present:
		- full path
		- file size
		- creation time
		- owner
		- checksum		*if available
	*/

	/* TODO: optionally use compression?? */

	buffer = config_load_file(slave->deletelog, &fsize);
	if(!buffer) {
		//SLAVES_DBG("Could not open deletelog for %s", slave->name);
		return 0;
	}

	current=0;
	next = buffer;
	while(current<fsize) {
		ptr = next;

		for(line=0;current+(line+1)<fsize;line++) {
			if((ptr[line] == '\r') || (ptr[line] == '\n')) {
				ptr[line] = 0;
				break;
			}
		}

		current += (line+1);
		next = ptr + (line+1);

		if(!line)
			continue;

		/* try to find the file in the vfs */
		file = vfs_find_element(slave->vroot, ptr);
		if(file) {

			/* element is not a file */
			if(file->type != VFS_FILE) {
				SLAVES_DBG("File with non-VFS_FILE type was in the deletelog!");
				continue;
			}
			
			/* file was found, delete it from this slave */
			if(collection_find(file->offline_from, slave)) {
				SLAVES_DBG("File %s is in %s's fileslog but is also in the deletelog!", ptr, slave->name);
				slave_offline_delete(slave, file, 0);
				
				/* if the file is available from nowhere anymore, remove it */
				if(!collection_size(file->offline_from) && !collection_size(file->available_from)) {
					SLAVES_DBG("File %s is not available from anywhere anymore, deleting...", ptr);
					vfs_recursive_delete(file);

					// TODO: cleanup the whole directory tree if it's all empty
				}

				count++;
			}
			
		}
	}

	free(buffer);

	return count;
}

struct slave_ctx *slave_load(const char *filename) {
	struct slave_ctx *slave;
	char *name;
	char *path;
	char *fullpath;
	char *fileslog;
	char *deletelog;

	path = _fullpath(NULL, ".", 0);
	if(!path) {
		SLAVES_DBG("Memory error");
		return NULL;
	}

	fullpath = malloc(strlen(path) + strlen(SLAVES_FOLDER) + strlen(filename) + 1);
	if(!fullpath) {
		SLAVES_DBG("Memory error");
		free(path);
		return NULL;
	}
	sprintf(fullpath, "%s" SLAVES_FOLDER "%s", path, filename);

	fileslog = malloc(strlen(path) + strlen(SLAVES_FOLDER) + strlen(filename) + strlen(".fileslog") + 1);
	if(!fileslog) {
		SLAVES_DBG("Memory error");
		free(fullpath);
		free(path);
		return NULL;
	}
	sprintf(fileslog, "%s" SLAVES_FOLDER "%s.fileslog", path, filename);

	deletelog = malloc(strlen(path) + strlen(SLAVES_FOLDER) + strlen(filename) + strlen(".deletelog") + 1);
	if(!deletelog) {
		SLAVES_DBG("Memory error");
		free(fileslog);
		free(fullpath);
		free(path);
		return NULL;
	}
	sprintf(deletelog, "%s" SLAVES_FOLDER "%s.deletelog", path, filename);
	free(path);

	/* get the section name */
	name = config_raw_read(fullpath, "name", NULL);
	if(!name) {
		SLAVES_DBG("No name field present in slave's config file??");
		free(deletelog);
		free(fileslog);
		free(fullpath);
		return NULL;
	}

	slave = malloc(sizeof(struct slave_ctx));
	if(!slave) {
		SLAVES_DBG("Memory error");
		free(deletelog);
		free(fileslog);
		free(fullpath);
		free(name);
		return NULL;
	}

	slave->config = config_new(fullpath, SLAVES_CONFIG_TIMEOUT);
	free(fullpath);
	if(!slave->config) {
		free(deletelog);
		free(fileslog);
		free(name);
		return NULL;
	}

	slave->fileslog = fileslog;
	slave->deletelog = deletelog;

	{
		char *foldername;

		foldername = config_read(slave->config, "virtual-root", NULL);
		if(foldername) {
			struct vfs_element *element;

			element = vfs_find_element(vfs_root, foldername);
			if(!element) {
				element = vfs_create_folder(vfs_root, foldername, "virtual-root");
				if(element) {
					slave->vroot = element;
					element->vrooted = slave;
				}
				else
					slave->vroot = vfs_root;
			}
			else
				slave->vroot = vfs_root;
			free(foldername);
		}
		else
			slave->vroot = vfs_root;
	}

	slave->cnx = NULL;
	slave->name = name;

	slave->lastonline = config_read_int(slave->config, "last-online", 0);

	slave->sections = collection_new();
	slave->offline_files = collection_new();
	collection_add(slaves, slave);

	/* try to load the fileslog */
	slave_load_fileslog(slave);

	/*
		Remove all files that are in the deletelog
		and that could still be in the fileslog.
		It may happens if the server has crashed
		and the fileslog could not be updated in
		time.
	*/
	if(slave_remove_deletelog_files(slave)) {

		/*
			Here we detected some files that were
			in the fileslog AND in the deletelog,
			so we dump the fileslog now in order
			to not waste any time with it later
		*/
		SLAVES_DBG("Fileslog was not valid, dumping now!");

		slave_dump_fileslog(slave);
	}

	return slave;
}

int slave_set_virtual_root(struct slave_ctx *slave, struct vfs_element *vroot) {

	if(slave->vroot == vroot) {
		SLAVES_DBG("Trying to set the same vroot");
		return 1;
	}

	/*
		We can't set a new vroot if the slave has any files already.
	*/

	if(collection_size(slave->offline_files) ||
			(slave->cnx && collection_size(slave->cnx->available_files))) {
		SLAVES_DBG("Setting vroot on slave with files already mapped");
		return 1;
	}

	slave->vroot = vroot;

	{
		char *path;

		path = vfs_get_relative_path(vfs_root, vroot);
		config_write(slave->config, "virtual-root", path);
		free(path);
	}

	return 1;
}

struct slave_ctx *slave_new(const char *name) {
	struct slave_ctx *slave;
	char filename[128];
	char *path;
	char *fullpath;
	char *fileslog;
	char *deletelog;

	if(!name) return NULL;

	sprintf(filename, "slave.%I64u", time_now());

	path = _fullpath(NULL, ".", 0);
	if(!path) {
		SLAVES_DBG("Memory error");
		return NULL;
	}

	fullpath = malloc(strlen(path) + strlen(SLAVES_FOLDER) + strlen(filename) + 1);
	if(!fullpath) {
		SLAVES_DBG("Memory error");
		free(path);
		return NULL;
	}
	sprintf(fullpath, "%s" SLAVES_FOLDER "%s", path, filename);

	fileslog = malloc(strlen(path) + strlen(SLAVES_FOLDER) + strlen(filename) + strlen(".fileslog") + 1);
	if(!fileslog) {
		SLAVES_DBG("Memory error");
		free(fullpath);
		free(path);
		return NULL;
	}
	sprintf(fileslog, "%s" SLAVES_FOLDER "%s.fileslog", path, filename);

	deletelog = malloc(strlen(path) + strlen(SLAVES_FOLDER) + strlen(filename) + strlen(".deletelog") + 1);
	if(!deletelog) {
		SLAVES_DBG("Memory error");
		free(fileslog);
		free(fullpath);
		free(path);
		return NULL;
	}
	sprintf(deletelog, "%s" SLAVES_FOLDER "%s.deletelog", path, filename);
	free(path);

	slave = malloc(sizeof(struct slave_ctx));
	if(!slave) {
		SLAVES_DBG("Memory error");
		free(deletelog);
		free(fileslog);
		free(fullpath);
		return NULL;
	}

	slave->config = config_new(fullpath, SLAVES_CONFIG_TIMEOUT);
	free(fullpath);
	if(!slave->config) {
		SLAVES_DBG("Memory error");
		free(deletelog);
		free(fileslog);
		free(slave);
		return NULL;
	}
	
	slave->vroot = vfs_root;

	slave->deletelog = deletelog;
	slave->fileslog = fileslog;

	slave->lastonline = 0;
	config_write_int(slave->config, "last-online", 0);

	slave->cnx = NULL;
	slave->name = strdup(name);
	if(!slave->name) {
		SLAVES_DBG("Memory error");
		free(slave->deletelog);
		free(slave->fileslog);
		config_destroy(slave->config);
		free(slave);
		return NULL;
	}
	config_write(slave->config, "name", name);

	slave->sections = collection_new();
	slave->offline_files = collection_new();
	collection_add(slaves, slave);

	slave_dump(slave);

	SLAVES_DBG("Slave added: %s", slave->name);

	return slave;
}

static unsigned int slave_get_matcher(struct collection *c, struct slave_ctx *slave, char *name) {

	return !stricmp(name, slave->name);
}

/* return a slave by its name */
struct slave_ctx *slave_get(const char *name) {
	struct slave_ctx *slave;

	if(!name) return NULL;

	slave = collection_match(slaves, (collection_f)slave_get_matcher, (char *)name);

	return slave;
}

/* save the slave infos to file */
unsigned int slave_dump(struct slave_ctx *slave) {
	char *path;

	if(!slave) return 0;

	config_write(slave->config, "name", slave->name);
	config_write_int(slave->config, "last-online", slave->lastonline);

	path = vfs_get_relative_path(vfs_root, slave->vroot);
	if(path) {
		config_write(slave->config, "virtual-root", path);
		free(path);
	}

	config_save(slave->config);

	return 1;
}

/* save the slave infos to file */
unsigned int slave_del(struct slave_ctx *slave) {

	if(!slave) return 0;

	/* kick the connected slave */
	if(slave->cnx) {
		slave_connection_destroy(slave->cnx);
		slave->cnx = NULL;
	}

	collection_delete(slaves, slave);

	/* delete the file */
	if(slave->config) {
		remove(slave->config->filename);
		config_destroy(slave->config);
		slave->config = NULL;
	}
	if(slave->fileslog) {
		remove(slave->fileslog);
		free(slave->fileslog);
		slave->fileslog = NULL;
	}
	if(slave->deletelog) {
		remove(slave->deletelog);
		free(slave->deletelog);
		slave->deletelog = NULL;
	}

	if(slave->name) {
		free(slave->name);
		slave->name = NULL;
	}

	if(slave->sections) {
		collection_void(slave->sections);
		while(collection_size(slave->sections)) {
			void *first = collection_first(slave->sections);
			section_unlink_slave(first, slave);
			collection_delete(slave->sections, first);
		}
		collection_destroy(slave->sections);
		slave->sections = NULL;
	}

	free(slave);

	return 1;
}

/* p is NULL on timeout and on read error */
static unsigned int delete_query_callback(struct slave_connection *cnx, struct slave_asynch_command *cmd, struct packet *p) {

	if(!p) {
		SLAVES_DBG("%I64u: No good packet received.", cmd->uid);
		return 1;
	}

	SLAVES_DIALOG_DBG("%I64u: Delete query response received", p->uid);

	if(p->type == IO_FAILURE) {
		/* general failure status: slave couln't transfer the file */

		SLAVES_DBG("%I64u: File could NOT be deleted from the remote slave.", p->uid);
		
		return 1;
	}

	SLAVES_DIALOG_DBG("%I64u: Remote slave deleted file with no problem", p->uid);

	return 1;
}

unsigned int slave_delete_file(struct slave_connection *cnx, struct vfs_element *element) {
	struct slave_asynch_command *cmd;
	char *filepath;

	if(element->type != VFS_FILE) {
		SLAVES_DBG("Slave delete file called on non-VFS_FILE element.");
		return 0;
	}

	filepath = vfs_get_relative_path(cnx->slave->vroot, element);
	if(!filepath) {
		SLAVES_DBG("Memory error");
		return 0;
	}

	cmd = asynch_new(cnx, IO_DELETE, MASTER_ASYNCH_TIMEOUT, filepath, strlen(filepath)+1, delete_query_callback, NULL);
	if(!cmd) {
		free(filepath);
		return 0;
	}

	SLAVES_DIALOG_DBG("%I64u: Delete query built for %s", cmd->uid, filepath);
	free(filepath);

	return 1;
}

/*
static unsigned int slave_rename_callback(struct slave_connection *cnx, struct slave_asynch_command *cmd, struct packet *p) {
	struct slave_rename_ctx *rename = cmd->param;
	struct vfs_element *source;

	if(!p) {
		SLAVES_DBG("Slave rename failed!");
		rename_destroy(rename);
		return 1;
	}

	if(p->type != IO_RENAMED) {
		SLAVES_DBG("Slave rename returned non-IO_RENAMED type!");
		rename_destroy(rename);
		return 1;
	}

	/ * 1. Create the new file * /

	/ * 2. Add the slave connection to the new file's availability * /

	/ * 3. Advertise the caller that the rename operation succeeded * /
	if(rename->callback) {
		(*rename->callback)(rename->cnx, rename->source, rename->dest, 1, rename->callback_param);
		rename->callback = NULL;
	}
	rename->callback_param = NULL;

	/ * 4. Destroy the rename context * /	
	source = rename->source;

	/ * 5. Remove the old file if it's no longer available from anywhere * /

	return 1;
}
*/
/*
	called by the asynch command callback OR by the
	vfs because the source is being deleted
*/
/*
int rename_destroy(struct slave_rename_ctx *rename) {

	if(rename->callback) {
		(*rename->callback)(rename->cnx, rename->source, rename->dest, 0, rename->callback_param);
		rename->callback = NULL;
	}
	rename->callback_param = NULL;

	if(rename->source) {
		rename->source->rename = NULL;
		rename->source = NULL;
	}

	if(rename->dest) {
		free(rename->dest);
		rename->dest = NULL;
	}

	free(rename);

	return 1;
}*/

/*
	The source must exist, the destination might not.
	The callback will be called when the response arrives.
	If the source file is deleted by the time the response arrives,
		the rename operation will fail and the callback will be called
		at this moment.
	If the slave disconnect before completion, the rename operation
		is beleived to have failed.
	A rename operation cannot be canceled in any other way than
		deleting the source file.
*/
/*
unsigned int slave_rename(
	struct slave_connection *cnx,
	struct vfs_element *source,
	char *dest,
	int (*callback)(struct slave_connection *cnx, struct vfs_element *source, char *dest, int success, void *param),
	void *param
) {
	struct slave_rename_ctx *rename;
	unsigned int length;
	char *sourcepath;
	char *data;

	if(!cnx || !source || !dest) {
		SLAVES_DBG("Params error");
		return 0;
	}

	if(source->rename) {
		SLAVES_DBG("FILE IS ALREADY BEING RENAMED!");
		return 0;
	}

	if(source->uploader) {
		SLAVES_DBG("FILE IS BEING UPLOADED!");
		return 0;
	}

	rename = malloc(sizeof(struct slave_rename_ctx));
	if(!rename) {
		SLAVES_DBG("Memory error");
		return 0;
	}

	rename->cnx = cnx;

	rename->callback = callback;
	rename->callback_param = param;

	rename->timestamp = time_now();
	rename->source = source;
	rename->dest = strdup(dest);
	if(!dest) {
		SLAVES_DBG("Memory error");
		free(rename->dest);
		return 0;
	}

	source->rename = rename;

	sourcepath = vfs_get_relative_path(cnx->slave->vroot, source);

	length = strlen(sourcepath) + 1 + strlen(dest) + 1;
	data = malloc(length);
	if(!data) {
		SLAVES_DBG("Memory error");
		free(sourcepath);
		rename_destroy(rename);
		return 0;
	}

	strcpy(&data[0], sourcepath);
	strcpy(&data[strlen(sourcepath)+1], dest);
	free(sourcepath);

	cmd = asynch_new(cnx, IO_SLAVE_RENAME, INFINITE, data, length, slave_rename_callback, rename);
	free(data);
	if(!cmd) {
		SLAVES_DBG("Memory error");
		rename_destroy(rename);
		return 0;
	}

	return 1;
}
*/


int make_sfvlog_query(struct slave_connection *cnx, struct collection *sfvfiles) {
	unsigned int length;
	char *buffer;


	return 1;
}

unsigned int file_list_query_cleanup_offline_files(struct collection *c, struct vfs_element *file, struct slave_ctx *slave) {

	collection_delete(slave->offline_files, file);
	collection_delete(file->offline_from, slave);

	/* if the file is no longer available from anywhere, delete it */
	if(!collection_size(file->offline_from) &&
			!collection_size(file->available_from) &&
			!collection_size(file->mirror_to)) {
		SLAVES_DBG("File %s is no longer available from %s", file->name, slave->name);
		vfs_recursive_delete(file);

		// TODO: delete the whole directory tree when it's empty
	}

	return 1;
}

/*
	after this last callback, if everything went well, the slave
	is fully merged and is ready to serve ftp clients
*/
/* p is NULL on timeout and on read error */
static unsigned int file_list_query_callback(struct slave_connection *cnx, struct slave_asynch_command *cmd, struct packet *p) {
	unsigned int length = 0;
	struct file_list_entry *entry;
	unsigned int total_files = 0, total_sfv = 0;
	unsigned long long int total_size = 0;
	struct vfs_element *element;
	//char *ptr;
	struct collection *sfv_files;

	unsigned long long int t;

	if(!p) {
		SLAVES_DBG("%I64u: No good packet received.", cmd->uid);
		return 0;
	}
	
	if(p->type != IO_FILE_LIST) {
		SLAVES_DBG("%I64u: Non-IO_FILE_LIST type received.", p->uid);
		return 0;
	}

	SLAVES_DIALOG_DBG("%I64u: File list response received", p->uid);

	length = (p->size - sizeof(struct packet));

	t = time_now();

	sfv_files = collection_new();

	entry = (struct file_list_entry *)p->data;
	while(length > sizeof(struct file_list_entry)) {
		unsigned int namelen;
		if(!entry->entry_size) {
			SLAVES_DBG("%I64u: ZERO entry size!", p->uid);
			collection_destroy(sfv_files);
			return 0;
		}
		if(entry->entry_size > length) {
			SLAVES_DBG("%I64u: Not enough room for another entry.", p->uid);
			collection_destroy(sfv_files);
			return 0;
		}
		length -= entry->entry_size;

		total_files++;
		total_size += entry->size;

		//element = vfs_find_element(cnx->slave->vroot, entry->name);
		//if(!element) {
			/* add the file to the vfs */
			element = vfs_create_file(cnx->slave->vroot, entry->name, "xFTPd");
			if(!element) {
				SLAVES_DBG("%I64u: Could not create the file in vfs: %s", p->uid, element->name);
				continue;
			}

			/* set the size of the element */
			vfs_set_size(element, entry->size);

			/* set the modification date of the element */
			if(element->timestamp != 0) {
				vfs_modify(element, (entry->timestamp - cnx->timediff));
			}
		//}

		slave_mark_online_from(cnx, element);

		/* if the file was .sfv then request its infos */
		namelen = strlen(element->name);
		if((namelen > 4) && !stricmp(&element->name[namelen-4], ".sfv")) {
			total_sfv++;
			collection_add_first(sfv_files, element);
			/*if(!make_sfv_query(cnx, element)) {
				SLAVES_DBG("%I64u: Could not make sfv file %s", p->uid, element->name);
			}*/
		}

		entry = (struct file_list_entry *)((char*)entry + entry->entry_size);
	}

	/* cleanup the slave's offline_files list */
	collection_iterate(cnx->slave->offline_files, (collection_f)file_list_query_cleanup_offline_files, cnx->slave);

	/* dump the files to the fileslog. */
	//slave_dump_fileslog(cnx->slave);
	SLAVES_DBG("%I64u: Slave sent %u files (%I64u bytes), we queried for %u sfv in %I64u ms.", p->uid,
		total_files, total_size, total_sfv, timer(t));

	/* add th slave to the ready connections */
	collection_delete(connecting_slaves, cnx);
	if(!collection_add_first(connected_slaves, cnx)) {
		SLAVES_DBG("Collection error");
		collection_destroy(sfv_files);
		return 0;
	}

	nuke_check_all();

	//if(!collection_size(sfv_files)) {
		/* this was the last stage of the connection process.
			now we will call the slave connection callback */
		if(!event_onSlaveIdentSuccess(cnx)) {
			SLAVES_DBG("%I64u: Slave connection rejected by onSlaveIdentSuccess", p->uid);
			collection_destroy(sfv_files);
			return 0;
		}

		/* from now on, this slave can send and receive files */
		cnx->ready = 1;
	/*} else {
		if(!make_sfvlog_query(cnx, sfv_files)) {
			SLAVES_DBG("Could NOT make SFV log query !");
			collection_destroy(sfv_files);
			return 0;
		}
	}*/

	collection_destroy(sfv_files);

	return 1;
}

/*
	we have to get the file list from the slave now.
	it will be sent as a list of files with path, name and size
	in a compact format
*/
unsigned int make_file_list_query(struct slave_connection *cnx) {
	struct slave_asynch_command *cmd;

	cmd = asynch_new(cnx, IO_FILE_LIST, MASTER_ASYNCH_TIMEOUT, NULL, 0, file_list_query_callback, NULL);
	if(!cmd) return 0;

	SLAVES_DIALOG_DBG("%I64u: File list query built", cmd->uid);

	return 1;
}

static unsigned int deletelog_query_callback(struct slave_connection *cnx, struct slave_asynch_command *cmd, struct packet *p) {
	FILE *f;

	if(!p) {
		SLAVES_DBG("%I64u: No good packet received.", cmd->uid);
		return 0;
	}
	
	if(p->type != IO_DELETED) {
		SLAVES_DBG("%I64u: Non-IO_DELETED type received.", p->uid);
		return 0;
	}
	
	SLAVES_DIALOG_DBG("%I64u: Deletelog response received", p->uid);

	/* clear the deletelog */
	f = fopen(cnx->slave->deletelog, "w");
	if(f) fclose(f);

	/* all delete queries are enqueued, now we have to ask the slave for its
		file list */
	if(!make_file_list_query(cnx)) {
		SLAVES_DBG("%I64u: Could not make file list query", p->uid);
		return 0;
	}

	return 1;
}

/*
	clean up the delete log and send the delete requests to the slave
*/
unsigned int make_deletelog_query(struct slave_connection *cnx) {
	struct slave_asynch_command *cmd;
	unsigned int filesize;
	char *buffer;

	buffer = config_load_file(cnx->slave->deletelog, &filesize);
	if(!buffer) {
		/* cannot open deletelog, sent file list query now */
		
		SLAVES_DBG("Could not open deletelog for %s, doing file list...", cnx->slave->name);

		if(!make_file_list_query(cnx)) {
			SLAVES_DBG("Could not make file list query");
			return 0;
		}

		return 1;
	}

	/* make only one delete query on the first pass. */
	cmd = asynch_new(cnx, IO_DELETELOG, MASTER_ASYNCH_TIMEOUT, buffer, filesize, deletelog_query_callback, NULL);
	free(buffer);
	if(!cmd) return 0;

	SLAVES_DIALOG_DBG("%I64u: Deletelog query built", cmd->uid);

	return 1;
}

/* p is NULL on timeout and on read error */
static unsigned int hello_query_callback(struct slave_connection *cnx, struct slave_asynch_command *cmd, struct packet *p) {
	struct slave_hello_data *hello;
	unsigned int length;
	struct slave_ctx *slave;

	if(!p) {
		SLAVES_DBG("%I64u: No good packet received.", cmd->uid);
		return 0;
	}
	
	if(p->type != IO_HELLO) {
		SLAVES_DBG("%I64u: Non-IO_HELLO type received.", p->uid);
		return 0;
	}

	SLAVES_DIALOG_DBG("%I64u: Hello response received", p->uid);

	length = (p->size - sizeof(struct packet));
	if(length < (sizeof(struct slave_hello_data))) {
		SLAVES_DBG("%I64u: Invalid hello data length", p->uid);
		return 0;
	}

	hello = (struct slave_hello_data *)&p->data;

	/* extract the name */
	if(!strlen(hello->name)) {
		SLAVES_DBG("%I64u: No name received", p->uid);
		return 0;
	}

	/* extract the diskfree */
	SLAVES_DBG("%I64u: Slave %s connected with %I64u/%I64u bytes free", p->uid, hello->name, hello->diskfree, hello->disktotal);

	cnx->timediff = (signed long long int)timer(hello->now);
	SLAVES_DBG("%I64u: Slave's now is %I64u, self now is %I64u", p->uid, hello->now, time_now());
	SLAVES_DBG("%I64u: Difference is %I64d", p->uid, cnx->timediff);
	
	//cnx->timediff = 0;

	cnx->diskfree = hello->diskfree;
	cnx->disktotal = hello->disktotal;

	slave = slave_get(hello->name);
	if(!slave) {
		SLAVES_DBG("%I64u: Slave %s is NOT known!", p->uid, hello->name);
		event_onSlaveIdentFail(cnx, hello);
		return 0;
	}

	if(slave->cnx) {
		SLAVES_DBG("%I64u: %s is already connected !", p->uid, hello->name);
		event_onSlaveIdentFail(cnx, hello);
		return 0;
	}
	
	/* link the connection with its slave */
	slave->cnx = cnx;
	cnx->slave = slave;

	/* empty the delete log */
	if(!make_deletelog_query(cnx)) {
		SLAVES_DBG("%I64u: Could not make deletelog query", p->uid);
		return 0;
	}

	return 1;
}

/*
	send a hello to the slave, we should get a reply with
	its name/diskfree/etc, wich is required to continue
*/
static unsigned int make_hello_query(struct slave_connection *cnx) {
	struct slave_asynch_command *cmd;
	struct master_hello_data data;

	/* technically, the slave could use encryption while the master does not and vice versa */
	data.use_encryption = slaves_use_encryption;
	data.use_compression = slaves_use_compression;
	data.compression_threshold = slaves_compression_threshold;

	cmd = asynch_new(cnx, IO_HELLO, MASTER_ASYNCH_TIMEOUT, (void*)&data, sizeof(struct master_hello_data), hello_query_callback, NULL);
	if(!cmd) return 0;

	SLAVES_DIALOG_DBG("%I64u: Hello query built", cmd->uid);
	
	/* FROM THIS POINT, all dialog is encrypted
		dialog can also (optionally) be compressed
		at the will of the master/slave */

	if(slaves_use_encryption) {
		cnx->io.flags |= IO_FLAGS_ENCRYPTED;
	}

	if(slaves_use_compression) {
		cnx->io.flags |= IO_FLAGS_COMPRESSED;
		cnx->io.compression_threshold = slaves_compression_threshold;
	}

	return 1;
}

/* p is NULL on timeout and on read error */
static unsigned int blowfish_key_query_callback(struct slave_connection *cnx, struct slave_asynch_command *cmd, struct packet *p) {
	unsigned int length;
	char *buffer;

	if(!p) {
		SLAVES_DBG("%I64u: No good packet received.", cmd->uid);
		return 0;
	}
		
	if(p->type != IO_BLOWFISH_KEY) {
		SLAVES_DBG("%I64u: Non-IO_BLOWFISH_KEY packet received.", p->uid);
		return 0;
	}

	SLAVES_DIALOG_DBG("%I64u: Blowfish key response reveived", p->uid);

	/* decrypt the client's blowfish key with our private key */
	length = (p->size - sizeof(struct packet));
	buffer = crypto_private_decrypt(&cnx->io.lkey, &p->data[0], length, &length);
	if(!buffer) {
		SLAVES_DBG("%I64u: Cannot decrypt slave's private key", p->uid);
		return 0;
	}

	/* set the client's blowfish key */
	if(!crypto_set_cipher_key(&cnx->io.rbf, buffer, length)) {
		SLAVES_DBG("%I64u: cannot set cypher key", p->uid);
		return 0;
	}
	memset(buffer, 0, length);
	free(buffer);

	/* we imported the remote blowfish key correctly,
		send a hello query */
	if(!make_hello_query(cnx)) {
		SLAVES_DBG("%I64u: Could not make hello query", p->uid);
		return 0;
	}

	return 1;
}

static unsigned int make_blowfish_key_query(struct slave_connection *cnx) {
	struct slave_asynch_command *cmd;
	unsigned int length, data_length;
	char *data, *buffer;

	/* set the local blowfish key, wich can't be longer than the
		maximum encryption size of the remote public key */
	length = crypto_max_public_encryption_length(&cnx->io.rkey);
	buffer = malloc(length);
	if(!buffer) {
		SLAVES_DBG("Memory error");
		return 0;
	}

	crypto_rand(buffer, length);

	if(!crypto_set_cipher_key(&cnx->io.lbf, buffer, length)) {
		SLAVES_DBG("Could not set cipher key");
		memset(buffer, 0, length);
		free(buffer);
		return 0;
	}

	/* publicly encrypt our blowfish key with the client's public key */
	data = crypto_public_encrypt(&cnx->io.rkey, buffer, length, &data_length);
	memset(buffer, 0, length);
	free(buffer);
	if(!data) return 0;

	cmd = asynch_new(cnx, IO_BLOWFISH_KEY, MASTER_ASYNCH_TIMEOUT, data, data_length, blowfish_key_query_callback, NULL);
	free(data);
	if(!cmd) return 0;

	SLAVES_DIALOG_DBG("%I64u: Blowfish key query built", cmd->uid);

	return 1;
}

/* p is NULL on timeout and on read error */
static unsigned int public_key_query_callback(struct slave_connection *cnx, struct slave_asynch_command *cmd, struct packet *p) {
	unsigned int length;

	if(!p) {
		SLAVES_DBG("%I64u: No good packet received.", cmd->uid);
		return 0;
	}
	
	if(p->type != IO_PUBLIC_KEY) {
		SLAVES_DBG("%I64u: Non-IO_PUBLIC_KEY packet received.", p->uid);
		return 0;
	}

	SLAVES_DIALOG_DBG("%I64u: Public key response received", p->uid);

	/* init the remote keypair */
	if(!crypto_initialize_keypair(&cnx->io.rkey, 0)) {
		SLAVES_DBG("%I64u: Could not initialize keypair.", p->uid);
		return 0;
	}

	/* import the remote keypair */
	length = (p->size - sizeof(struct packet));
	if(!crypto_import_keypair(&cnx->io.rkey, 0, &p->data[0], length)) {
		SLAVES_DBG("%I64u: Could NOT import slave's keypair", p->uid);
		return 0;
	}

	/* we've imported the remote key with no problem,
		generate a blowfish key and send it */
	if(!make_blowfish_key_query(cnx)) {
		SLAVES_DBG("%I64u: Could not make blowfish key query", p->uid);
		return 0;
	}

	return 1;
}

static unsigned int make_public_key_query(struct slave_connection *cnx) {
	struct slave_asynch_command *cmd;
	unsigned int length;
	char *data;

	if(!crypto_generate_keypair(&cnx->io.lkey, COMMUNICATION_KEY_LEN)) {
		SLAVES_DBG("Keypair generation failed");
		return 0;
	}

	/* send our public key for the communication */
	data = crypto_export_keypair(&cnx->io.lkey, 0, &length);
	if(!data) {
		SLAVES_DBG("Could not export keypair.");
		return 0;
	}

	cmd = asynch_new(cnx, IO_PUBLIC_KEY, MASTER_ASYNCH_TIMEOUT, data, length, public_key_query_callback, NULL);
	free(data);
	if(!cmd) return 0;

	SLAVES_DIALOG_DBG("%I64u: Public key query built", cmd->uid);

	return 1;
}

static unsigned int count_size_of_files(struct collection *c, void *item, void *param) {
	struct {
		unsigned long long int size;
	} *ctx = param;
	struct vfs_element *file = item;

	ctx->size += file->size;

	return 1;
}

unsigned long long int slave_files_size(struct slave_connection *cnx) {
	struct {
		unsigned long long int size;
	} ctx = { 0 };

	if(!cnx) return 0;

	collection_iterate(cnx->available_files, count_size_of_files, &ctx);

	return ctx.size;
}

static unsigned int send_asynch_query_callback(struct collection *c, void *item, void *param) {
	struct slave_asynch_command *cmd = item;
	struct {
		unsigned int success;
		struct slave_connection *cnx;
	} *ctx = param;

	/* send the command's data */
	if(!io_write_data(&ctx->cnx->io, cmd->uid, cmd->command, cmd->data, cmd->data_length)) {

		SLAVES_DBG("%I64u: Could not write packet data", cmd->uid);

		asynch_destroy(cmd, NULL);

		/* we will be disconnected from the slave */
		ctx->success = 0;
		return 0;
	}

	/* send succeeded: move the item to the asynch_response list */
	collection_delete(ctx->cnx->asynch_queries, cmd);
	if(!collection_add(ctx->cnx->asynch_response, cmd)) {
		SLAVES_DBG("Collection error");

		ctx->success = 0;

		asynch_destroy(cmd, NULL);

		return 0;
	}

	/* update the send time */
	cmd->send_time = time_now();
	ctx->cnx->asynchtime = time_now();

	ctx->success = 1;

	/* send one by one */
	return 0;
}

static unsigned int send_asynch_query(struct slave_connection *cnx) {
	struct {
		unsigned int success;
		struct slave_connection *cnx;
	} ctx = { 0, cnx };

	collection_iterate(cnx->asynch_queries, send_asynch_query_callback, &ctx);

	return ctx.success;
}

static unsigned int read_asynch_response(struct slave_connection *cnx) {
	struct packet *p = NULL;
	unsigned int success;

	/* read a packet */
	if(!io_read_packet(&cnx->io, &p, INFINITE)) {
		SLAVES_DBG("Could not read a packet: stopping");
		return 0;
	}
	if(!p) {
		//SLAVES_DBG("Could not read a packet: will resume");
		return 1;
	}

	success = asynch_match(cnx, p);
	if(!success) {
		SLAVES_DBG("Slave will be disconnected because of the data received.");
	}

	/* destroy the packet */
	free(p);

	return success;
}

static unsigned int slaves_dump_fileslog_callback(struct collection *c, struct slave_ctx *slave, void *param) {
	unsigned long long int *time = param;

	if(timer(slave->fileslog_timestamp) > FTPD_FILESLOG_TIME) {
	
		slave_dump_fileslog(slave);
		slave->fileslog_timestamp = time_now();

		if(timer(*time) > FTPD_FILESLOG_THRESHOLD) {
			SLAVES_DBG("Reached fileslog threshold on %s after %I64u!", slave->name, timer(*time));

			return 1;
		}
	}

	return 0;
}

void slaves_dump_fileslog() {
	struct slave_ctx *slave;
	unsigned long long int time;

	time = time_now();

	slave = collection_match(slaves, (collection_f)slaves_dump_fileslog_callback, &time);
	if(slave) {
		//SLAVES_DBG("Collection size: %u", collection_size(slaves));
		/* move all slaves that were dumped at the end of the collection */
		do {
			struct slave_ctx *first = collection_first(slaves);
			collection_movelast(slaves, first);
			//SLAVES_DBG("Moved %s to end", first->name);
			if(first == slave) {
				break;
			}
		} while(1);
		//SLAVES_DBG("Collection size: %u", collection_size(slaves));
	}

	/*time = timer(time);
	
	if(time) {
		SLAVES_DBG("Dumped all fileslog in %I64u ms", time);
	}*/

	return;
}

int slaves_check_asynch_timeouts(struct collection *c, struct slave_asynch_command *cmd, struct slave_connection *cnx) {

	if(cmd->timeout == INFINITE) {
		return 1;
	}

	if(timer(cmd->send_time) > cmd->timeout) {
		SLAVES_DBG("%I64u: Asynch timeout reached! (%u ms)", cmd->uid, cmd->timeout);

		asynch_destroy(cmd, NULL);

		return 1;
	}

	return 1;
}

int slaves_check_xfer_timeouts(struct collection *c, struct ftpd_client_ctx *client, struct slave_connection *cnx) {

	/*if(!collection_find(clients, client)) {
		SLAVES_DBG("ERROR: client at %08x IS INVALID!!!", (int)client);

		collection_delete(c, client);
		return 1;
	}*/

	if(timer(client->xfer.last_alive) > FTPD_XFER_TIMEOUT) {
		SLAVES_DBG("%I64u: Client xfer timeout reached!", client->xfer.uid);

		/*if(client->xfer.uid == -1) {
			SLAVES_DBG("WARNING!!!! Client's xfer uid is -1!!!!");
		}*/

		ftpd_client_cleanup_data_connection(client);

		//collection_delete(c, client);
		return 1;
	}

	return 1;
}

int slaves_check_mirror_timeouts(struct collection *c, struct mirror_ctx *mirror, struct slave_connection *cnx) {
	struct mirror_side *side;

	side = (mirror->source.cnx == cnx) ? &mirror->source : &mirror->target;

	if(timer(side->last_alive) > FTPD_XFER_TIMEOUT) {
		SLAVES_DBG("%I64u: Mirror xfer timeout reached!", mirror->uid);

		mirror_cancel(mirror);

		return 1;
	}

	return 1;
}

/* Check and enforces all timeouts for the slave */
static int slave_check_timeouts(struct slave_connection *cnx) {

	/* 1. Check timeouts on awaited asynch responses */
	collection_iterate(cnx->asynch_response, (collection_f)slaves_check_asynch_timeouts, cnx);

	/* 2. Check timeouts on xfers */
	collection_iterate(cnx->xfers, (collection_f)slaves_check_xfer_timeouts, cnx);

	/* 3. Check timeouts on mirrors */
	collection_iterate(cnx->mirror_to, (collection_f)slaves_check_mirror_timeouts, cnx);
	collection_iterate(cnx->mirror_from, (collection_f)slaves_check_mirror_timeouts, cnx);

	return 1;
}

int slave_connection_read(int fd, struct slave_connection *cnx) {

	/* start by dumping the fileslog to disk */
	/*if(cnx->slave) {
		slaves_dump_fileslog();
	}*/
	
	obj_ref(&cnx->o);
	
	/* read an asynch response */
	if(!read_asynch_response(cnx)) {

		SLAVES_DBG("Could not read incoming packet from slave");

		/* failed to properly handle the response */

		/* disconnect from slave */
		slave_connection_destroy(cnx);
	
		obj_unref(&cnx->o);

		return 0;
	}

	slave_check_timeouts(cnx);
	
	obj_unref(&cnx->o);

	return 1;
}

int slave_connection_read_timeout(struct slave_connection *cnx) {
	
	SLAVES_DBG("Read timeout with %s", socket_formated_peer_address(cnx->io.fd));

	/* disconnect from slave */
	slave_connection_destroy(cnx);

	return 0;
}

int slave_connection_write_timeout(struct slave_connection *cnx) {
	
	SLAVES_DBG("Write timeout with %s", socket_formated_peer_address(cnx->io.fd));

	/* disconnect from slave */
	slave_connection_destroy(cnx);

	return 0;
}

int slave_connection_write(int fd, struct slave_connection *cnx) {

	/* start by dumping the fileslog to disk */
	/*if(cnx->slave) {
		slaves_dump_fileslog();
	}*/

	if(!collection_size(cnx->asynch_queries)) {
		/* No data to be sent, it's all good */
		return 1;
	}
	
	obj_ref(&cnx->o);

	/* send an anych query */
	if(!send_asynch_query(cnx)) {

		SLAVES_DBG("Could not send top query to slave");

		/* failed to send the query- destroy the slave connection */

		/* disconnect from slave */
		slave_connection_destroy(cnx);
		
		obj_unref(&cnx->o);

		return 0;
	}
	
	obj_unref(&cnx->o);

	return 1;
}

void slave_connection_destroy(struct slave_connection *cnx) {
	
	/* void all collections */
	collection_void(cnx->mirror_from);
	collection_void(cnx->mirror_to);
	collection_void(cnx->xfers);
	collection_void(cnx->available_files);
	collection_void(cnx->asynch_queries);
	collection_void(cnx->asynch_response);

	obj_destroy(&cnx->o);

	return;
}

static int slave_connection_obj_destroy_mirror(struct collection *c, struct mirror_ctx *mirror, void *param) {

	mirror_cancel(mirror);

	return 1;
}

static int slave_connection_obj_destroy_asynch(struct collection *c, struct slave_asynch_command *cmd, void *param) {

	asynch_destroy(cmd, NULL);

	return 1;
}

static int slave_connection_obj_destroy_xfer(struct collection *c, struct ftpd_client_ctx *client, void *param) {

	ftpd_client_cleanup_data_connection(client);

	return 1;
}

void slave_connection_obj_destroy(struct slave_connection *cnx) {
	unsigned long long int time;

	time = time_now();

	/* call the event */
	if(cnx->slave) {
		event_onSlaveDisconnect(cnx);
	}

	/* try to remove from both lists cuz we don't know in wich one it is */
	if(collection_find(connecting_slaves, cnx)) {
		collection_delete(connecting_slaves, cnx);
	}

	if(collection_find(connected_slaves, cnx)) {
		collection_delete(connected_slaves, cnx);
	}
	
	/* free the signal group */
	signal_clear(cnx->group);

	/* destroy the socket */
	if(cnx->io.fd != -1) {
		close_socket(cnx->io.fd);
		socket_monitor_fd_closed(cnx->io.fd);
		cnx->io.fd = -1;
	}

	/* cancel all mirrors operation */
	collection_iterate(cnx->mirror_from, (collection_f)slave_connection_obj_destroy_mirror, NULL);
	collection_iterate(cnx->mirror_to, (collection_f)slave_connection_obj_destroy_mirror, NULL);

	/* free the asynch commands collections */
	collection_iterate(cnx->asynch_queries, (collection_f)slave_connection_obj_destroy_asynch, NULL);
	collection_iterate(cnx->asynch_response, (collection_f)slave_connection_obj_destroy_asynch, NULL);

	/*
		xfers should be deleted in the process of deleting
		the asynch command collections, but still clean
		it up
	*/
	collection_iterate(cnx->xfers, (collection_f)slave_connection_obj_destroy_xfer, NULL);

	/* release all files currently owned by the slave */
	if(cnx->slave) {
		//struct collection *temp = NULL;

		/* Dirty trick to save a lot of time -- not working? */
		/*temp = cnx->available_files;
		cnx->available_files = cnx->slave->offline_files;
		cnx->slave->offline_files = temp;*/

		while(collection_size(cnx->available_files)) {
			void *first = collection_first(cnx->available_files);
			if(cnx->slave) {
				slave_mark_offline_from(cnx->slave, first);
			} else {
				collection_delete(cnx->available_files, first);
			}
		}
	}

	collection_destroy(cnx->group);
	cnx->group = NULL;

	collection_destroy(cnx->mirror_from);
	cnx->mirror_from = NULL;

	collection_destroy(cnx->mirror_to);
	cnx->mirror_to = NULL;

	collection_destroy(cnx->asynch_queries);
	cnx->asynch_queries = NULL;

	collection_destroy(cnx->asynch_response);
	cnx->asynch_response = NULL;

	collection_destroy(cnx->xfers);
	cnx->xfers = NULL;

	collection_destroy(cnx->available_files);
	cnx->available_files = NULL;

	if(cnx->slave) {

		/* change the last-online field */
		config_write_int(cnx->slave->config, "last-online", time_now());
		cnx->slave->lastonline = time_now();
		slave_dump(cnx->slave);

		cnx->slave->cnx = NULL;
		cnx->slave = NULL;
	}

	/* free the keys */
	crypto_destroy_keypair(&cnx->io.lkey);
	crypto_destroy_keypair(&cnx->io.rkey);

	free(cnx);
	
	SLAVES_DBG("Disconnected in %I64u ms!", timer(time));

	return;
}

int slave_connection_error(int fd, struct slave_connection *cnx) {

	SLAVES_DBG("Slave connection error");

	/*  */
	slave_connection_destroy(cnx);

	return 1;
}

int slave_connection_close(int fd, struct slave_connection *cnx) {

	SLAVES_DBG("Slave connection closed");

	/*  */
	slave_connection_destroy(cnx);

	return 1;
}

/* add a connection to the not-ready slaves list */
struct slave_connection *slave_connection_new(int fd) {
	struct slave_connection *cnx;

	cnx = malloc(sizeof(struct slave_connection));
	if(!cnx) {
		SLAVES_DBG("Memory error");
		return NULL;
	}

	//cnx->destroyed = 0;
	obj_init(&cnx->o, cnx, (obj_f)slave_connection_obj_destroy);

	cnx->ready = 0;
	cnx->slave = NULL;

	cnx->group = collection_new();

	cnx->last_command = IO_NOTHING;
	memset(&cnx->io, 0, sizeof(cnx->io));
	cnx->io.fd = fd;
	cnx->io.flags = 0;

	cnx->timestamp = time_now();
	cnx->statstime = 0;
	cnx->asynchtime = 0;

	cnx->diskfree = 0;
	cnx->disktotal = 0;

	cnx->lagtime = 0;

	cnx->asynch_queries = collection_new();
	cnx->asynch_response = collection_new();
	cnx->available_files = collection_new();

	/* xfers list */
	cnx->xfers = collection_new();

	/* mirrors */
	cnx->mirror_from = collection_new();
	cnx->mirror_to = collection_new();

	collection_add(connecting_slaves, cnx);

	if(!event_onSlaveConnect(cnx)) {
		
		SLAVES_DBG("New slave rejected by onSlaveConnect");
		collection_delete(connecting_slaves, cnx);
		collection_destroy(cnx->group);
		collection_destroy(cnx->mirror_to);
		collection_destroy(cnx->mirror_from);
		collection_destroy(cnx->xfers);
		collection_destroy(cnx->available_files);
		collection_destroy(cnx->asynch_response);
		collection_destroy(cnx->asynch_queries);
		free(cnx);
		return NULL;
	}

	/* register socket events */
	socket_monitor_new(fd, 1, 1);
	socket_monitor_signal_add(fd, cnx->group, "socket-error", (signal_f)slave_connection_error, cnx);
	socket_monitor_signal_add(fd, cnx->group, "socket-close", (signal_f)slave_connection_close, cnx);
	
	{
		struct signal_callback *s;
			
		s = socket_monitor_signal_add(fd, cnx->group, "socket-read", (signal_f)slave_connection_read, cnx);
		signal_timeout(s, SLAVE_MASTER_TIMEOUT, (timeout_f)slave_connection_read_timeout, cnx);
		
		s = socket_monitor_signal_add(fd, cnx->group, "socket-write", (signal_f)slave_connection_write, cnx);
		signal_timeout(s, SLAVE_MASTER_TIMEOUT, (timeout_f)slave_connection_write_timeout, cnx);
	}

	return cnx;
}

int slaves_connect(int fd, void *param) {
	struct slave_connection *cnx;
	int new_fd;

	new_fd = accept(fd, NULL, 0);
	if(new_fd == -1) {

		SLAVES_DBG("Incoming connection could not be accepted");

		/*
			if this socket cannot be accepted, we will
			try to continue for other clients
		*/

		return 0;
	}
	socket_current++;
	
	/* enable linger so we'll perform hard abort on closesocket() */
	socket_linger(new_fd, 0);

	/* set the correct socket settings */
	socket_set_max_read(new_fd, SLAVE_MASTER_SOCKET_SIZE);
	socket_set_max_write(new_fd, SLAVE_MASTER_SOCKET_SIZE);

	/* hook events on the new slave connection */
	cnx = slave_connection_new(new_fd);
	if(!cnx) {
		SLAVES_DBG("Connection could not be accepted");
		close_socket(new_fd);
		return 0;
	}

	if(slaves_use_encryption) {
		if(!make_public_key_query(cnx)) {
			SLAVES_DBG("Could not make public key query");
			slave_connection_destroy(cnx);
			return 0;
		}
	} else {
		if(!make_hello_query(cnx)) {
			SLAVES_DBG("Could not make hello key query");
			slave_connection_destroy(cnx);
			return 0;
		}
	}

	return 1;
}

int slaves_error(int fd, void *param) {

	SLAVES_DBG("Slaves socket error");

	main_fatal();

	return 1;
}

int slaves_close(int fd, void *param) {

	SLAVES_DBG("Slaves socket closed");

	main_fatal();

	return 1;
}

int slaves_init() {

	SLAVES_DBG("Loading ...");

	slaves = collection_new();

	connecting_slaves = collection_new();
	connected_slaves = collection_new();

	slaves_port = 0;
	slaves_group = collection_new();
	slaves_fd = -1;

	if(!vfs_root)
		vfs_root = vfs_create_root();

	if(!slaves_load_config()) {
		return 0;
	}

	/* create our listening socket */
	slaves_fd = create_listening_socket(slaves_port);
	if(slaves_fd == -1) {
		SLAVES_DBG("Failed to listen on port %u", slaves_port);
		return 0;
	}

	SLAVES_DBG("Listening on port %u for new slaves", slaves_port);

	socket_monitor_new(slaves_fd, 0, 1);
	socket_monitor_signal_add(slaves_fd, slaves_group, "socket-connect", (signal_f)slaves_connect, NULL);
	socket_monitor_signal_add(slaves_fd, slaves_group, "socket-error", (signal_f)slaves_error, NULL);
	socket_monitor_signal_add(slaves_fd, slaves_group, "socket-close", (signal_f)slaves_close, NULL);

	return 1;
}

void slaves_free() {

	SLAVES_DBG("Unloading ...");

	/* TODO! */

	return;
}
