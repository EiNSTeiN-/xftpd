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

#ifndef NO_FTPD_DEBUG
#  define DEBUG_CONFIG
#endif

#ifdef DEBUG_CONFIG
# include "logging.h"
# ifdef FTPD_DEBUG_TO_CONSOLE
#  define CONFIG_DBG(format, arg...) printf("["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg)
# else
#  define CONFIG_DBG(format, arg...) logging_write("debug.log", "["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg)
# endif
#else
#  define CONFIG_DBG(format, arg...)
#endif

typedef struct config_field config_field;
struct config_field {
	struct obj o;
	struct collectible c;
	
	char modified; /* since the last save */

	unsigned int namesum;
	char *name;
	char *value;
} __attribute__((packed));

typedef struct config_file config_file;
struct config_file {
	struct obj o;
	struct collectible c;
	
	unsigned long long int timeout; /* maximum time between each saves */
	unsigned long long int savetime; /* last time the file was saved to disk */
	int modified; /* 1 if any field was modified since the last save */

	/* Fully qualified path to the file. */
	char *filename;

	/* 1 if the file is loaded in memory */
	int loaded;

	/* All fields of the file */
	struct collection *fields; /* struct config_field */
} __attribute__((packed));

/*
	Low level access to config files
*/
char *config_load_file(const char *filename, unsigned int *length);
char *config_lua_load_file(const char *filename, unsigned int *length);

char *config_raw_read(const char *filename, const char *param, const char *default_value);
char *config_raw_lua_read(const char *filename, const char *param, const char *default_value);
int config_raw_write(const char *filename, const char *param, const char *new_value);

/* Integer operations are only present for convenience. */
unsigned long long int config_raw_read_int(const char *filename, const char *param, unsigned long long int default_value);
int config_raw_write_int(const char *filename, const char *param, unsigned long long int new_value);


/*
	High-level access to config files
*/

/* Initialize the config structure without loading the file */
struct config_file *config_new(const char *filename, unsigned long long int timeout);

/* Destroy a config structure, discarding any unsaved changes */
void config_destroy(struct config_file *config);

/* Force a config file to be loaded to memory */
int config_load(struct config_file *config);

/* Force any changes in a config file to be saved to disk */
int config_save(struct config_file *config);

/* Check for timeouts and save files to disk if needed */
int config_poll();

/*
	Read/write access to config files. If the file is not loaded,
	it will be implicitly loaded by any of those functions.

	Integer operations are simply wrapped around thier srting
	equivalents.
*/

/* Return a copy of the value that must be freed by the caller. */
char *config_read(struct config_file *config, const char *param, const char *default_value);
char *config_lua_read(struct config_file *config, const char *param, const char *default_value);
unsigned long long int config_read_int(struct config_file *config, char *param, unsigned long long int default_value);

int config_write(struct config_file *config, const char *param, const char *new_value);
int config_write_int(struct config_file *config, const char *param, unsigned long long int new_value);

#endif /* __CONFIG_H */
