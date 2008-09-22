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

#include "debug.h"
#if defined(DEBUG_ADIO)
# define ADIO_DBG(format, arg...) { _DEBUG_CONSOLE(format, ##arg) _DEBUG_FILE(format, ##arg) }
#else
# define ADIO_DBG(format, arg...)
#endif

struct adio_file {
	unsigned int balance; /* running count of operations */
#ifdef WIN32
	HANDLE s; /* stream */
#else
  int s; /* file descriptor */
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
#else
  int read; /* 1 if reading, 0 otherwise */
#endif
} __attribute__((packed));

/*
	Open the file 'filename' and return an adio_file structure
	representing the open file. If create is set then the file
	may not exist, otherwise the operation fail if the file is
	not already created.
*/
struct adio_file *adio_open(const char *filename, int create);

/*
	Destroy the adio_file structure.
*/
void adio_close(struct adio_file *adio);

/*
	Create a read operation on an opened file. If 'op' is not NULL then the already
	created structure is reused.

	The 'buffer' may change between calls with the same 'op'.
*/
struct adio_operation *adio_read(struct adio_file *file, char *buffer, unsigned long long int position,
									unsigned int length, struct adio_operation *_op);

/*
	Same as adio_read but for write operations.
*/
struct adio_operation *adio_write(struct adio_file *file, char *buffer, unsigned long long int position,
									unsigned int length, struct adio_operation *_op);

/*
	Probe the adio_operation structure. 'ready' is set to the currently available
	number of bytes. Return 1 if the operation is complete, zero if there is still more
	data to be read and -1 on error.
*/
int adio_probe(struct adio_operation *op, unsigned int *ready);

/*
	Destroy the adio_operation structure.
*/
void adio_complete(struct adio_operation *op);


#endif /* __ADIO_H */
