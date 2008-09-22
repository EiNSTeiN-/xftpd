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
#else
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#endif

#include "adio.h"
#include "logging.h"
#include "time.h"

/*
	Asynchronous disk i/o functions for the file server daemon.
	
	Author:
		EiNSTeiN_
	
	Win32 docs:
		http://msdn2.microsoft.com/en-US/library/aa365683.aspx
		
  Linux docs:
    http://linux.die.net/ - Check out docs pages for open(), read(), write(), and fcntl()
*/

struct adio_file *adio_open(const char *filename, int create) {
	struct adio_file *adio = NULL;
	
	if(!filename) {
		ADIO_DBG("Params error");
		return NULL;
	}
	
	adio = malloc(sizeof(struct adio_file));
	if(!adio) {
		ADIO_DBG("Memory error");
		return NULL;
	}
	
	ADIO_DBG("Opening %s", filename);
	
	adio->balance = 0;
	
#ifdef WIN32
	adio->s = CreateFile(
		filename,
		(GENERIC_READ | GENERIC_WRITE),
		(FILE_SHARE_READ | FILE_SHARE_WRITE),
		NULL,
		(create ? CREATE_ALWAYS : OPEN_EXISTING),
		(FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED),
		NULL
	);
	
	if(adio->s == INVALID_HANDLE_VALUE) {
		ADIO_DBG("Failed to adio_open(%s, %u); GLE is %u", filename, (int)create, (int)GetLastError());
		free(adio);
		return NULL;
	}
#else
  adio->s = open(filename, O_RDWR | (create ? O_CREAT : 0) | O_NONBLOCK);
  
  if(adio->s == -1) {
		ADIO_DBG("Failed to adio_open(%s, %u); errno is %u", filename, (int)create, (int)errno);
		free(adio);
		return NULL;
  }
#endif
	
	return adio;
}

struct adio_operation *adio_read(struct adio_file *file, char *buffer, unsigned long long int position,
									unsigned int length, struct adio_operation *_op) {
	struct adio_operation *op;
	int success;
  
	if(!file) {
		ADIO_DBG("Params error");
		return NULL;
	}
	
	if(!buffer) {
		ADIO_DBG("Params error");
		return NULL;
	}
	
	if(!_op) {
		op = malloc(sizeof(struct adio_operation));
		if(!op) {
			ADIO_DBG("Memory error");
			return NULL;
		}
		
		op->file = file;
		op->file->balance++;
		
#ifdef WIN32
		op->ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
#endif
	} else {
		op = _op;
	}
	
	op->timestamp = time_now();
	op->buffer = buffer;
	op->position = position;
	op->length = length;
	op->length_done = 0;
	
#ifdef WIN32
	op->ov.Internal = 0;
	op->ov.InternalHigh  = 0;
	op->ov.Offset = (op->position & 0xFFFFFFFF);
	op->ov.OffsetHigh = ((op->position >> 32) & 0xFFFFFFFF);
	
	success = (ReadFile(file->s, op->buffer, op->length, NULL, &op->ov) == TRUE);
	if(!success && (GetLastError() != ERROR_IO_PENDING)) {
		ADIO_DBG("Failed to ReadFile(); GLE is %u", (int)GetLastError());
		CloseHandle(op->ov.hEvent);
		free(op);
		return NULL;
	}
	
	if(success) {
		op->length_done = op->length;
		ADIO_DBG("ReadFile() succeeded on first call, %u bytes done", op->length_done);
	}
#else
  op->read = 1;
  
  success = pread64(file->s, op->buffer, op->length, op->position);
  if((success < 0) && (errno != EAGAIN)) {
		ADIO_DBG("Failed to pread64(); errno is %u", (int)errno);
		free(op);
		return NULL;
  }
  
  if(success == 0) {
		ADIO_DBG("pread64() says we're at EOF with %u/%u bytes read", op->length_done, op->length);
		free(op);
		return NULL;
  }
  
  if(success > 0) {
		op->length_done += success;
		ADIO_DBG("pread64() succeeded on first call, %u bytes done", op->length_done);
  }
#endif
	
	return op;
}

