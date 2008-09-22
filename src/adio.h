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

#ifndef __ADIO_H
#define __ADIO_H

#include "constants.h"

#ifndef NO_FTPD_DEBUG
#  define DEBUG_ADIO
#endif

#ifdef DEBUG_ADIO
# ifdef FTPD_DEBUG_TO_CONSOLE
#  define ADIO_DBG(format, arg...) printf("["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg)
# else
#  define ADIO_DBG(format, arg...) logging_write("debug.log", "["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg)
# endif
#else
#  define ADIO_DBG(format, arg...)
#endif

struct adio_file {
	unsigned int balance; /* running count of operations */
#ifdef WIN32
	HANDLE s; /* stream */
#endif
} __attribute__((packed));

struct adio_operation {
	unsigned long long int timestamp; /* time at wich the operation is started */
	char *buffer;
	unsigned long long int position; /* position in the file */
	unsigned int length; /* length to be retreived */
	unsigned int length_done;
	struct adio_file *file;
#ifdef WIN32
	OVERLAPPED ov;
#endif
} __attribute__((packed));

struct adio_file *adio_open(const char *filename, int create);
void adio_close(struct adio_file *adio);
	

struct adio_operation *adio_read(struct adio_file *file, char *buffer, unsigned long long int position,
									unsigned int length, struct adio_operation *_op);
struct adio_operation *adio_write(struct adio_file *file, char *buffer, unsigned long long int position,
									unsigned int length, struct adio_operation *_op);
int adio_probe(struct adio_operation *op, unsigned int *ready);
void adio_complete(struct adio_operation *op);


#endif /* __ADIO_H */
