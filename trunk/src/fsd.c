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
#include <poll.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <direct.h>

#include "collection.h"
#include "socket.h"
#include "io.h"
#include "logging.h"
#include "config.h"
#include "fsd.h"
#include "ftpd.h"
#include "time.h"
#include "sfv.h"
#include "stats.h"
#include "crc32.h"
#include "service.h"
#include "slaves.h"
#include "signal.h"
#include "proxy.h"

#include "constants.h"

/* name of this slave */
static char *slave_name = NULL;

static unsigned int master_ip = 0;
static unsigned short master_port = 0;
static unsigned int master_connections = 0;

struct collection *enqueued_packets = NULL;

struct collection *mapped_disks = NULL; /* contain disk_map structs */
static unsigned int next_uid = 0;
struct disk_map *current_disk; /* the disk currently used to store files */

static unsigned short current_local_port = 0;
static unsigned short fsd_low_data_port = 40000;
static unsigned short fsd_high_data_port = 50000;

static unsigned int fsd_buffer_up =  1 * 1024 * 1024;
static unsigned int fsd_buffer_down =  1 * 1024 * 1024;
static char *working_buffer = NULL;

static struct collection *xfers_collection = NULL;

static unsigned long long int current_proxy_uid = 0;

/* use proxy to connect to master */
static unsigned int use_proxy = 0;
static unsigned int proxy_ip = 0;
static unsigned short proxy_port = 0;

/* use proxy to connect for active data connection */
static unsigned int use_data_proxy = 0;
static unsigned int data_proxy_ip = 0;
static unsigned short data_proxy_port = 0;

/* use proxy to listen for passive data connection */
static unsigned int use_pasv_proxy = 0;
static unsigned int pasv_proxy_ip = 0;
static unsigned short pasv_proxy_port = 0;


/* return 0 if the current_disk is not valid */
static unsigned int rotate_disks() {

	if(!current_disk) {
		next_uid = 0;
		current_disk = collection_next(mapped_disks, &next_uid);
		if(!current_disk) {
			SLAVE_DBG("NO disk loaded!");
			return 0;
		}

		current_disk->current_files = 0;
		return 1;
	}

	if(current_disk->current_files >= current_disk->threshold) {
		current_disk = collection_next(mapped_disks, &next_uid);
		if(!current_disk) {
			next_uid = 0;
			current_disk = collection_next(mapped_disks, &next_uid);
			if(!current_disk) {
				SLAVE_DBG("NO disk loaded!");
				return 0;
			}
		}

		current_disk->current_files = 0;
		return 1;
	}
	
	return 1;
}

static unsigned short get_next_local_port() {
	unsigned short port;

	if((current_local_port < fsd_low_data_port) || (current_local_port > fsd_high_data_port))
		current_local_port = fsd_low_data_port;

	port = current_local_port;

	current_local_port++;

	return port;
}

int _stat64(
   const char *path,
   struct __stat64 *buffer 
);

/* add a file map to the specified disk
	the name is like \dir\file.bin relative to disk->path */
static struct file_map *file_map_add(struct disk_map *disk, const char *name) {
	struct __stat64 stats;
	char *full_name;
	struct file_map *file;

	file = malloc(sizeof(struct file_map) + strlen(name) + 1);
	if(!file) {
		SLAVE_DBG("Memory error");
		return NULL;
	}
	file->disk = disk;
	strcpy(file->name, name);

	file->xfers = collection_new();

	/* verify that we can access the file both way */
	full_name = malloc(strlen(disk->path) + strlen(name) + 1);
	if(!full_name) {
		SLAVE_DBG("Memory error");
		collection_destroy(file->xfers);
		free(file);
		return NULL;
	}
	sprintf(full_name, "%s%s", disk->path, &name[1]);

	if(_stat64(full_name, &stats) == -1) {
		SLAVE_DBG("Error getting stats on %s", full_name);
		collection_destroy(file->xfers);
		free(file);
		free(full_name);
		return NULL;
	}
	free(full_name);

	file->size = stats.st_size;
	file->timestamp = (stats.st_mtime * 1000); /* timestamp need milliseconds resolution */

	collection_add(disk->files_collection, file);

	file->io.refcount = 0;
	file->io.upload = 0;
	file->io.stream = -1;

	return file;
}

/* used by lookup_file */
static unsigned int lookup_file_on_disk_callback(struct collection *c, void *item, void *param) {
	struct file_map *file = item;
	struct {
		char *name;
		struct file_map *file;
	} *ctx = param;

	if(!stricmp(file->name, ctx->name)) {
		ctx->file = file;
		return 0;
	}

	return 1;
}

/* used by lookup_file */
static unsigned int lookup_file_callback(struct collection *c, void *item, void *param) {
	struct disk_map *disk = item;
	struct {
		char *name;
		struct file_map *file;
	} *ctx = param;

	collection_iterate(disk->files_collection, lookup_file_on_disk_callback, param);

	return (ctx->file ? 0 : 1);
}

/* return a pointer to the file specified by 'name' */
static struct file_map *lookup_file(char *name) {
	struct {
		char *name;
		struct file_map *file;
	} ctx = { name, NULL };

	/* iterate disks */
	collection_iterate(mapped_disks, lookup_file_callback, &ctx);

	return ctx.file;
}

void _rmdir_recursive(char *path, char *toremove) {
	char *fullpath, *ptr;

	fullpath = malloc(strlen(path) + strlen(toremove) + 1);
	if(!fullpath) {
		SLAVE_DBG("Memory error");
		return;
	}
	sprintf(fullpath, "%s%s", path, toremove);
	_rmdir(fullpath);
	free(fullpath);

	ptr = strchr(toremove, '\\');
	if(ptr) {
		while(strchr(ptr+1, '\\')) ptr = strchr(ptr+1, '\\');
		*ptr = 0;
		ptr++;
		if(strlen(toremove)) {
			_rmdir_recursive(path, toremove);
		}
	}

	return;
}

static void delete_xfer(struct slave_xfer *xfer);

/* delete the file from disk,
	close all data connections if some are open,
	delete the parent folder if there's one & if
	there's no more file & folder in it */
static void file_unmap(struct file_map *file) {
	char *fullname, *ptr;

	if(!file) return;

	fullname = malloc(strlen(file->disk->path) + strlen(file->name) + 1);
	if(fullname) {
		sprintf(fullname, "%s%s", file->disk->path, &file->name[1]);
	}

	/* delete all xfers from that file */
	if(file->xfers) {
		collection_void(file->xfers);
		while(collection_size(file->xfers)) {
			struct slave_xfer *xfer = collection_first(file->xfers);
			delete_xfer(xfer);
			collection_delete(file->xfers, xfer);
		}
		collection_destroy(file->xfers);
		file->xfers = NULL;
	}

	/* unlink the file from its owner disk */
	collection_delete(file->disk->files_collection, file);

	/* delete the file on disk */
	if(fullname) {
		remove(fullname);
		free(fullname);
	}
	
	fullname = &file->name[1];
	ptr = strchr(fullname, '\\');
	if(ptr) {
		while(strchr(ptr+1, '\\')) ptr = strchr(ptr+1, '\\');
		*ptr = 0;

		if(strlen(fullname))
			_rmdir_recursive(file->disk->path, fullname);
	}

	file->disk = NULL;
	free(file);

	return;
}

/* used by get_xfer_from_uid */
static unsigned int get_xfer_from_uid_callback(struct collection *c, struct slave_xfer *xfer, void *param) {
	struct {
		unsigned long long int uid;
	} *ctx = param;

	return (xfer->uid == ctx->uid);
}

/* return a slave_xfer pointer from the uid of the xfer. */
static struct slave_xfer *get_xfer_from_uid(unsigned long long int uid) {
	struct slave_xfer *xfer;
	struct {
		unsigned long long int uid;
	} ctx = { uid };

	xfer = collection_match(xfers_collection, (collection_f)get_xfer_from_uid_callback, &ctx);

	return xfer;
}

/* enqueue a packet to be sent to the master */
static unsigned int enqueue_packet(unsigned long long int uid, unsigned int type, void *data, unsigned int data_length) {
	struct packet *p;

	if(!data && data_length) {
		SLAVE_DBG("data_length != 0 and data == NULL");
		return 0;
	}

	p = malloc(sizeof(struct packet) + data_length);
	if(!p) {
		SLAVE_DBG("Memory error");
		return 0;
	}

	p->size = sizeof(struct packet) + data_length;
	p->uid = uid;
	p->type = type;
	if(data) {
		memcpy(&p->data[0], data, data_length);
	}

	if(!collection_add(enqueued_packets, p)) {
		SLAVE_DBG("Collection error");
		free(p);
		return 0;
	}

	return 1;
}

/* send a general failure reply */
static unsigned int reply_failure(struct io_context *io, struct packet *p) {

	if(!enqueue_packet(p->uid, IO_FAILURE, NULL, 0)) {
		return 0;
	}

	SLAVE_DIALOG_DBG("%I64u: Failure reply built", p->uid);

	return 1;
}

/* used by stat_disk_files */
static unsigned int stat_file(struct collection *c, struct file_map *file, void *param) {
	struct {
		unsigned int file_count;
		unsigned int names_size;
	} *ctx = param;

	ctx->file_count++;
	ctx->names_size += strlen(file->name)+1;

	return 1;
}

/* used by build_file_list */
static unsigned int stat_disk_files(struct collection *c, struct disk_map *disk, void *param) {

	collection_iterate(disk->files_collection, (collection_f)stat_file, param);

	return 1;
}

