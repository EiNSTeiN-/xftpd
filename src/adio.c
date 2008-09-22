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

#include "adio.h"
#include "logging.h"
#include "time.h"

/*
	Asynchronous disk i/o functions for the file server daemon.
	
	Author:
		EiNSTeiN_
	
	Win32 docs:
		http://msdn2.microsoft.com/en-US/library/aa365683.aspx
*/

/*
	Open the file 'filename' and return an adio_file structure
	representing the open file. If create is set then the file
	may not exist.
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
	
	//ADIO_DBG("Opening %s", filename);
	
	adio->balance = 0;
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
		ADIO_DBG("Failed to CreateFile(%s, %u); GLE is %u", filename, (int)create, (int)GetLastError());
		free(adio);
		return NULL;
	} else {
	
		//ADIO_DBG("SUCCESS: %s", filename);
	}
	
	return adio;
}

/*
	Create a read operation on an opened file. If 'op' is not NULL then the already
	created structure is reused.

	The buffer may change between calls with the same 'op'.
*/
struct adio_operation *adio_read(struct adio_file *file, char *buffer, unsigned long long int position,
									unsigned int length, struct adio_operation *_op) {
	struct adio_operation *op;
	BOOL success;
	
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
		
		op->ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	} else {
		op = _op;
		
		/*if(op->ov.hEvent) {
			CloseHandle(op->ov.hEvent);
			op->ov.hEvent = NULL;
		}*/
	}
	
	op->timestamp = time_now();
	op->buffer = buffer;
	op->position = position;
	op->length = length;
	op->length_done = 0;
	
	op->ov.Internal = 0;
	op->ov.InternalHigh  = 0;
	op->ov.Offset = (position & 0xFFFFFFFF);
	op->ov.OffsetHigh = ((position >> 32) & 0xFFFFFFFF);
	
	//FlushFileBuffers(file->s);
	
	success = ReadFile(file->s, op->buffer, op->length, NULL, &op->ov);
	if(!success && (GetLastError() != ERROR_IO_PENDING)) {
		ADIO_DBG("Failed to ReadFile(); GLE is %u", (int)GetLastError());
		CloseHandle(op->ov.hEvent);
		free(op);
		return NULL;
	}
	
	//FlushFileBuffers(file->s);
	
	if(success) {
		op->length_done = op->length;
		//ADIO_DBG("ReadFile() succeeded on first call, %u bytes done", op->length_done);
	}
	
	return op;
}

/*
	Same as adio_read but for write operations.
*/
struct adio_operation *adio_write(struct adio_file *file, char *buffer, unsigned long long int position,
									unsigned int length, struct adio_operation *_op) {
		struct adio_operation *op;
	BOOL success;
	
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
		
		op->ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	} else {
		op = _op;
		
		/*if(op->ov.hEvent) {
			CloseHandle(op->ov.hEvent);
			op->ov.hEvent = NULL;
		}*/
	}
	
	op->timestamp = time_now();
	op->buffer = buffer;
	op->position = position;
	op->length = length;
	op->length_done = 0;
	
	op->ov.Internal = 0;
	op->ov.InternalHigh  = 0;
	op->ov.Offset = (position & 0xFFFFFFFF);
	op->ov.OffsetHigh = ((position >> 32) & 0xFFFFFFFF);
	
	//FlushFileBuffers(file->s);
	
	success = WriteFile(file->s, op->buffer, op->length, NULL, &op->ov);
	if(!success && (GetLastError() != ERROR_IO_PENDING)) {
		ADIO_DBG("Failed to WriteFile(); GLE is %u", (int)GetLastError());
		CloseHandle(op->ov.hEvent);
		free(op);
		return NULL;
	}
	
	if(success) {
		op->length_done = op->length;
		//ADIO_DBG("WriteFile() succeeded on first call, %u bytes done", op->length_done);
	}
	
	return op;
}

/*
	Probe the adio_operation structure. 'ready' is set to the currently available
	number of bytes. Return 1 if the operation is complete, zero if there is still more
	data to be read and -1 on error.
*/
int adio_probe(struct adio_operation *op, unsigned int *ready) {
	unsigned int length_done = 0;
	BOOL success;
	
	if(!op) {
		ADIO_DBG("Params error");
		return -1;
	}
	
	/* Check if the operation is already complete */
	if(op->length_done == op->length) {
		//ADIO_DBG("probe called on complete file, %u bytes done", op->length_done);
		success = HasOverlappedIoCompleted(&op->ov);
		if(!success) ADIO_DBG("wierd: overlapped operation did not yet complete");
		if(ready) *ready = op->length_done;
		return success ? 1 : 0;
	}
	
	/* Check if the operation completed yet */
	success = GetOverlappedResult(op->file->s, &op->ov, (void*)&length_done, FALSE);
	if(!success && /*(GetLastError() != ERROR_IO_PENDING) &&*/ (GetLastError() != ERROR_IO_INCOMPLETE)) {
		ADIO_DBG("could not get overlapped results; GLE is %u", (int)GetLastError());
		return -1;
	}
	
	/* Display the new length */
	/*if((length_done - op->length_done) != 0) {
		ADIO_DBG("probe detected %u more bytes read than last time (%u bytes done)",
			(length_done - op->length_done), op->length_done);
	}*/
	op->length_done = length_done;
	if(ready) *ready = op->length_done;
	
	/* Check if we're done */
	if(op->length_done == op->length) {
		//ADIO_DBG("probe detected that there is no more data to be read");
		success = HasOverlappedIoCompleted(&op->ov);
		if(!success) ADIO_DBG("wierd: overlapped operation did not yet complete");
		return success ? 1 : 0;
	}
	
	/* Looks like we're gonna have to wait more */
	return 0;
}

/*
	Destroy the adio_operation structure.
*/
void adio_complete(struct adio_operation *op) {
	
	if(!op) {
		ADIO_DBG("Params error");
		return;
	}
	
	if(op->ov.hEvent) {
		CloseHandle(op->ov.hEvent);
		op->ov.hEvent = NULL;
	}
	
	op->file->balance--;
	op->file = NULL;
	free(op);
	
	return;
}

/*
	Destroy the adio_file structureé.
*/
void adio_close(struct adio_file *adio) {
	
	if(!adio) {
		ADIO_DBG("Params error");
		return;
	}
	
	CloseHandle(adio->s);
	adio->s = NULL;
	
	if(adio->balance) {
		ADIO_DBG("ERROR: Could not close adio file because operation calls are unbalanced");
		/* we won't free the structure here */
		return;
	}
	
	free(adio);
	
	return;
}
