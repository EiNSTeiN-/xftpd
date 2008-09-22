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

#ifndef __FSD_H
#define __FSD_H

#include "constants.h"

#ifndef NO_FTPD_DEBUG
#  define DEBUG_SLAVE
//#  define DEBUG_SLAVE_DIALOG
#endif

#ifdef DEBUG_SLAVE
# ifdef FTPD_DEBUG_TO_CONSOLE
#  define SLAVE_DBG(format, arg...) printf("["__FILE__ ":\t\t%d ]\t" format "\n", __LINE__, ##arg)
# else
#  define SLAVE_DBG(format, arg...) logging_write("debug.log", "["__FILE__ ":\t\t%d ]\t" format "\n", __LINE__, ##arg)
# endif
#else
#  define SLAVE_DBG(format, arg...)
#endif

#ifdef DEBUG_SLAVE_DIALOG
# ifdef FTPD_DEBUG_TO_CONSOLE
#  define SLAVE_DIALOG_DBG(format, arg...) printf("["__FILE__ ":\t\t%d ]\t" format "\n", __LINE__, ##arg)
# else
#  define SLAVE_DIALOG_DBG(format, arg...) logging_write("debug.log", "["__FILE__ ":\t\t%d ]\t" format "\n", __LINE__, ##arg)
# endif
#else
#  define SLAVE_DIALOG_DBG(format, arg...)
#endif

#include "collection.h"


struct slave_xfer {
	/* the uid is shared with the master. it is used to
		identify the xfer */
	unsigned long long int uid;

	unsigned int speedsize;
	unsigned long long int speedcheck;
	unsigned int lastsize;

	unsigned long long int timestamp;

	unsigned int ip;
	unsigned short port;
	unsigned long long int listen_uid;

	unsigned int passive; /* 1 on passive (listening) connections */
	unsigned int upload; /* 0 on downloads */
	unsigned long long int restart; /* where to restart the file transfer */

	unsigned int buffersize;

	int completed; /* Operation completed. */
	int connected; /* connected (socket init is done) */
	struct collection *group;
	int fd;

	unsigned int proxy_connected;
	struct packet *query;
	struct packet *reply;
	unsigned int filledsize;

	unsigned long long int xfered; /* xfered size */
	unsigned int checksum; /* checksum for the transfer */

	unsigned int ready; /* ready to transfer the file? */
	struct file_map *file;

	/* uid of the asynch command from wich the transfer
		request arrived, we will need it to send the
		response to the master when the transfer will
		be completed */
	unsigned long long int asynch_uid;

} __attribute__((packed));

struct slave_listen_reply {
	unsigned int ip;
	unsigned short port;
} __attribute__((packed));

struct slave_transfer_reply {
	unsigned long long int xfersize; /* size transfered */
	unsigned int checksum; /* checksum for the transfer */
	unsigned long long int filesize; /* size of the file */
} __attribute__((packed));

struct file_list_entry {
	unsigned int entry_size; /* size of this entry */
	unsigned long long int size; /* size of the file described */
	unsigned long long int timestamp; /* slave's local timestamp for the file */
	char name[1];
} __attribute__((packed));

struct slave_hello_data {
	unsigned long long int diskfree; /* this is the total of all mapped disks */
	unsigned long long int disktotal;
	unsigned long long int now; /* the now() of the slave at the moment it sends the hello packet */
	char name[1];
} __attribute__((packed));

struct file_map {
	struct disk_map *disk; /* points to the disk on wich is the file */

	/* infos for when the file is open */
	struct {
		/* reference count. when 0 is reached, the
			file is closed and the buffer is free'd */
		unsigned int refcount; 
		unsigned int upload; /* 0 when downloading */
		int stream;
	} io;

	unsigned long long int size; /* file size in bytes */
	unsigned long long int timestamp; /* modification date */
	/* TODO: crc checksum, ... */

	struct collection *xfers; /* current xfers for this file, collection of struct struct slave_xfer */

	char name[1];	/* name relative to the disk's path like hum\abc\file.bin */
} __attribute__((packed));

struct disk_map {
	unsigned long long int diskfree; /* updated after each file operation */
	unsigned long long int disktotal;
	
	unsigned int current_files; /* number of files stored in a row */
	unsigned int threshold; /* after how many files will we change the disk */

	/* collection of file_map strucures */
	/* all files are stored in a single collection and  */
	struct collection *files_collection;

	char path[1]; /* fully qualified path like C:\folder\ */
} __attribute__((packed));

#endif /* __FSD_H */
