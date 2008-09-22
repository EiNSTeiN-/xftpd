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

#include <stdio.h>


#include "logging.h"
#include "asprintf.h"
#include "constants.h"
#include "time.h"

void logging_write(const char* filename, const char *format, ...) {
	int result;
	char *str = NULL;
	FILE* file;

	va_list args;
	va_start(args, format);
	result = vasprintf(&str, format, args);
	if(result == -1) return;
	va_end(args);

	//printf(str);
	file = fopen(filename,"a");
	if(!file) {
		free(str);
		return;
	}
	if(fwrite(str,strlen(str),1,file) != 1) {
		free(str);
		fclose(file);
		return;
	}
	fclose(file);

	free(str);
	return;
}

void logging_write_file(FILE *file, const char *format, ...) {
	int result;
	char *str = NULL;

	va_list args;
	va_start(args, format);
	result = vasprintf(&str, format, args);
	if(result == -1) return;
	va_end(args);

	if(fwrite(str,strlen(str),1,file) != 1) {
		free(str);
		return;
	}

	free(str);
	return;
}

/*
// echo to the standard output
int logging_echo(const char *format, ...) {
	int result;
	char *str=NULL;

	va_list args;
	va_start(args, format);
	result = vasprintf(&str, format, args);
	if(result == -1) return 0;
	va_end(args);

	printf(str);

	free(str);
	return 1;
}
*/
