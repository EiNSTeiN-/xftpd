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

#ifndef __CONFIG_H
#define __CONFIG_H

#include "constants.h"
#include "obj.h"
#include "collection.h"

#include "debug.h"
#if defined(DEBUG_CONFIG)
# define CONFIG_DBG(format, arg...) { _DEBUG_CONSOLE(format, ##arg) _DEBUG_FILE(format, ##arg) }
#else
# define CONFIG_DBG(format, arg...)
#endif

typedef struct config_field config_field;
struct config_field {
	struct obj o;
	struct collectible c;
	
	unsigned int namesum;
	char *name;
	char *value;
	
	/* comments just before this field */
	char *comments;
} __attribute__((packed));

typedef struct config_file config_file;
struct config_file {
	struct obj o;
	struct collectible c;
	
	/* Fully qualified path to the file. */
	char *filename;
	
	/* The adio file and optionally the adio operation
		if any is started. */
	struct adio_file *adf;
	struct adio_operation *op;
	
	/* reference count to this config structure */
	int refs;
	
	int is_saving	:1; /* nonzero if the file is currently being saved. */
	int shouldsave	:1; /* nonzero if the file should be saved to disk */
	
	/* timestamp at which the file was last saved. */
	unsigned long long int savetime;
	
	/* comments after the last field are stored here... */
	char *comments;
	
	/* All fields of the file */
	struct collection *fields; /* struct config_field */
} __attribute__((packed));


extern struct collection *open_configs;


/*
	Low level access to config files
*/
char *config_load_file(const char *filename, unsigned int *length);

char *config_raw_read(const char *filename, const char *param, const char *default_value);
int config_raw_write(const char *filename, const char *param, const char *new_value);

/* Integer operations are only present for convenience.
	They are wrappers around the two functions above. */
unsigned long long int config_raw_read_int(const char *filename, const char *param, unsigned long long int default_value);
int config_raw_write_int(const char *filename, const char *param, unsigned long long int new_value);



/*
	Opens the specified configuration file.
	
	If the file was already opened, then the reference count
	of the config_file structure is incremented.
	
	If the file was not already opened, the file is loaded
	in memory during this call.
*/
struct config_file *config_open(const char *filename);
	
/*
	Creates an empty config structure that is never writte
	to the disk.
*/
struct config_file *config_volatile();

/*
	Starts an asynchronous operation to save the contents
	of the file to disk. The file is saved automaticly every
	5 minutes by default.
*/
int config_save(struct config_file *config);

/*
	Decrements the reference count of the config structure.

	If the reference count reach zero, the file is first saved
	if there is any unsaved changes, then the file is closed.
*/
void config_close(struct config_file *config);

/* Return a copy of the value associated with parameter. The returned value that must be freed
	by the caller. If the parameter has no value stored, the string in default_value will be copied and
	returned. The return value is NULL if the parameter has no associated value AND default_value is also NULL,
	or if any error occurs. */
char *config_read(struct config_file *config, const char *param, const char *default_value);
unsigned long long int config_read_int(struct config_file *config, char *param, unsigned long long int default_value);

/* Same as above, but return the internal copy of the value associated with the parameter. The
	returned pointer must not be modified or kept by the caller across calls. The caller must
	not attempt to free the returned pointer. If the parameter has no value, then the exact pointer
	passed in default_value is returned. The return value is NULL if the parameter has no
	associated value AND default_value is also NULL, or if any error occurs. */
const char *config_pread(struct config_file *config, const char *param, const char *default_value);

/*	Replace the current value associated with the parameter. If the parameter had not yet been
	assigned any value, the parameter is created and will be written to the config file on disk.
	If the new value is NULL, the parameter is deleted and removed from the file on disk. If the
	parameter had not yet been assigned any value, the and the new value is NULL, then the config
	file say unchanged on disk. */
int config_write(struct config_file *config, const char *param, const char *new_value);
int config_write_int(struct config_file *config, const char *param, unsigned long long int new_value);

int config_field_comment(struct config_file *config, const char *param, const char *new_comment);
int config_file_comment(struct config_file *config, const char *new_comment);

int config_poll();

#endif /* __CONFIG_H */