/* used by build_disk_file_list */
static unsigned int build_file_list(struct collection *c, struct file_map *file, void *param) {
	struct {
		char *buffer;
		unsigned int offset;
	} *ctx = param;
	struct file_list_entry *entry = (struct file_list_entry *)(&ctx->buffer[ctx->offset]);
	unsigned int entry_size = (sizeof(struct file_list_entry) + strlen(file->name) + 1);
	unsigned int i;

	ctx->offset += entry_size;
	
	entry->entry_size = entry_size;
	entry->size = file->size;
	entry->timestamp = file->timestamp;
	strcpy(entry->name, file->name);
	for(i=0;i<strlen(entry->name);i++) if(entry->name[i] == '\\') entry->name[i] = '/';

	return 1;
}

/* used by process_file_list */
static unsigned int build_disk_file_list(struct collection *c, struct disk_map *disk, void *param) {

	collection_iterate(disk->files_collection, (collection_f)build_file_list, param);

	return 1;
}

/* build a big buffer with all the files we've got
	and send it to the master */
static unsigned int process_file_list(struct io_context *io, struct packet *p) {
	struct {
		unsigned int file_count;
		unsigned int names_size;
	} ctx = { 0, 0 };
	
	SLAVE_DIALOG_DBG("%I64u: File list query received", p->uid);

	collection_iterate(mapped_disks, (collection_f)stat_disk_files, &ctx);

	if(!ctx.file_count) {
		if(!enqueue_packet(p->uid, IO_FILE_LIST, NULL, 0))
			return 0;
		
		SLAVE_DIALOG_DBG("%I64u: File list response built (NO file)", p->uid);
	} else {
		struct {
			char *buffer;
			unsigned int offset;
		} ctx2 = { NULL, 0 };

		ctx2.buffer = malloc((ctx.file_count * sizeof(struct file_list_entry)) + ctx.names_size);
		if(!ctx2.buffer) {
			SLAVE_DBG("Memory error");
			if(!reply_failure(io, p))
				return 0;
			return 1;
		}

		collection_iterate(mapped_disks, (collection_f)build_disk_file_list, &ctx2);
		
		if(!enqueue_packet(p->uid, IO_FILE_LIST, ctx2.buffer, ctx2.offset)) {
			free(ctx2.buffer);
			return 0;
		}
		free(ctx2.buffer);
		
		SLAVE_DIALOG_DBG("%I64u: File list response built (%u bytes)", p->uid, ctx2.offset);
	}

	return 1;
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

static char *build_sfv_info(struct file_map *file, unsigned int *size) {
	unsigned int filled = 0;
	char line[1024];
	char *buffer = NULL;
	char *path, *_crc;
	unsigned int i;
	FILE *s;
	unsigned int crc;
	struct sfv_entry *entry;
	unsigned long long int t;

	t = time_now();

	*size = 0;

	path = malloc(strlen(file->disk->path)+strlen(file->name)+1);
	if(!path) {
		SLAVE_DBG("Memory error");
		return NULL;
	}
	sprintf(path, "%s%s", file->disk->path, &file->name[1]);

	s = fopen(path, "r");
	if(!s) {
		SLAVE_DBG("File %s could not be opened", path);
		free(path);
		return NULL;
	}
	free(path);

	while(!feof(s)) {
		if(!fgets(line, sizeof(line)-1, s)) break;

		/* got a line, now parse if */
		if(line[0] == ';') continue;
	
		for(i=0;i<strlen(line);i++)
			if((line[i] == '\r') || (line[i] == '\n')) line[i] = 0;

		_crc = strchr(line, ' ');
		if(!_crc) continue;
		while(strchr(_crc+1, ' ')) _crc = strchr(_crc+1, ' ');

		*_crc = 0;
		_crc++;

		if(strlen(_crc) != 8) continue;

		/* translate the password in hex */
		for(i=0;i<strlen(_crc);i++) {
			if(!valid_hex(_crc[i])) continue;
		}

		crc = 0;

		for(i=0;i<strlen(_crc);i++) {
			crc |= (char_to_hex(_crc[i]) << (32-(4 * ((i % 8)+1))));
		}

		/* we've got the crc & the filename, happend it to the buffer */
		if(!buffer) {
			buffer = malloc(sizeof(struct sfv_entry) + strlen(line) + 1);
		} else {
			buffer = realloc(buffer, filled + (sizeof(struct sfv_entry) + strlen(line) + 1));
		}

		if(!buffer) {
			SLAVE_DBG("Memory error");
			break; /* memory error */
		}

		entry = (struct sfv_entry*)&buffer[filled];

		entry->crc = crc;
		sprintf(entry->filename, line);

		/* move to next entry */
		filled += (sizeof(struct sfv_entry) + strlen(line) + 1);
	}

	fclose(s);

	*size = filled;

	return buffer;
}

/* open the specified sfv file and send the extracted
	files informations */
static unsigned int process_slave_sfv(struct io_context *io, struct packet *p) {
	char *path = &p->data[0];
	struct file_map *file;
	char *buffer;
	unsigned int size, i;

	SLAVE_DIALOG_DBG("%I64u: SFV query received", p->uid);

	for(i=0;i<strlen(path);i++)
		if(path[i] == '/') path[i] = '\\';

	file = lookup_file(path);
	if(!file) {
		SLAVE_DBG("Could not find local file: %s", path);
		if(!reply_failure(io, p))
			return 0;
		return 1;
	}

	buffer = build_sfv_info(file, &size);
	if(!buffer) {
		SLAVE_DBG("Could not build sfv infos for %s", path);
		if(!reply_failure(io, p))
			return 0;
		return 1;
	}
	
	if(!enqueue_packet(p->uid, IO_SFV, buffer, size)) {
		free(buffer);
		return 0;
	}
	free(buffer);

	SLAVE_DIALOG_DBG("%I64u: SFV response build (%u bytes)", p->uid, size);

	return 1;
}

/* used by process_hello */
static unsigned int make_stats_diskspace(struct collection *c, struct disk_map *disk, struct stats_global *gstats) {
	struct _diskfree_t driveinfo;
	unsigned int drive = 0;

	if((disk->path[0] >= 'a') && (disk->path[0] <= 'z')) drive = disk->path[0] - 'a'+1;
	if((disk->path[0] >= 'A') && (disk->path[0] <= 'Z')) drive = disk->path[0] - 'A'+1;
	if(!drive) return 1;

	if(_getdiskfree(drive, &driveinfo)) {
		SLAVE_DBG("Cannot get disk infos for drive: %c", disk->path[0]);
		return 1;
	}
	
	/* update the local diskfree and disktotal all along */
	disk->diskfree = (unsigned long long int)(
						(unsigned long long int)driveinfo.avail_clusters *
						(unsigned long long int)driveinfo.sectors_per_cluster *
						(unsigned long long int)driveinfo.bytes_per_sector);
	disk->disktotal = (unsigned long long int)(
						(unsigned long long int)driveinfo.total_clusters *
						(unsigned long long int)driveinfo.sectors_per_cluster *
						(unsigned long long int)driveinfo.bytes_per_sector);

	gstats->diskfree += disk->diskfree;
	gstats->disktotal += disk->disktotal;

	return 1;
}

static unsigned int make_stats_xfers(struct collection *c, struct slave_xfer *xfer, void *param) {
	struct {
		unsigned int i;
		struct stats_xfer *xstats;
	} *ctx = param;

	ctx->xstats[ctx->i].uid = xfer->uid;
	ctx->xstats[ctx->i].xfered = xfer->xfered;

	ctx->i++;

	return 1;
}

static unsigned int process_slave_stats(struct io_context *io, struct packet *p) {
	unsigned int size;
	char *buffer;
	struct stats_global *gstats;
	struct {
		unsigned int i;
		struct stats_xfer *xstats;
	} ctx = { 0, NULL };

	size = sizeof(struct stats_global);
	size += (collection_size(xfers_collection) * sizeof(struct stats_xfer));

	buffer = malloc(size);
	if(!buffer) {
		SLAVE_DBG("Memory error");
		if(!reply_failure(io, p))
			return 0;
		return 1;
	}

	gstats = (struct stats_global *)&buffer[0];

	gstats->diskfree = 0;
	gstats->disktotal = 0;
	collection_iterate(mapped_disks, (collection_f)make_stats_diskspace, gstats);

	ctx.xstats = (struct stats_xfer *)&buffer[sizeof(struct stats_global)];
	collection_iterate(xfers_collection, (collection_f)make_stats_xfers, &ctx);
	
	if(!enqueue_packet(p->uid, IO_STATS, buffer, size)) {
		free(buffer);
		return 0;
	}
	free(buffer);

	return 1;
}

/* used by process_hello */
static unsigned int make_hello_diskspace(struct collection *c, struct disk_map *disk, struct slave_hello_data *data) {

	data->diskfree += disk->diskfree;
	data->disktotal += disk->disktotal;

	return 1;
}

/* send our name & current disk usage */
static unsigned int process_hello(struct io_context *io, struct packet *p) {
	struct slave_hello_data *data;
	unsigned int data_length;
	struct master_hello_data *hello = (struct master_hello_data *)&p->data[0];

	SLAVE_DIALOG_DBG("%I64u: Hello query received", p->uid);

	if(hello->use_encryption) {
		io->flags |= IO_FLAGS_ENCRYPTED;
	}

	if(hello->use_compression) {
		io->flags |= IO_FLAGS_COMPRESSED;
		io->compression_threshold = hello->compression_threshold;
	}

	data_length = (sizeof(struct slave_hello_data) + strlen(slave_name) + 1);
	data = malloc(data_length);
	if(!data) {
		SLAVE_DBG("Memory error");
		return 0;
	}

	data->diskfree = 0;
	data->disktotal = 0;
	collection_iterate(mapped_disks, (collection_f)make_hello_diskspace, data);

	strcpy(data->name, slave_name);

	data->now = time_now();

	if(!enqueue_packet(p->uid, IO_HELLO, data, data_length)) {
		free(data);
		return 0;
	}
	free(data);

	SLAVE_DIALOG_DBG("%I64u: Hello response built", p->uid);

	return 1;
}

/* generate a blowfish key for secure communication and
	send it to the master */
static unsigned int process_blowfish_key(struct io_context *io, struct packet *p) {
	unsigned int length, data_length;
	char *data, *buffer;

	SLAVE_DIALOG_DBG("%I64u: Blowfish query received", p->uid);

	/* decrypt the client's blowfish key with our private key */
	length = (p->size - sizeof(struct packet));
	buffer = crypto_private_decrypt(&io->lkey, &p->data[0], length, &length);
	if(!buffer) {
		SLAVE_DBG("%I64u: Could not decrypt master's blowfish key", p->uid);
		return 0;
	}

	/* set the client's blowfish key */
	if(!crypto_set_cipher_key(&io->rbf, buffer, length)) {
		SLAVE_DBG("%I64u: Could not set cipher key", p->uid);
		return 0;
	}
	memset(buffer, 0, length);
	free(buffer);

	/* set the local blowfish key, wich can't be longer than the
		maximum encryption size of the remote public key */
	length = crypto_max_public_encryption_length(&io->rkey);
	buffer = malloc(length);
	if(!buffer) {
		SLAVE_DBG("%I64u: Memory error", p->uid);
		return 0;
	}

	crypto_rand(buffer, length);

	if(!crypto_set_cipher_key(&io->lbf, buffer, length)) {
		SLAVE_DBG("%I64u: Could not set cipher key", p->uid);
		memset(buffer, 0, length);
		free(buffer);
		return 0;
	}

	/* publicly encrypt our blowfish key with the client's public key */
	data = crypto_public_encrypt(&io->rkey, buffer, length, &data_length);
	memset(buffer, 0, length);
	free(buffer);
	if(!data) {
		SLAVE_DBG("%I64u: Could not encrypt own blowfish key", p->uid);
		return 0;
	}

	if(!enqueue_packet(p->uid, IO_BLOWFISH_KEY, data, data_length)) {
		free(data);
		return 0;
	}
	free(data);

	SLAVE_DIALOG_DBG("%I64u: Blowfish response built", p->uid);

	return 1;
}

/* send the public part of our encryption key key */
static unsigned int process_public_key(struct io_context *io, struct packet *p) {
	unsigned int length;
	char *data;

	SLAVE_DIALOG_DBG("%I64u: Public key query received", p->uid);

	/* init the remote keypair */
	if(!crypto_initialize_keypair(&io->rkey, 0)) {
		SLAVE_DBG("%I64u: Could not initialize keypair", p->uid);
		return 0;
	}

	/* import the remote keypair */
	length = (p->size - sizeof(struct packet));
	if(!crypto_import_keypair(&io->rkey, 0, &p->data[0], length)) {
		SLAVE_DBG("%I64u: Could not import master's keypair", p->uid);
		return 0;
	}

	/* we've imported the remote key with no problem,
		generate our public key and send it */
	if(!crypto_generate_keypair(&io->lkey, COMMUNICATION_KEY_LEN)) {
		SLAVE_DBG("%I64u: Could not generate own keypair", p->uid);
		return 0;
	}

	/* send our public key for the communication */
	data = crypto_export_keypair(&io->lkey, 0, &length);
	if(!data) {
		SLAVE_DBG("%I64u: Could not export own keypair", p->uid);
		return 0;
	}

	if(!enqueue_packet(p->uid, IO_PUBLIC_KEY, data, length)) {
		free(data);
		return 0;
	}
	free(data);

	SLAVE_DIALOG_DBG("%I64u: Public key repsponse sent", p->uid);

	return 1;
}

/* destroy a slave_xfer structure, close the file if its refcount
	reach zero */
static void xfer_destroy(struct slave_xfer *xfer) {

	if(xfer->group) {
		signal_clear(xfer->group);
		collection_destroy(xfer->group);
		xfer->group = NULL;
	}

	if(xfer->fd != -1) {
		socket_monitor_fd_closed(xfer->fd);
		close_socket(xfer->fd);
		xfer->fd = -1;
	}

	if(xfer->query) {
		free(xfer->query);
		xfer->query = NULL;
	}

	if(xfer->reply) {
		free(xfer->reply);
		xfer->reply = NULL;
	}

	if(xfer->file) {
		if(xfer->file->io.refcount) {
			xfer->file->io.refcount--;

			if(!xfer->file->io.refcount) {
				/* no more downloads on that file, close it */

				SLAVE_DBG("File %s is no longer in use", xfer->file->name);

				if(xfer->file->io.stream != -1) {
					_close(xfer->file->io.stream);
					xfer->file->io.stream = -1;
				}
			}
		}

		collection_delete(xfer->file->xfers, xfer);
		xfer->file = NULL;
	}

	collection_delete(xfers_collection, xfer);

	free(xfer);

	return;
}

/* xfer finished the wrong way */
static void delete_xfer(struct slave_xfer *xfer) {
	struct file_map *file;
	unsigned int upload;
	unsigned long long int asynch_uid;
	//char *filename;

	upload = xfer->upload;
	asynch_uid = xfer->asynch_uid;
	file = xfer->file;

	xfer_destroy(xfer);

	if(upload && file) {
		/* client was uploding, we shall remove the file */
		file_unmap(file);
	}

	/* send FAILURE to master */
	if(asynch_uid != -1) {
		enqueue_packet(asynch_uid, IO_FAILURE, NULL, 0);
	}

	return;
}

/* xfer finished the right way */
/* send some infos about the transfer to the client */
static void complete_xfer(struct slave_xfer *xfer) {
	/* send the amount of data transfered with the reply */
	struct slave_transfer_reply data;

	/* total size transfered */
	if(xfer->file) {
		data.xfersize = xfer->xfered;
		data.filesize = xfer->file->size;
	} else {
		SLAVE_DBG("%I64u: CANNOT COMPLETE XFER BECAUSE NO FILE POINTER", xfer->uid);
		data.xfersize = 0;
		data.filesize = 0;
	}

	/* checksum for the transfer */
	crc32_close(&xfer->checksum);
	data.checksum = xfer->checksum;

	SLAVE_DBG("%I64u: Transfer complete: %I64u bytes transfered, checksum is %08x (time: %I64u)",
			xfer->uid, xfer->xfered, xfer->checksum, time_now());

	/* send TRASNFERED to the master */
	enqueue_packet(xfer->asynch_uid, IO_SLAVE_TRANSFERED, &data, sizeof(data));
	
	xfer_destroy(xfer);

	return;
}

int xfer_read(int fd, struct slave_xfer *xfer) {
	unsigned int ret;
	int size, avail;
	unsigned long long int timediff;

	if(!xfer->proxy_connected) {

		/* read the 'reply' packet */
		ret = packet_read(fd, &xfer->reply, &xfer->filledsize);
		if(!ret) {
			if(!xfer->reply) {
				/* should NEVER happen */
				delete_xfer(xfer);
				return 0;
			} else {
				/* packet could not be entierly read */
				return 1;
			}
		}

		if(ret && !xfer->reply) {
			/* there was not enough data yet */
			return 1;
		}

		/* parse the packet */
		if(xfer->passive && (xfer->reply->type == PROXY_LISTENING)) {
			/*
				we are operating in passive mode here,
				the proxy must send us a "listening" packet
				that we must transferback to the master.
			*/
			struct proxy_listening *listening = (struct proxy_listening *)&xfer->reply->data;			
			struct slave_listen_reply data;

			xfer->ip = listening->ip;

			data.ip = xfer->ip;
			data.port = xfer->port;

			/* tell the master we're listening right now */
			if(!enqueue_packet(xfer->listen_uid, IO_SLAVE_LISTENING, &data, sizeof(data))) {
				xfer_destroy(xfer);
				return 0;
			}
		}

		else if(!xfer->passive && (xfer->reply->type == PROXY_CONNECTED)) {
			/*
				we are operating in active mode here,
				the proxy must send us a "connected" packet
			*/

			// nothing special to do here.

		}
		else {
			/* the proxy sent us garbage */

			SLAVE_DBG("Proxy sent unrecognized data");
			delete_xfer(xfer);
			return 0;
		}

		free(xfer->reply);
		xfer->reply = NULL;

		xfer->proxy_connected = 1;
		
		return 1;
	}
	
	/* if we're supposed to receive data here, we do it */
	if(!xfer->ready || !xfer->connected) {
		//SLAVE_DBG("%I64u: File transfer is not yet ready (download)", xfer->uid);
		return 1;
	}

	if(!xfer->upload) {
		// Not uploading, nothing to do here: remove the callback
		signal_clear_with_filter(xfer->group, "socket-read", (void *)fd);
		return 1;
	}
/*
	timediff = timer(xfer->speedcheck);
	if(timediff > SPEEDCHECK_TIME) {
		unsigned int speed;
		unsigned int newsize;
		int increase;

		speed = (xfer->speedsize / (timediff / 1000));

		newsize = speed + ((speed * 10) / 100) + 1;
		if(newsize && (newsize > SPEEDCHECK_MINIMAL)) {

			increase = ((newsize - xfer->lastsize) * 100) / newsize;
			if(increase < 0) {
				increase = -increase;
			}

			if(increase > SPEEDCHECK_THRESHOLD) {
				SLAVE_DBG("(read) Speed for the last %u ms was %u bytes per second", SPEEDCHECK_TIME, speed);
				SLAVE_DBG("(read) Setting new socket size to %u, last was: %u (change of %d%%)", newsize, xfer->lastsize, increase);
				
				xfer->speedsize = 0;
				xfer->speedcheck = time_now();
				xfer->lastsize = newsize;
				
				socket_set_max_read(fd, newsize);
			}
		}
	}
*/
	do {
		/* get the size available from the buffer */
		avail = socket_avail(fd);
		if(avail > xfer->buffersize) {
			avail = xfer->buffersize;
		}

		/* read the most data we can from the socket */
		size = recv(fd, working_buffer, avail, 0);
		if(size == 0) {
			/* socket closed, connection complete */
			SLAVE_DBG("%I64u: Socket closed. transfer complete?", xfer->uid);
			complete_xfer(xfer);
			return 1;
		}
		if(size < 0) {
			/* socket error */
			SLAVE_DBG("%I64u: recv() error (connection reset?). size: %u. avail: %u", xfer->uid, size, avail);
			delete_xfer(xfer);
			return 1;
		}

		crc32_add(&xfer->checksum, working_buffer, size);

		_lseeki64(xfer->file->io.stream, xfer->restart, SEEK_SET);

		/* write to file */
		if(_write(xfer->file->io.stream, working_buffer, size) != size) {
			/* write error */
			SLAVE_DBG("%I64u: _write() error on %s%s at %I64u", xfer->uid, xfer->file->disk->path,
				&xfer->file->name[1], _telli64(xfer->file->io.stream));
			delete_xfer(xfer);
			return 1;
		}

		_commit(xfer->file->io.stream);

		xfer->xfered += size;
		xfer->file->size += size;
		xfer->restart += size;
		xfer->speedsize += size;

		/* try again 'til the buffer's empty */
	} while(socket_avail(fd));

	//SLAVE_DBG("%I64u: %u bytes received.", xfer->uid, size);

	return 1;
}

int xfer_write(int fd, struct slave_xfer *xfer) {
	unsigned int rem_size, size;
	int read_size, write_size;
	unsigned long long int timediff;

	if(!xfer->proxy_connected) {

		/* write the 'query' packet */
		if(xfer->query) {

			if(send(fd, (void*)xfer->query, xfer->query->size, 0) != xfer->query->size) {
				SLAVE_DBG("%I64u: send() error while sending query to proxy.", xfer->uid);
				delete_xfer(xfer);
				return 0;
			}

			free(xfer->query);
			xfer->query = NULL;
		}

		return 1;
	}
	
	/* if we're supposed to write some data here, so do it */
	if(!xfer->ready || !xfer->connected) {
		//SLAVE_DBG("%I64u: File transfer is not yet ready (upload)", xfer->uid);
		return 1;
	}
	
	if(xfer->upload) {
		// client's uploading, nothing to do here: remove the callback
		signal_clear_with_filter(xfer->group, "socket-write", (void *)fd);
		return 1;
	}
	
	if(xfer->restart > xfer->file->size) {
		SLAVE_DBG("%I64u: CANNOT RESTART AFTER EOF", xfer->uid);
		delete_xfer(xfer);
		return 0;
	}
	
	if(xfer->restart == xfer->file->size) {
		/* xfer completed successfuly, now we tell the master */
		if(!xfer->completed) {
			SLAVE_DBG("%I64u: Transfer completed successfully. Sutting down socket. (time: %I64u)", xfer->uid, time_now());
			
			make_socket_blocking(fd, 1);
			shutdown(fd, SD_SEND);

			xfer->completed = 1;
		}
		return 1;
	}

	/*
	timediff = timer(xfer->speedcheck);
	if(timediff > SPEEDCHECK_TIME) {
		unsigned int speed;
		unsigned int newsize;
		int increase;

		speed = (xfer->speedsize / (timediff / 1000));

		newsize = speed + ((speed * 10) / 100) + 1;
		if(newsize && (newsize > SPEEDCHECK_MINIMAL)) {

			increase = ((newsize - xfer->lastsize) * 100) / newsize;
			if(increase < 0) {
				increase = -increase;
			}

			if(increase > SPEEDCHECK_THRESHOLD) {
				SLAVE_DBG("(write) Speed for the last %u ms was %u bytes per second", SPEEDCHECK_TIME, speed);
				SLAVE_DBG("(write) Setting new socket size to %u, last was: %u (change of %d%%)", newsize, xfer->lastsize, increase);
				
				xfer->speedsize = 0;
				xfer->speedcheck = time_now();
				xfer->lastsize = newsize;
				
				socket_set_max_write(fd, newsize);
			}
		}
	}*/

	/* size remaining to be uploaded */
	rem_size = (unsigned int)(xfer->file->size - xfer->restart);

	size = xfer->buffersize;
	if(size > rem_size)
		size = rem_size;

	/* set the correct position in file */
	_lseeki64(xfer->file->io.stream, xfer->restart, SEEK_SET);

	/* read from file */
	read_size = _read(xfer->file->io.stream, working_buffer, size);
	if(read_size <= 0) {
		/* read error */
		SLAVE_DBG("%I64u: _read error on %s%s at %I64u for %u", xfer->uid, xfer->file->disk->path,
			xfer->file->name, _telli64(xfer->file->io.stream), size);
		delete_xfer(xfer);
		return 1;
	}
	
	/* write to client */
	write_size = send(fd, working_buffer, read_size, 0);
	if(write_size <= 0) {
		/* send error */
		SLAVE_DBG("%I64u: WSAGetLastError: %u", xfer->uid, WSAGetLastError());
		SLAVE_DBG("%I64u: send() error: %u wanted, %u done (connection reset?).", xfer->uid, read_size, write_size);
		delete_xfer(xfer);
		return 1;
	}

	crc32_add(&xfer->checksum, working_buffer, write_size);

	xfer->xfered += write_size;
	xfer->restart += write_size;
	xfer->speedsize += write_size;

	return 1;
}

int xfer_close(int fd, struct slave_xfer *xfer) {

	SLAVE_DBG("%I64u: Graceful closure of the socket.", xfer->uid);

	/* connection closed gracefully */
	complete_xfer(xfer);

	return 1;
}

int xfer_error(int fd, struct slave_xfer *xfer) {

	SLAVE_DBG("%I64u: Socket caused an error.", xfer->uid);

	/* connection closed unexpectedly */
	delete_xfer(xfer);

	return 1;
}

int xfer_read_timeout(struct slave_xfer *xfer) {
	
	if(!xfer->upload) {
		// client's downloading (so we are writing)
		// so it's normal to have read timeouts
		return 1;
	}

	SLAVE_DBG("%I64u: Read timeout with %I64u bytes read.", xfer->uid, xfer->xfered);

	/*if(xfer->xfered) {
		complete_xfer(xfer);
	} else*/ {
		delete_xfer(xfer);
	}
	
	return 1;
}

int xfer_write_timeout(struct slave_xfer *xfer) {
	
	if(xfer->upload) {
		// client's uploading (so we are reading)
		// so it's normal to have write timeouts
		return 1;
	}

	SLAVE_DBG("%I64u: Write timeout with %I64u bytes written.", xfer->uid, xfer->xfered);
	delete_xfer(xfer);
	
	return 1;
}

int xfer_connect_timeout(struct slave_xfer *xfer) {
	
	SLAVE_DBG("%I64u: Connect timeout.", xfer->uid);
	delete_xfer(xfer);
	
	return 1;
}


int xfer_setup_socket(struct slave_xfer *xfer) {
	int err;

	/* set the correct settings on the socket here because we know if we're uploading or not */
	if(xfer->upload) {
		xfer->lastsize = fsd_buffer_down;
		SLAVE_DBG("%I64u: Setting max read size to %u", xfer->uid, fsd_buffer_down);
		err = socket_set_max_read(xfer->fd, fsd_buffer_down);
		if(err) {
			SLAVE_DBG("%I64u: Error was: %u", xfer->uid, err);
		}
	} else {
		xfer->lastsize = fsd_buffer_up;
		SLAVE_DBG("%I64u: Setting max write size to %u", xfer->uid, fsd_buffer_up);
		err = socket_set_max_write(xfer->fd, fsd_buffer_up);
		if(err) {
			SLAVE_DBG("%I64u: Error was: %u", xfer->uid, err);
		}
	}
	
	xfer->speedsize = 0;
	xfer->speedcheck = time_now();

	SLAVE_DBG("%I64u: Socket setup complete.", xfer->uid);

	return 1;
}

/*
	For active connection:
		1. The transfer query arrives
		2. xfer_connect() is called

	For passive connection:
		First case:
			1. The listen query arrive
			2. xfer_connect() is called
			3. The transfer query arrives
		Second case:
			1. The listen query arrives
			2. The transfer query arrives
			3. xfer_connect() is called

		In both cases, we set the socket settings at 3.
*/
int xfer_connect(int fd, struct slave_xfer *xfer) {

	if(xfer->passive && !use_pasv_proxy) {
		
		/* accept() and swap sockets ... */
		SLAVE_DBG("%I64u: Accepting passive connection", xfer->uid);
		
		xfer->fd = accept(fd, NULL, 0);
		
		signal_clear(xfer->group);

		socket_monitor_fd_closed(fd);
		close_socket(fd);

		if(xfer->fd == -1) {
			SLAVE_DBG("%I64u: Could not accept new connection.", xfer->uid);
			delete_xfer(xfer);
			return 0;
		}

		/* enable linger so we'll perform hard abort on closesocket() */
		socket_linger(xfer->fd, 0);
		
		/* hook up the new socket */
		socket_monitor_new(xfer->fd, 1, 1);
		socket_monitor_signal_add(xfer->fd, xfer->group, "socket-close", (signal_f)xfer_close, xfer);
		socket_monitor_signal_add(xfer->fd, xfer->group, "socket-error", (signal_f)xfer_error, xfer);

	} else {
		SLAVE_DBG("%I64u: Active connection established", xfer->uid);
		
		/* remove this very signal */
		signal_clear_with_filter(xfer->group, "socket-connect", (void *)fd);
	}
	
	/* hook the "read" and "write" signals on the socket */
	{
		struct signal_callback *s;
		
		s = socket_monitor_signal_add(xfer->fd, xfer->group, "socket-read", (signal_f)xfer_read, xfer);
		signal_timeout(s, DATA_CONNECTION_TIMEOUT, (timeout_f)xfer_read_timeout, xfer);
		
		s = socket_monitor_signal_add(xfer->fd, xfer->group, "socket-write", (signal_f)xfer_write, xfer);
		signal_timeout(s, DATA_CONNECTION_TIMEOUT, (timeout_f)xfer_write_timeout, xfer);
	}

	xfer->connected = 1;

	if(xfer->ready) {
		/*
			if ready is not set here, we do not yet
			have enough informations to setup the sockets
		*/
		xfer_setup_socket(xfer);
	}

	return 1;
}

/* called on PASV connections. we must start listening
	for a client but we don't yet know which file we
	will transfer on the connection */
static unsigned int process_slave_listen(struct io_context *io, struct packet *p) {
	struct slave_xfer *xfer;
	struct slave_listen_request *req = (struct slave_listen_request *)&p->data;
	unsigned int length = (p->size - sizeof(struct packet));

	SLAVE_DIALOG_DBG("%I64u: Listen query received", p->uid);

	if(length < sizeof(struct slave_listen_request)) {
		SLAVE_DBG("%I64u: Protocol error", p->uid);
		return 0; /* protocol error */
	}

	/* allocate the xfer structure right away
		and store important data so when the
		master will tell us wich file we have
		to send we will continue with this structure */
	xfer = malloc(sizeof(struct slave_xfer));
	if(!xfer) {
		SLAVE_DBG("%I64u: Memory error", p->uid);
		return 0;
	}

	xfer->group = collection_new();
	xfer->fd = -1;
	xfer->restart = 0;
	xfer->asynch_uid = -1;
	xfer->file = NULL;
	xfer->ready = 0;
	xfer->uid = req->xfer_uid;
	xfer->passive = 1;
	xfer->timestamp = time_now();
	xfer->xfered = 0;
	xfer->query = 0;
	xfer->reply = 0;
	xfer->upload = 0;
	xfer->connected = 0;
	xfer->completed = 0;

	xfer->port = get_next_local_port();

	/* queue the xfer into the xfers_collection list */
	if(!collection_add(xfers_collection, xfer)) {
		SLAVE_DBG("Collection error");
		xfer_destroy(xfer);
		return 0;
	}

	if(use_pasv_proxy) {
		xfer->fd = connect_to_ip_non_blocking(pasv_proxy_ip, pasv_proxy_port);
		xfer->proxy_connected = 0;
		xfer->filledsize = 0;
	} else {
		xfer->fd = create_listening_socket(xfer->port);
		xfer->proxy_connected = 1;
	}

	if(xfer->fd == -1) {
		SLAVE_DBG("%I64u: Could not get a listening socket", p->uid);
		/* don't disconnect from the master here,
			just answer that we can't handle this client */
		xfer_destroy(xfer);
		if(!enqueue_packet(p->uid, IO_FAILURE, NULL, 0)) return 0;
		return 1;
	}

	if(!use_pasv_proxy) {
		struct slave_listen_reply data;

		xfer->ip = socket_local_address(io->fd);
		if(!xfer->ip || (xfer->ip == -1)) {
			SLAVE_DBG("%I64u: Could not get socket ip.", p->uid);
			xfer_destroy(xfer);
			if(!enqueue_packet(p->uid, IO_FAILURE, NULL, 0)) return 0;
			return 1;
		}

		data.ip = xfer->ip;
		data.port = xfer->port;

		/* tell the master we're listening right now */
		if(!enqueue_packet(p->uid, IO_SLAVE_LISTENING, &data, sizeof(data))) {
			xfer_destroy(xfer);
			return 0;
		}
		
		SLAVE_DIALOG_DBG("%I64u: Listening response built", p->uid);
	}
	
	/* register events on the proxy socket: "error", "close", "connect" */
	if(use_pasv_proxy) {
		socket_monitor_new(xfer->fd, 0, 0);
	} else {
		socket_monitor_new(xfer->fd, 0, 1);
	}
	
	{
		struct signal_callback *s;
		
		s = socket_monitor_signal_add(xfer->fd, xfer->group, "socket-connect", (signal_f)xfer_connect, xfer);
		signal_timeout(s, DATA_CONNECTION_TIMEOUT, (timeout_f)xfer_connect_timeout, xfer);
	}
	socket_monitor_signal_add(xfer->fd, xfer->group, "socket-close", (signal_f)xfer_close, xfer);
	socket_monitor_signal_add(xfer->fd, xfer->group, "socket-error", (signal_f)xfer_error, xfer);
	
	if(use_pasv_proxy) {
		struct proxy_listen listen;
		xfer->listen_uid = p->uid;

		listen.port = xfer->port;

		/* enqueue the "listen" packet to the intention of the proxy */
		xfer->query = packet_new(current_proxy_uid++, PROXY_LISTEN, &listen, sizeof(listen));
		if(!xfer->query) {
			SLAVE_DBG("%I64u: Could not create proxy's query", p->uid);
			xfer_destroy(xfer);
			if(!enqueue_packet(p->uid, IO_FAILURE, NULL, 0)) return 0;
			return 1;
		}
	}

	return 1;
}

/* setup the xfer->file->io struct for the specified xfer */
static unsigned int setup_file_io(struct slave_xfer *xfer) {
	char *filename;

	/* check if the file is already open. */
	if(xfer->file->io.refcount) {

		if(xfer->file->io.upload != xfer->upload) {
			/* client requesting to download a file that is
				currently being uploaded */
			SLAVE_DBG("%I64u: Client request for different operation than current", xfer->uid);
			return 0;
		}

		xfer->file->io.refcount++;
		return 1;
	}

	/* find the complete filename */
	filename = malloc(strlen(xfer->file->disk->path) + strlen(xfer->file->name) + 1);
	if(!filename) {
		SLAVE_DBG("%I64u: Memory error", xfer->uid);
		return 0;
	}
	sprintf(filename, "%s%s", xfer->file->disk->path, &xfer->file->name[1]);

	/* open the file */
	if(xfer->upload) {
		/* try to remove the file, just in case it was already there.
			overwrite permissions must be handled on master side */
		//_unlink(filename);
		xfer->file->io.stream = _open(filename, _O_WRONLY | _O_BINARY /*| _O_SEQUENTIAL*/, _S_IREAD | _S_IWRITE);
	} else {
		xfer->file->io.stream = _open(filename, _O_RDONLY | _O_BINARY /*| _O_SEQUENTIAL*/);
	}

	if(xfer->file->io.stream == -1) {
		SLAVE_DBG("%I64u: File %s could not be opened", xfer->uid, filename);

		free(filename);
		return 0;
	}
	free(filename);

	/* file open & ready ! */
	xfer->file->io.upload = xfer->upload;
	xfer->file->io.refcount++;

	return 1;
}

/* return -1 on error, 0 on success */
int _creat_recursive(char *filename) {
	char *tempdir, *ptr;
	unsigned int i;

	if(!strchr(filename, '\\')) return -1;

	tempdir = strdup(filename);
	if(!tempdir) return -1;

	ptr = strchr(tempdir, '\\')+1;
	while(1) {
		ptr = strchr(ptr, '\\');
		if(!ptr) break;

		*ptr = 0;
		_mkdir(tempdir);
		*ptr = '\\';
		ptr++;
	}
	free(tempdir);

	i = _creat(filename, _S_IREAD | _S_IWRITE);
	_close(i);

	return (i != -1) ? 0 : -1;
}

/* called when the master want us to start transfering data.
we sould resume with our previously created xfer struct
for passive transfers, or create a new one for active
transfers. we will also make sure the file really exists
for download requests or we will create the file for
upload requests. */
static unsigned int process_slave_transfer(struct io_context *io, struct packet *p) {
	struct slave_transfer_request *req = (struct slave_transfer_request *)&p->data;
	unsigned int length = (p->size - sizeof(struct packet));
	struct slave_xfer *xfer;
	struct file_map *file;
	unsigned int i;
	//unsigned long argp;
	char *fullname;

	SLAVE_DIALOG_DBG("%I64u: Transfer query received", p->uid);

	if(length < sizeof(struct slave_transfer_request)) {
		SLAVE_DBG("%I64u: Protocol error", p->uid);
		return 0; /* protocol error */
	}

	/* lookup for a matching transfer in the xfers_collection list */
	xfer = get_xfer_from_uid(req->xfer_uid);

	if(req->passive) {
		if(!xfer) {
			SLAVE_DBG("%I64u: Could not match xfer %I64u locally", p->uid, req->xfer_uid);
			if(!enqueue_packet(p->uid, IO_FAILURE, NULL, 0)) return 0;
			return 1;
		}
	} else {
		if(xfer) {
			SLAVE_DBG("%I64u: Matched with local xfer %I64u while not supposed to.", p->uid, xfer->uid);
			xfer_destroy(xfer);
			if(!enqueue_packet(p->uid, IO_FAILURE, NULL, 0)) return 0;
			return 1;
		}

		/* create a new slave_xfer struct and fill some information */
		xfer = malloc(sizeof(struct slave_xfer));
		if(!xfer) {
			SLAVE_DBG("%I64u: Memory error", p->uid);
			if(!enqueue_packet(p->uid, IO_FAILURE, NULL, 0)) return 0;
			return 1;
		}

		/* these infos will already be set for passive connections
			so we must set them now on active connections */
		xfer->uid = req->xfer_uid;

		xfer->passive = 0;

		xfer->completed = 0;
		xfer->connected = 0;
		xfer->ip = req->ip;
		xfer->port = req->port;
		xfer->group = collection_new();
		xfer->query = NULL;
		xfer->reply = NULL;

		if(use_data_proxy) {
			xfer->fd = connect_to_ip_non_blocking(data_proxy_ip, data_proxy_port);
			xfer->proxy_connected = 0;
			xfer->filledsize = 0;
		}
		else {
			xfer->fd = connect_to_ip_non_blocking(xfer->ip, xfer->port);
			xfer->proxy_connected = 1;
		}

		if(xfer->fd == -1) {
			/* socket failure */
			SLAVE_DBG("%I64u: Could not create socket", p->uid);
			xfer_destroy(xfer);
			if(!enqueue_packet(p->uid, IO_FAILURE, NULL, 0)) return 0;
			return 1;
		}

		if(!collection_add(xfers_collection, xfer)) {
			SLAVE_DBG("%I64u: Collection error", p->uid);
			xfer_destroy(xfer);
			if(!enqueue_packet(p->uid, IO_FAILURE, NULL, 0)) return 0;
			return 1;
		}
		
		/* connect all events to the xfer socket: "error", "close", "connect" */
		socket_monitor_new(xfer->fd, 0, 0);
		{
			struct signal_callback *s;
			
			s = socket_monitor_signal_add(xfer->fd, xfer->group, "socket-connect", (signal_f)xfer_connect, xfer);
			signal_timeout(s, DATA_CONNECTION_TIMEOUT, (timeout_f)xfer_connect_timeout, xfer);
		}
		
		socket_monitor_signal_add(xfer->fd, xfer->group, "socket-close", (signal_f)xfer_close, xfer);
		socket_monitor_signal_add(xfer->fd, xfer->group, "socket-error", (signal_f)xfer_error, xfer);

		if(use_data_proxy) {
			struct proxy_connect connect;

			connect.ip = xfer->ip;
			connect.port = xfer->port;

			/* add the "connect" packet for the proxy */
			xfer->query = packet_new(current_proxy_uid++, PROXY_CONNECT, &connect, sizeof(connect));
			if(!xfer->query) {
				SLAVE_DBG("%I64u: Could not create packet for proxy", p->uid);
				xfer_destroy(xfer);
				if(!enqueue_packet(p->uid, IO_FAILURE, NULL, 0)) return 0;
				return 1;
			}
		}
	}

	/* keep some infos */
	xfer->upload = req->upload; /* 'upload' is from the USER's point of view- upload for him is download for us. */
	xfer->restart = req->upload ? 0 : req->restart; /* enforce "no resume on upload" policy */
	SLAVE_DBG("%I64u: Restarting at %I64u", p->uid, req->restart);
	xfer->asynch_uid = p->uid;
	xfer->timestamp = time_now();
	xfer->xfered = 0;
	xfer->file = NULL;
	xfer->buffersize = req->upload ? fsd_buffer_up : fsd_buffer_down;

	xfer->ready = 1;

	if(xfer->connected) {
		/*
			if connected is not set here, we do not yet
			have enough informations to setup the sockets
		*/
		xfer_setup_socket(xfer);
	}

	crc32_init(&xfer->checksum);

	for(i=0;i<strlen(req->filename);i++) if(req->filename[i] == '/') req->filename[i] = '\\';

	if(!xfer->upload) {
		/* lookup the file in all our mapped disks */
		file = lookup_file(req->filename);
		if(!file) {
			SLAVE_DBG("%I64u: Could not find requested file: %s", p->uid, req->filename);
			xfer_destroy(xfer);
			if(!enqueue_packet(p->uid, IO_FAILURE, NULL, 0)) return 0;
			return 1;
		}
	} else {
		/* rotate the disks so we don't get to upload to the same disk until it's full */
		if(!rotate_disks() || !current_disk) {
			SLAVE_DBG("%I64u: Could not find next upload disk", p->uid);
			xfer_destroy(xfer);
			if(!enqueue_packet(p->uid, IO_FAILURE, NULL, 0)) return 0;
			return 1;
		}
		current_disk->current_files++;

		fullname = malloc(strlen(current_disk->path) + strlen(req->filename) + 1);
		if(!fullname) {
			SLAVE_DBG("%I64u: Memory error", p->uid);
			xfer_destroy(xfer);
			if(!enqueue_packet(p->uid, IO_FAILURE, NULL, 0)) return 0;
			return 1;
		}
		sprintf(fullname, "%s%s", current_disk->path, &req->filename[1]);
		_creat_recursive(fullname);
		free(fullname);

		/* create the file on the current disk */
		file = file_map_add(current_disk, req->filename);
		if(!file) {
			SLAVE_DBG("%I64u: Could not create %s", p->uid, req->filename);
			xfer_destroy(xfer);
			if(!enqueue_packet(p->uid, IO_FAILURE, NULL, 0)) return 0;
			return 1;
		}
	}

	/* link this xfer to the file */
	if(!collection_add(file->xfers, xfer)) {
		SLAVE_DBG("%I64u: Collection error", p->uid);
		xfer_destroy(xfer);
		if(!enqueue_packet(p->uid, IO_FAILURE, NULL, 0)) return 0;
		return 0;
	}

	/* link the file to this xfer */
	xfer->file = file;

	if(!setup_file_io(xfer)) {
		SLAVE_DBG("%I64u: Failed to setup file i/o", p->uid);
		xfer_destroy(xfer);
		if(!enqueue_packet(p->uid, IO_FAILURE, NULL, 0)) return 0;
		return 1;
	}
	
	SLAVE_DIALOG_DBG("%I64u: Transfer setup completed", p->uid);

	return 1;
}

/* delete a file and tell the master afterward if we succeded */
static unsigned int process_slave_delete(struct io_context *io, struct packet *p) {
	char *filename = (char*)&p->data;
	struct file_map *file;
	unsigned int i;

	/* correct the filename */
	for(i=0;i<strlen(filename);i++) if(filename[i] == '/') filename[i] = '\\';

	SLAVE_DIALOG_DBG("%I64u: Delete query received for %s", p->uid, filename);

	file = lookup_file(filename);
	if(!file) {
		SLAVE_DBG("%I64u: Could not find file %s", p->uid, filename);
		if(!enqueue_packet(p->uid, IO_FAILURE, NULL, 0)) return 0;
		return 1;
	}

	file_unmap(file);

	if(!enqueue_packet(p->uid, IO_DELETED, NULL, 0))
		return 0;

	return 1;
}

/*
	when we connect to the master it sends us our
	deletelog wich are all the files that were deleted
	while we were offline.
*/
static unsigned int process_slave_deletelog(struct io_context *io, struct packet *p) {
	char *buffer = (char*)&p->data;
	struct file_map *file;
	unsigned int i;
	unsigned int current, length, line;
	char *ptr, *next;

	SLAVE_DIALOG_DBG("%I64u: Deletelog query received", p->uid);

	length = (p->size - sizeof(struct packet));

	current = 0;
	next = buffer;
	while(current<length) {
		ptr = next;

		for(line = 0;current+(line+1) < length;line++) {
			if((ptr[line] == '\r') || (ptr[line] == '\n')) {
				ptr[line] = 0;
				break;
			}
		}

		current += (line+1);
		next = ptr + (line+1);

		if(!line)
			continue;
		
		/* correct the filename */
		for(i=0;i<strlen(ptr);i++) if(ptr[i] == '/') ptr[i] = '\\';

		file = lookup_file(ptr);
		if(!file) {
			SLAVE_DBG("%I64u: Could not find file %s", p->uid, ptr);
		}
		else {
			file_unmap(file);
		}
	}

	/*
		wathever we did or not succeded to delete all
		files, we'll say it's good enough.
	*/
	if(!enqueue_packet(p->uid, IO_DELETED, NULL, 0))
		return 0;

	return 1;
}

/* dispatch the input from the master
	to the correct handler */
static unsigned int handle_inputs(struct io_context *io) {
	struct packet *p;
	unsigned int ret;

	if(!io_read_packet(io, &p, INFINITE)) {
		SLAVE_DBG("Error while reading data packet");
		return 0;
	}
	if(!p) {
		SLAVE_DBG("No error, but no packet.");
		return 1;
	}

	//SLAVE_DBG("Parsing packet %I64u", p->uid);

	switch(p->type) {
	case IO_PUBLIC_KEY:	/* reply with FAILURE or the same type */
		ret = process_public_key(io, p);
		break;
	case IO_BLOWFISH_KEY:	/* reply with FAILURE or the same type */
		ret = process_blowfish_key(io, p);
		break;
	case IO_HELLO:	/* reply with FAILURE or the same type */
		ret = process_hello(io, p);
		break;
	case IO_FILE_LIST:	/* reply with FAILURE or the same type */
		ret = process_file_list(io, p);
		break;
	case IO_SLAVE_LISTEN:	/* reply with FAILURE or SLAVE_LISTENING */
		ret = process_slave_listen(io, p);
		break;
	case IO_SLAVE_TRANSFER:	/* reply with FAILURE or SLAVE_TRANSFERED */
		ret = process_slave_transfer(io, p);
		break;
	case IO_DELETE: /* reply with FAILURE or DELETED */
		ret = process_slave_delete(io, p);
		break;
	case IO_DELETELOG: /* reply with FAILURE or DELETED */
		ret = process_slave_deletelog(io, p);
		break;
	case IO_SFV: /* reply with FAILURE or the same type */
		ret = process_slave_sfv(io, p);
		break;
	case IO_STATS: /* reply with FAILURE or the same type */
		ret = process_slave_stats(io, p);
		break;
	default:
		ret = reply_failure(io, p);
		break;
	}

	free(p);
	return ret;
}

/* send the first packet in the queue to the master */
static unsigned int handle_outputs(struct io_context *io) {
	struct packet *p;
	unsigned int success;

	p = collection_first(enqueued_packets);
	if(!p)
		/* no packet to be sent, it's alright */
		return 1;

	/*
		we must delete the packet now because
		io_write_packet may relocate it...
	*/
	collection_delete(enqueued_packets, p);

	success = io_write_packet(io, &p);
	free(p);

	return success;
}

/* store all files found in 'path' and add it to the file list of 'disk' */
static unsigned int map_files_from_path(struct disk_map *disk, const char *path) {
	WIN32_FIND_DATA fd;
	char *sub_path;
	HANDLE iter;
	unsigned int directory;
	char *full_path;
	struct file_map *file;

	full_path = malloc(strlen(disk->path)+strlen(path)+2);
	if(!full_path) {
		SLAVE_DBG("Memory error");
		return 0;
	}
	sprintf(full_path, "%s%s*", disk->path, &path[1]);

	iter = FindFirstFile(full_path, &fd);
	if(iter == INVALID_HANDLE_VALUE) {
		SLAVE_DBG("Path could not be walked: %s", full_path);
		free(full_path);
		return 0;
	}
	free(full_path);

	do {
		if(!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;

		directory = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;

		sub_path = malloc(strlen(path)+strlen(fd.cFileName)+2);
		if(!sub_path) continue;
		sprintf(sub_path, "%s%s%c", path, fd.cFileName, directory ? '\\' : 0);

		/* if the file is a directory, call this function recursively */
		if(directory) {
			if(!map_files_from_path(disk, sub_path)) {
				free(sub_path);
				continue;
			}
		} else {
			file = file_map_add(disk, sub_path);
			if(!file) {
				free(sub_path);
				continue;
			}
		}

		free(sub_path);

	} while(FindNextFile(iter, &fd));

	FindClose(iter);

	return 1;
}

/* wrapper around map_files_from_path */
static unsigned int map_files_from_disk(struct disk_map *disk) {

	if(!map_files_from_path(disk, "\\")) {
		SLAVE_DBG("Could not map files in %s", disk->path);
		return 0;
	}

	return collection_size(disk->files_collection);
}

/* take something like c:/dir as input and
	return something like c:\dir\ */
static char *normalize_path(const char *path) {
	unsigned int i;
	char *normalized;

	normalized = malloc(strlen(path)+3);
	if(!normalized) {
		SLAVE_DBG("Memory error");
		return NULL;
	}
	strcpy(normalized, path);

	/* turn / into \ */
	for(i=0;i<strlen(normalized);i++) if(normalized[i] == '/') normalized[i] = '\\';

	/* add a tailing \ if not present */
	if(normalized[strlen(normalized)-1] != '\\') strcat(normalized, "\\");

	return normalized;
}

/* add a disk map to the mapped disks collection */
static struct disk_map *disk_map_add(const char *path, unsigned int threshold) {
	struct disk_map *disk;
	struct _diskfree_t driveinfo;
	unsigned int drive = 0;
	char *fullpath;

	fullpath = _fullpath(NULL, path, 0);
	if(fullpath) {
		disk = malloc(sizeof(struct disk_map) + strlen(fullpath) + 1);
		if(!disk) {
			SLAVE_DBG("Memory error");
			return NULL;
		}
		strcpy(disk->path, fullpath);
		free(fullpath);
	} else {
		disk = malloc(sizeof(struct disk_map) + strlen(path) + 1);
		if(!disk) {
			SLAVE_DBG("Memory error");
			return NULL;
		}
		strcpy(disk->path, path);
	}

	if((disk->path[0] >= 'a') && (disk->path[0] <= 'z')) drive = disk->path[0] - 'a'+1;
	if((disk->path[0] >= 'A') && (disk->path[0] <= 'Z')) drive = disk->path[0] - 'A'+1;
	if(!drive) {
		free(disk);
		return NULL;
	}

	if(_getdiskfree(drive, &driveinfo)) {
		SLAVE_DBG("Cant get drive stats for %c", disk->path[0]);
		free(disk);
		return NULL;
	}

	disk->diskfree = (unsigned long long int)(
						(unsigned long long int)driveinfo.avail_clusters *
						(unsigned long long int)driveinfo.sectors_per_cluster *
						(unsigned long long int)driveinfo.bytes_per_sector);
	disk->disktotal = (unsigned long long int)(
						(unsigned long long int)driveinfo.total_clusters *
						(unsigned long long int)driveinfo.sectors_per_cluster *
						(unsigned long long int)driveinfo.bytes_per_sector);

	disk->current_files = 0;
	disk->threshold = threshold;

	disk->files_collection = collection_new();
	if(!collection_add(mapped_disks, disk)) {
		collection_destroy(disk->files_collection);
		free(disk);
		return NULL;
	}

	return disk;
}

/* load the config for the disks from config file */
static unsigned int load_config() {
	unsigned int threshold;
	struct disk_map *disk;
	unsigned int i = 0, files;
	char buffer[128];
	char *path, *tmp;
	char *p;

	for(i=1;;i++) {
		sprintf(buffer, "disk(%u).path", i);
		tmp = config_raw_read(SLAVE_CONFIG_FILE, buffer, NULL);
		if(!tmp) break;

		path = normalize_path(tmp);
		free(tmp);
		if(!path) break;

		/* TODO: - expand environment variables
				 - handle relative paths		*/

		sprintf(buffer, "disk(%u).threshold", i);
		threshold = config_raw_read_int(SLAVE_CONFIG_FILE, buffer, 1);

		disk = disk_map_add(path, threshold);
		if(!disk) {
			SLAVE_DBG("Could not add a disk map for %s", path);
			free(path);
			continue;
		}
		free(path);

		/* fill the disk's files collection with files from the hard drive */
		files = map_files_from_disk(disk);

		SLAVE_DBG("Mapped %u files from %s (threshold at %u files)", files, disk->path, threshold);
	}

	i--;
	SLAVE_DBG("Loaded %u disks", i);

	/* load & create buffer */
	fsd_buffer_up = config_raw_read_int(SLAVE_CONFIG_FILE, "slave.buffer.upload", SLAVE_UP_BUFFER_SIZE);
	if(!fsd_buffer_up) {
		SLAVE_DBG("slave.buffer.upload was invalid, defaulted to %u", SLAVE_UP_BUFFER_SIZE);
		fsd_buffer_up = SLAVE_UP_BUFFER_SIZE;
	}
	fsd_buffer_down = config_raw_read_int(SLAVE_CONFIG_FILE, "slave.buffer.download", SLAVE_DN_BUFFER_SIZE);
	if(!fsd_buffer_down) {
		SLAVE_DBG("slave.buffer.download was invalid, defaulted to %u", SLAVE_DN_BUFFER_SIZE);
		fsd_buffer_down = SLAVE_DN_BUFFER_SIZE;
	}

	working_buffer = malloc(fsd_buffer_up > fsd_buffer_down ? fsd_buffer_up : fsd_buffer_down);
	if(!working_buffer) {
		SLAVE_DBG("Error while allocating working buffer: %u bytes is too much?",
			fsd_buffer_up > fsd_buffer_down ? fsd_buffer_up : fsd_buffer_down);
		return 0;
	}

	/* load master connection count */
	master_connections = config_raw_read_int(SLAVE_CONFIG_FILE, "master.connections", 0);

	/* load our name */
	slave_name = config_raw_read(SLAVE_CONFIG_FILE, "slave.name", NULL);
	if(!slave_name) {
		SLAVE_DBG("slave.name is NOT set!");
		return 0;
	}

	
	/* load the master hostname */
	p = config_raw_read(SLAVE_CONFIG_FILE, "master.host", NULL);
	if(!p) {
		SLAVE_DBG("master.host not set!");
		return 0;
	}

	i = socket_addr(p);
	if(i && (i != -1))
		master_ip = i;

	free(p);

	/* load the master connection port */
	master_port = config_raw_read_int(SLAVE_CONFIG_FILE, "master.port", 0);
	if(!master_port || (master_port & 0xffff0000)) {
		SLAVE_DBG("Valid range for master.port is 1-65535");
		return 0;
	}

	p = config_raw_read(SLAVE_CONFIG_FILE, "master.proxy", NULL);
	if(p) {
		if(socket_split_addr(p, &tmp, &proxy_port)) {

			i = socket_addr(tmp);
			if(i && (i != -1)) {
				SLAVE_DBG("Using proxy %s:%u to contact the master", tmp, proxy_port);

				proxy_ip = i;
				use_proxy = 1;
			}
			free(tmp);
		}
		free(p);
	}

	p = config_raw_read(SLAVE_CONFIG_FILE, "slave.proxy.incoming", NULL);
	if(p) {
		if(socket_split_addr(p, &tmp, &pasv_proxy_port)) {

			i = socket_addr(tmp);
			if(i && (i != -1)) {
				SLAVE_DBG("Using proxy %s:%u for PASV data connection", tmp, pasv_proxy_port);

				pasv_proxy_ip = i;
				use_pasv_proxy = 1;
			}
			free(tmp);
		}
		free(p);
	}

	p = config_raw_read(SLAVE_CONFIG_FILE, "slave.proxy.outgoing", NULL);
	if(p) {
		if(socket_split_addr(p, &tmp, &data_proxy_port)) {

			i = socket_addr(tmp);
			if(i && (i != -1)) {
				SLAVE_DBG("Using proxy %s:%u for PORT data connection", tmp, data_proxy_port);

				data_proxy_ip = i;
				use_data_proxy = 1;
			}
			free(tmp);
		}
		free(p);
	}

	return 1;
}

struct main_ctx {
	unsigned int connected;
	struct io_context io;
	struct collection *group;

	int proxy_connected;
	struct packet *query;
	struct packet *reply;
	unsigned int filledsize;
};

int main_socket_read(int fd, struct main_ctx *main_ctx) {
	unsigned int ret;

	if(!main_ctx->proxy_connected) {

		ret = packet_read(fd, &main_ctx->reply, &main_ctx->filledsize);
		if(!ret) {
			if(!main_ctx->reply) {
				/* should NEVER happen */
				main_ctx->connected = 0;
				SLAVE_DBG("Fatal error while reading packet");
				return 0;
			} else {
				/* packet could not be entierly read */
				SLAVE_DBG("Packet only partially read");
				return 1;
			}
		}

		if(ret && !main_ctx->reply) {
			/* there was not enough data yet */
			SLAVE_DBG("Not enough data to call it a packet just yet.");
			return 1;
		}

		/* check the packet */
		if(main_ctx->reply->type != PROXY_CONNECTED) {
			SLAVE_DBG("First packet from the proxy was not connect confirmation");
			main_ctx->connected = 0;
			return 0;
		}

		main_ctx->proxy_connected = 1;
	} else {
		if(!handle_inputs(&main_ctx->io)) {
			SLAVE_DBG("Lost connection to master while reading");
			main_ctx->connected = 0;
			return 0;
		}
	}

	return 1;
}

int main_socket_read_timeout(struct main_ctx *main_ctx) {
	SLAVE_DBG("Timeout while reading from master");
	main_ctx->connected = 0;
	return 0;
}

int main_socket_write(int fd, struct main_ctx *main_ctx) {

	if(!main_ctx->proxy_connected && main_ctx->query) {

		if(send(fd, (void *)main_ctx->query, main_ctx->query->size, 0) != main_ctx->query->size) {
			SLAVE_DBG("send() error, could not sent the whole of %u bytes", main_ctx->query->size);
			main_ctx->connected = 0;
			return 0;
		}

		free(main_ctx->query);
		main_ctx->query = NULL;

	} else {
		if(!handle_outputs(&main_ctx->io)) {
			SLAVE_DBG("Lost connection to master while writing");
			main_ctx->connected = 0;
			return 0;
		}
	}

	return 1;
}

int main_socket_write_timeout(struct main_ctx *main_ctx) {
	SLAVE_DBG("Timeout while writing to master");
	main_ctx->connected = 0;
	return 0;
}

int main_socket_connect(int fd, struct main_ctx *main_ctx) {
	struct proxy_connect connect;
	struct signal_callback *s;

	/* connection accepted on master socket ! */
	SLAVE_DBG("Main socket is now connected !");

	/* register read and write callbacks */
	s = socket_monitor_signal_add(fd, main_ctx->group, "socket-read", (signal_f)main_socket_read, main_ctx);
	signal_timeout(s, SLAVE_MASTER_TIMEOUT, (timeout_f)main_socket_read_timeout, main_ctx);

	s = socket_monitor_signal_add(fd, main_ctx->group, "socket-write", (signal_f)main_socket_write, main_ctx);
	signal_timeout(s, SLAVE_MASTER_TIMEOUT, (timeout_f)main_socket_write_timeout, main_ctx);

	socket_set_max_read(fd, SLAVE_MASTER_SOCKET_SIZE);
	socket_set_max_write(fd, SLAVE_MASTER_SOCKET_SIZE);

	if(!use_proxy) {
		main_ctx->proxy_connected = 1;
	} else {
		connect.ip = master_ip;
		connect.port = master_port;

		main_ctx->query = packet_new(current_proxy_uid++, PROXY_CONNECT, &connect, sizeof(connect));
		if(!main_ctx->query) {
			SLAVE_DBG("Could not create query packet for proxy");
			main_ctx->connected = 0;
			return 0;
		}
		main_ctx->reply = NULL;
		main_ctx->filledsize = 0;
		main_ctx->proxy_connected = 0;
	}

	return 1;
}

int main_socket_error(int fd, struct main_ctx *main_ctx) {

	/* connection error on master socket */
	SLAVE_DBG("Main socket caused an error");
	signal_clear(main_ctx->group);
	main_ctx->connected = 0;

	return 1;
}

int main_socket_close(int fd, struct main_ctx *main_ctx) {

	/* connection closed on master socket */
	SLAVE_DBG("Main socket closed gracefully");
	signal_clear(main_ctx->group);
	main_ctx->connected = 0;

	return 1;
}

void set_current_path() {
	char full_path[MAX_PATH];
	char *ptr;

	GetModuleFileName(NULL, full_path, sizeof(full_path));
	ptr = strchr(full_path, '\\');
	if(ptr) {
		while(strchr(ptr+1, '\\')) ptr = strchr(ptr+1, '\\');
		*(ptr+1) = 0;
	}
	_chdir(full_path);

	return;
}

#ifdef SLAVE_WIN32_SERVICE
int win32_service_main() {
#else
int main(int argc, char* argv[]) {
#endif
	unsigned long long int attempts = 0;
	struct main_ctx main_ctx;

	set_current_path();

#ifdef SLAVE_SILENT_CRASH
	SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
#endif

	crypto_init();
	socket_init();

	mapped_disks = collection_new();

	enqueued_packets = collection_new();
	xfers_collection = collection_new();
	main_ctx.group = collection_new();

	if(!load_config()) {
		SLAVE_DBG("Could not load config from " SLAVE_CONFIG_FILE);
		return 1;
	}

	while(!master_connections || (attempts < master_connections)) {

		attempts++;

		if(!master_connections)
			SLAVE_DBG("Connection attempt #%I64u on (infinite)", attempts);
		else
			SLAVE_DBG("Connection attempt #%I64u on (%u)", attempts, master_connections);


		/* create the socket */
		main_ctx.io.p = NULL;
		main_ctx.io.filled_size = 0;
		main_ctx.io.flags = 0;
		main_ctx.io.compression_threshold = 0;
		main_ctx.io.fd = connect_to_ip_non_blocking(use_proxy ? proxy_ip : master_ip, use_proxy ? proxy_port : master_port);
		if(main_ctx.io.fd == -1) {
			SLAVE_DBG("Could not create main socket");
			Sleep(10000);
			continue;
		}

		/* register the socket monitor and all events */
		socket_monitor_new(main_ctx.io.fd, 0, 0);
		socket_monitor_signal_add(main_ctx.io.fd, main_ctx.group, "socket-connect", (signal_f)main_socket_connect, &main_ctx);
		socket_monitor_signal_add(main_ctx.io.fd, main_ctx.group, "socket-error", (signal_f)main_socket_error, &main_ctx);
		socket_monitor_signal_add(main_ctx.io.fd, main_ctx.group, "socket-close", (signal_f)main_socket_close, &main_ctx);

		main_ctx.connected = 1;

		/* loop until we are disconnected from the master */
		do {

			//signal_poll();

			socket_poll();

			Sleep(SLAVE_SLEEP_TIME);

		} while(main_ctx.connected);

		SLAVE_DBG("Connection lost from master !");

		signal_clear(main_ctx.group);

		if(main_ctx.io.fd != -1) {
			//signal_clear_all_with_filter(main_ctx.group, (void *)main_ctx.io.fd);
			socket_monitor_fd_closed(main_ctx.io.fd);
			close_socket(main_ctx.io.fd);
			main_ctx.io.fd = -1;
		}

		if(main_ctx.io.p) {
			free(main_ctx.io.p);
			main_ctx.io.p = NULL;
		}

		/* we lost connection to master,
			cleanup all xfers. */
		while(collection_size(xfers_collection)) {
			void *first = collection_first(xfers_collection);
			xfer_destroy(first);
			collection_delete(xfers_collection, first);
		}

		/* also cleanup all enqueued packets */
		while(collection_size(enqueued_packets)) {
			void *first = collection_first(enqueued_packets);
			free(first);
			collection_delete(enqueued_packets, first);
		}

		Sleep(10000);
	}

	SLAVE_DBG("Giving up connection attempts !");

	collection_destroy(main_ctx.group);
	collection_destroy(enqueued_packets);
	collection_destroy(xfers_collection);

	socket_free();
	crypto_free();

	return 0;
}

#ifdef SLAVE_WIN32_SERVICE
int main(int argc, char* argv[]) {
	char *name;
	char *displayname;
	char *desc;
	
	set_current_path();

	name = config_raw_read(SLAVE_CONFIG_FILE, "slave.service.name", "xFTPd-slave");
	displayname = config_raw_read(SLAVE_CONFIG_FILE, "slave.service.displayname", "xFTPd-slave");
	desc = config_raw_read(SLAVE_CONFIG_FILE, "slave.service.description", "xFTPd-slave");

	// if we're a service, it means someone *else* will get the result
	if(service_start_and_call(name, displayname, desc, (func)&win32_service_main)) {
		return 1;
	}

	return 0;
}
#endif