struct adio_operation *adio_write(struct adio_file *file, char *buffer, unsigned long long int position,
									unsigned int length, struct adio_operation *_op) {
	struct adio_operation *op;
	int success;
  
	if(!file) {
		ADIO_DBG("Params error");
		return NULL;
	}
	
	if(!buffer) {
		ADIO_DBG("Params error");
		return NULL;
	}
	
	if(!_op) {
		op = malloc(sizeof(struct adio_operation));
		if(!op) {
			ADIO_DBG("Memory error");
			return NULL;
		}
		
		op->file = file;
		op->file->balance++;

#ifdef WIN32
		op->ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
#endif
	} else {
		op = _op;
	}
	
	op->timestamp = time_now();
	op->buffer = buffer;
	op->position = position;
	op->length = length;
	op->length_done = 0;
	
#ifdef WIN32
	op->ov.Internal = 0;
	op->ov.InternalHigh  = 0;
	op->ov.Offset = (position & 0xFFFFFFFF);
	op->ov.OffsetHigh = ((position >> 32) & 0xFFFFFFFF);
	
	success = (WriteFile(file->s, op->buffer, op->length, NULL, &op->ov) == TRUE);
	if(!success && (GetLastError() != ERROR_IO_PENDING)) {
		ADIO_DBG("Failed to WriteFile(); GLE is %u", (int)GetLastError());
		CloseHandle(op->ov.hEvent);
		free(op);
		return NULL;
	}
	
	if(success) {
		op->length_done = op->length;
		ADIO_DBG("WriteFile() succeeded on first call, %u bytes read", op->length_done);
	}
#else
  op->read = 0;
  
  success = pwrite64(file->s, op->buffer, op->length, op->position);
  if((success < 0) && (errno != EAGAIN)) {
		ADIO_DBG("Failed to pwrite64(); errno is %u", (int)errno);
		free(op);
		return NULL;
  }
  
  if(success == 0) {
		ADIO_DBG("pwrite64() says we're at EOF with %u/%u bytes read", op->length_done, op->length);
		free(op);
		return NULL;
  }
  
  if(success > 0) {
		op->length_done += success;
		ADIO_DBG("pwrite64() succeeded on first call, %u bytes done", op->length_done);
  }
#endif
	
	return op;
}

int adio_probe(struct adio_operation *op, unsigned int *ready) {
	unsigned int length_done = 0;
	int success;
	
	if(!op) {
		ADIO_DBG("Params error");
		return -1;
	}
	
	/* Check if the operation is already complete */
	if(op->length_done == op->length) {
		ADIO_DBG("probe called on complete file, %u bytes done", op->length_done);
		if(ready) *ready = op->length_done;
#ifdef WIN32
		success = HasOverlappedIoCompleted(&op->ov);
		if(!success) ADIO_DBG("wierd: overlapped operation did not yet complete");
		return success ? 1 : 0;
#else
    return 1;
#endif
	}
	
	/* Check if the operation completed yet */
#ifdef WIN32
	success = GetOverlappedResult(op->file->s, &op->ov, (void*)&length_done, FALSE);
	if(!success && /*(GetLastError() != ERROR_IO_PENDING) &&*/ (GetLastError() != ERROR_IO_INCOMPLETE)) {
		ADIO_DBG("could not get overlapped results; GLE is %u", (int)GetLastError());
		return -1;
	}
#else
  if(op->read)
    success = pread64(op->file->s, op->buffer, op->length-op->length_done, op->position+op->length_done);
  else
    success = pwrite64(op->file->s, op->buffer, op->length-op->length_done, op->position+op->length_done);
  
  if((success < 0) && (errno != EAGAIN)) {
		ADIO_DBG("Failed to p%s64(); errno is %u", op->read ? "read" : "write", (int)errno);
  }
  
  if(success == 0) {
		ADIO_DBG("p%s64() says we're at EOF with %u/%u bytes read", op->read ? "read" : "write", op->length_done, op->length);
  }
  
  if(success > 0) {
		length_done += success;
		ADIO_DBG("p%s64() succeeded on first call, %u bytes done", op->read ? "read" : "write", op->length_done);
  }
#endif
	
	/* Display the new length */
	if((length_done - op->length_done) != 0) {
		ADIO_DBG("probe detected %u more bytes read than last time (%u bytes done)",
			(length_done - op->length_done), op->length_done);
	}
	op->length_done = length_done;
	if(ready) *ready = op->length_done;
	
	/* Check if we're done */
	if(op->length_done == op->length) {
		ADIO_DBG("probe detected that there is no more data to be read");
#ifdef WIN32
		success = HasOverlappedIoCompleted(&op->ov);
		if(!success) ADIO_DBG("wierd: overlapped operation did not yet complete");
		return success ? 1 : 0;
#else
    return 1;
#endif
	}
	
	/* Looks like we're gonna have to wait more */
	return 0;
}

void adio_complete(struct adio_operation *op) {
	
	if(!op) {
		ADIO_DBG("Params error");
		return;
	}
	
#ifdef WIN32
	if(op->ov.hEvent) {
		CloseHandle(op->ov.hEvent);
		op->ov.hEvent = NULL;
	}
#endif
	
	op->file->balance--;
	op->file = NULL;
	free(op);
	
	return;
}

/*
	Destroy the adio_file structure.
*/
void adio_close(struct adio_file *adio) {
	
	if(!adio) {
		ADIO_DBG("Params error");
		return;
	}
	
#ifdef WIN32
	CloseHandle(adio->s);
	adio->s = NULL;
#else
  close(adio->s);
  adio->s = -1;
#endif
  
	if(adio->balance) {
		ADIO_DBG("ERROR: Could not close adio file because operation calls are unbalanced");
		/* we won't free the structure here */
		return;
	}
	
	free(adio);
	
	return;
}
