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
#include <io.h>

#include "config.h"
#include "logging.h"
#include "collection.h"
#include "time.h"
#include "crc32.h"
#include "asprintf.h"

static struct collection *open_configs = NULL;

static char *config_lua_buffer = NULL;

/* TODO: use that and parse manualy instead of fgets() */
char *config_load_file(const char *filename, unsigned int *length)
{
	char *buffer;
	FILE *fd;
	unsigned int fsize;

	fd = fopen(filename, "rb");
	if(fd == NULL) {
		/* file not found ? */
		return NULL;
	}

	fseek(fd, 0, SEEK_END);
	fsize = ftell(fd);

	if((int)fsize <= 0) {
		/* file is empry ? */
		fclose(fd);
		return NULL;
	}

	buffer = malloc(fsize);
	if(!buffer) {
		CONFIG_DBG("Memory error: %u", fsize);
		fclose(fd);
		return NULL;
	}

	fseek(fd, 0, SEEK_SET);
	if(fread(buffer, fsize, 1, fd) != 1) {
		CONFIG_DBG("fread error: %u", fsize);
		free(buffer);
		fclose(fd);
		return NULL;
	}

	if(length)
		*length = fsize;

	fclose(fd);

	return buffer;
}

char *config_lua_load_file(const char *filename, unsigned int *length) {

	if(config_lua_buffer) {
		free(config_lua_buffer);
		config_lua_buffer = NULL;
	}

	config_lua_buffer = config_load_file(filename, length);

	return config_lua_buffer;
}

/* return the parameter's value from the ini, or the default value on error */
char *config_raw_read(const char *filename, const char *param, const char *default_value) {
	char buffer[1024], *ptr;
	unsigned int i;
	FILE *f;

	if(!filename || !param) {
		CONFIG_DBG("Params error");
		return NULL;
	}
	if(!strlen(param)) {
		CONFIG_DBG("Params error");
		return NULL;
	}

	/* open the file */
	f = fopen(filename, "r");
	if(!f) return default_value ? strdup(default_value) : NULL;

	/* search the parameter */
	while(!feof(f)) {

		/* read a line */
		if(!fgets(buffer, sizeof(buffer)-1, f)) break;

		/* avoid comments */
		if(buffer[0] == '#') continue;

		for(i=0;i<strlen(buffer);i++) {
			if(buffer[i] == '\r') buffer[i] = 0;
			if(buffer[i] == '\n') buffer[i] = 0;
		}

		/* parse the line */
		if(!strnicmp(buffer, param, strlen(param))) {
			ptr = &buffer[strlen(param)];

			/* trim white spaces before the equal sign */
			while(*ptr == ' ') ptr++;
			if(*ptr != '=') {
				/* invalid line */
				continue;
			}
			else ptr++;

			/* trim white spaces after the equal sign */
			while(*ptr == ' ') ptr++;

			/* no value? return the default one */
			if(!strlen(ptr)) break;

			/* there we are, now we return the value */
			fclose(f);

			return strdup(ptr);
		}
	}

	fclose(f);

	return default_value ? strdup(default_value) : NULL;
}

char *config_raw_lua_read(const char *filename, const char *param, const char *default_value) {

	if(config_lua_buffer) {
		free(config_lua_buffer);
		config_lua_buffer = NULL;
	}

	config_lua_buffer = config_raw_read(filename, param, default_value);

	return config_lua_buffer;
}

unsigned long long int config_raw_read_int(const char *filename, const char *param, unsigned long long int default_value) {
	unsigned long long int i;
	char *value;

	if(!filename || !param) {
		CONFIG_DBG("Params error");
		return 0;
	}
	if(!strlen(param)) {
		CONFIG_DBG("Params error");
		return 0;
	}

	/* try to read */
	value = config_raw_read(filename, param, NULL);
	if(!value) return default_value;

	/* sanity check */
	for(i=0;i<strlen(value);i++) {
		if(value[i] < '0' || value[i] > '9') {
			free(value);
			return default_value;
		}
	}

	/* translate the number */
	i = _atoi64(value);
	free(value);

	return i;
}

/* write the parameter. if new_value is NULL the field is removed */
int config_raw_write(const char *filename, const char *param, const char *new_value) {
	char buffer[1024];
	unsigned int i, replaced = 0;
	FILE *f;
	char *filename_tmp;

	if(!filename || !param) {
		CONFIG_DBG("Params error");
		return 0;
	}
	if(!strlen(param)) {
		CONFIG_DBG("Params error");
		return 0;
	}

	filename_tmp = malloc(strlen(filename) + 4 + 1);
	if(!filename_tmp) {
		CONFIG_DBG("Memory error");
		return 0;
	}
	sprintf(filename_tmp, "%s.tmp", filename);

	/* open the file */
	f = fopen(filename, "r");
	if(!f) {
		/* file does not exist ? */
		if(new_value) {
			logging_write(filename, "%s = %s\n", param, new_value);
		}
		free(filename_tmp);
		return 0;
	}

	/* remove the file if it exists */
	remove(filename_tmp);

	/* search the parameter */
	while(!feof(f)) {

		/* read a line */
		if(!fgets(buffer, sizeof(buffer)-1, f)) break;

		/* avoid comments */
		if(buffer[0] == '#') continue;

		for(i=0;i<strlen(buffer);i++) {
			if(buffer[i] == '\r') buffer[i] = 0;
			if(buffer[i] == '\n') buffer[i] = 0;
		}

		/* parse the line */
		if(!strnicmp(buffer, param, strlen(param)) && ((buffer[strlen(param)] == ' ') || (buffer[strlen(param)] == '='))) {

			replaced = 1;

			/* got it, we just write our new value now */
			if(new_value) {
				logging_write(filename_tmp, "%s = %s\n", param, new_value);
			}

		} else {
			/* write the line  */
			logging_write(filename_tmp, "%s\n", buffer);

		}
	}

	if(!replaced && new_value) {
		logging_write(filename_tmp, "%s = %s\n", param, new_value);
	}

	fclose(f);

	/* replace the old file */
	remove(filename);
	rename(filename_tmp, filename);

	free(filename_tmp);

	return 1;
}

/* return the parameter's value from the ini, or the default value on error */
int config_raw_write_int(const char *filename, const char *param, unsigned long long int new_value) {
	char buffer[1024];
	unsigned int i, replaced = 0;
	FILE *f;
	char *filename_tmp;

	if(!filename || !param) {
		CONFIG_DBG("Params error");
		return 0;
	}
	if(!strlen(param)) {
		CONFIG_DBG("Params error");
		return 0;
	}

	filename_tmp = malloc(strlen(filename) + 4 + 1);
	if(!filename_tmp) {
		CONFIG_DBG("Memory error");
		return 0;
	}
	sprintf(filename_tmp, "%s.tmp", filename);

	/* open the file */
	f = fopen(filename, "r");
	if(!f) {
		/* file does not exist ? */
		logging_write(filename, "%s = %I64u\n", param, new_value);
		free(filename_tmp);
		return 0;
	}

	/* remove the file if it exists */
	remove(filename_tmp);

	/* search the parameter */
	while(!feof(f)) {

		/* read a line */
		if(!fgets(buffer, sizeof(buffer)-1, f)) break;

		/* avoid comments */
		if(buffer[0] == '#') continue;

		for(i=0;i<strlen(buffer);i++) {
			if(buffer[i] == '\r') buffer[i] = 0;
			if(buffer[i] == '\n') buffer[i] = 0;
		}

		/* parse the line */
		if(!strnicmp(buffer, param, strlen(param)) && ((buffer[strlen(param)] == ' ') || (buffer[strlen(param)] == '='))) {

			replaced = 1;

			/* got it, we just write our new value now */
			logging_write(filename_tmp, "%s = %I64u\n", param, new_value);

		} else {
			logging_write(filename_tmp, "%s\n", buffer);

		}
	}

	if(!replaced)
		logging_write(filename_tmp, "%s = %I64u\n", param, new_value);

	fclose(f);

	/* replace the old file */
	remove(filename);
	rename(filename_tmp, filename);

	free(filename_tmp);

	return 1;
}

static void config_obj_destroy(struct config_file *config) {
	
	collectible_destroy(config);

	collection_destroy(config->fields);
	if(config->filename) {
		free(config->filename);
	}
	free(config);
	
	return;
}


/* Initialize the config structure without loading the file */
struct config_file *config_new(const char *filename, unsigned long long int timeout) {
	struct config_file *config;
	
	/*if(!filename) {
		CONFIG_DBG("Params error");
		return 0;
	}*/

	if(!open_configs) {
		open_configs = collection_new(C_CASCADE);
	}

	config = malloc(sizeof(struct config_file));
	if(!config) {
		CONFIG_DBG("Memory error");
		return NULL;
	}
	
	obj_init(&config->o, config, (obj_f)config_obj_destroy);
	collectible_init(config);
	
	if(filename) {
		config->filename = strdup(filename);
		if(!config->filename) {
			CONFIG_DBG("Memory error");
			free(config);
			return NULL;
		}
	} else {
		config->filename = NULL;
	}

	config->timeout = timeout;
	config->savetime = time_now();

	config->modified = 0;
	config->loaded = 0;

	config->fields = collection_new(C_CASCADE);

	if(!collection_add(open_configs, config)) {
		if(config->filename) {
			free(config->filename);
		}
		collection_destroy(config->fields);
		free(config);
		return NULL;
	}

	return config;
}

/* Destroy a config structure, discarding any unsaved changes */
void config_destroy(struct config_file *config) {

	if(!config) {
		CONFIG_DBG("Params error");
		return;
	}

	obj_destroy(&config->o);

	return;
}

static int config_get_field_matcher(struct collection *c, struct config_field *field, void *param) {
	struct {
		const char *name;
		unsigned int namesum;
	} *ctx = param;

	if(field->namesum == ctx->namesum) {
		return !stricmp(ctx->name, field->name);
	}

	return 0;
}

static struct config_field *config_get_field(struct config_file *config, const char *name, unsigned int namesum) {
	struct {
		const char *name;
		unsigned int namesum;
	} ctx = { name, namesum };

	return collection_match(config->fields, (collection_f)config_get_field_matcher, &ctx);
}

static void config_field_destroy(struct config_field *field) {
	
	obj_destroy(&field->o);
	
	return;
}

static void config_field_obj_destroy(struct config_field *field) {
	
	collectible_destroy(field);
	
	free(field->name);
	field->name = NULL;
	
	free(field->value);
	field->value = NULL;
	
	return;
}

static struct config_field *config_new_field(struct config_file *config, const char *name, unsigned int namesum, const char *value) {
	struct config_field *field;

	field = malloc(sizeof(struct config_field));
	if(!field) {
		CONFIG_DBG("Memory error");
		return NULL;
	}
	
	obj_init(&field->o, field, (obj_f)config_field_obj_destroy);
	collectible_init(field);

	field->modified = 0;
	field->namesum = namesum;
	
	field->name = strdup(name);
	if(!field->name) {
		CONFIG_DBG("Memory error");
		free(field);
		return NULL;
	}

	field->value = strdup(value);
	if(!field->value) {
		CONFIG_DBG("Memory error");
		free(field->name);
		free(field);
		return NULL;
	}

	if(!collection_add(config->fields, field)) {
		CONFIG_DBG("Collection error");
		free(field->value);
		free(field->name);
		free(field);
		return NULL;
	}

	return field;
}
#define IS_MAJ(_c) (((_c) >= 'A') && ((_c) <= 'Z'))

/*
	The name sum is calculated without the ending zero for more
	flexibility (less string movement)
*/
unsigned int config_namesum(const char *name, unsigned int length) {
	unsigned int i;
	unsigned int sum;
	unsigned char m;

	crc32_init(&sum);
	for(i=0;i<length;i++) {
		if(IS_MAJ(name[i])) {
			m = (name[i] - 'A') + 'a';
			crc32_add(&sum, (void *)&m, 1);
		} else {
			crc32_add(&sum, (void *)&name[i], 1);
		}
	}
	crc32_close(&sum);

	return sum;
}

#undef IS_MAJ

/* Force a config file to be loaded to memory */
int config_load(struct config_file *config) {
	char buffer[1024], *ptr, *equal;
	unsigned int i;
	unsigned int namesum;
	FILE *f;
	
	if(!config) {
		CONFIG_DBG("Params error");
		return 0;
	}

	if(!config->filename) {
		/* not backed up on disk - can't load */
		return 1;
	}

	if(config->loaded) {
		CONFIG_DBG("Already loaded");
		return 0;
	}

	/* open the file */
	f = fopen(config->filename, "r");
	if(!f) {
		/* if we cannot open the file, it's no error because it doesn't have to exist. */
		config->loaded = 1;
		return 0;
	}

	/* search the parameter */
	while(!feof(f)) {

		/* read a line */
		if(!fgets(buffer, sizeof(buffer)-1, f)) break;

		/* avoid comments */
		if(buffer[0] == '#') continue;

		for(i=0;i<strlen(buffer);i++) {
			if(buffer[i] == '\r') buffer[i] = 0;
			if(buffer[i] == '\n') buffer[i] = 0;
		}

		equal = strchr(buffer, '=');
		if(!equal) continue;
		ptr = equal;
		while(*(ptr-1) == ' ' && ((ptr-1) >= buffer)) ptr--;
		*ptr = 0;
		
		ptr = equal+1;
		while(*ptr == ' ' && *ptr) ptr++;

		// buffer is the name
		// ptr is the value

		namesum = config_namesum(buffer, strlen(buffer));

		config_new_field(config, buffer, namesum, ptr);
	}

	fclose(f);
	config->loaded = 1;

	return 1;
}

static int config_save_modified(struct collection *c, struct config_field *field, struct config_file *config) {

	if(field->modified) {
		if(field->value) {
			config_raw_write(config->filename, field->name, field->value);
		} else {
			config_field_destroy(field);
		}
		field->modified = 0;
	}

	return 1;
}

/* Force any changes in a config file to be saved to disk */
int config_save(struct config_file *config) {

	if(!config) {
		CONFIG_DBG("Params error");
		return 0;
	}

	if(!config->filename) {
		/* not backed up on disk - can't save */
		return 0;
	}
	
	if(!config->loaded) {
		CONFIG_DBG("Config not loaded");
		return 0;
	}
	
	if(!config->modified) {
		CONFIG_DBG("Config not modified");
		return 0;
	}

	collection_iterate(config->fields, (collection_f)config_save_modified, config);
	config->modified = 0;
	config->savetime = time_now();

	return 1;
}

static int config_poll_save_modified(struct collection *c, struct config_file *config, void *param) {

	if(config->filename && config->loaded && config->modified && (timer(config->savetime) > config->timeout)) {
		config_save(config);
	}

	return 1;
}

/* Check for timeouts and save files to disk if needed */
int config_poll() {
	/*unsigned long long int time;

	time = time_now();*/

	collection_iterate(open_configs, (collection_f)config_poll_save_modified, NULL);

	/*time = timer(time);

	if(time) {
		CONFIG_DBG("Saved all configs in %I64u ms", time);
	}*/

	return 1;
}

/*
	Read/write access to config files. If the file is not loaded,
	it will be implicitly loaded by any of those functions.

	Integer operations are simply wrapped around thier srting
	equivalents.
*/

/* Return a copy of the value that must be freed by the caller. */
char *config_read(struct config_file *config, const char *param, const char *default_value) {
	struct config_field *field;
	unsigned int namesum;
	
	if(!config || !param) {
		CONFIG_DBG("Params error");
		return NULL;
	}
	
	if(!config->loaded && config->filename) {
		config_load(config);
	}

	namesum = config_namesum(param, strlen(param));
	field = config_get_field(config, param, namesum);
	if(!field) {
		/* not found ... */
		return default_value ? strdup(default_value) : NULL;
	}

	/* field->value may be NULL if the field was to be deleted ... */
	return field->value ? strdup(field->value) : (default_value ? strdup(default_value) : NULL);
}

char *config_lua_read(struct config_file *config, const char *param, const char *default_value) {

	if(config_lua_buffer) {
		free(config_lua_buffer);
		config_lua_buffer = NULL;
	}

	config_lua_buffer = config_read(config, param, default_value);

	return config_lua_buffer;
}

unsigned long long int config_read_int(struct config_file *config, char *param, unsigned long long int default_value) {
	unsigned long long int n;
	char *s;

	if(!config || !param) {
		CONFIG_DBG("Params error");
		return 0;
	}
	
	if(!config->loaded && config->filename) {
		config_load(config);
	}

	s = config_read(config, param, NULL);
	if(!s) {
		/* not found ... */
		return default_value;
	}

	n = _atoi64(s);
	free(s);

	return n;
}

int config_write(struct config_file *config, const char *param, const char *new_value) {
	struct config_field *field;
	unsigned int namesum;

	if(!config || !param) {
		CONFIG_DBG("Params error");
		return 0;
	}
	
	if(!config->loaded && config->filename) {
		config_load(config);
	}

	namesum = config_namesum(param, strlen(param));
	field = config_get_field(config, param, namesum);
	if(field) {
		if(field->value) {
			free(field->value);
			field->value = NULL;

			field->modified = 1;
			config->modified = 1;
		}

		if(new_value) {
			field->value = strdup(new_value);
			if(!field->value) {
				CONFIG_DBG("Memory error");
				return 0;
			}
			field->modified = 1;
			config->modified = 1;
		}

		return 1;
	}

	if(!new_value) {
		/* no new value means the value is to be deleted anyway */
		return 1;
	}

	field = config_new_field(config, param, namesum, new_value);
	if(!field) {
		CONFIG_DBG("Memory error");
		return 0;
	}
	field->modified = 1;
	config->modified = 1;

	return 1;
}

int config_write_int(struct config_file *config, const char *param, unsigned long long int new_value) {
	char *s;

	if(!config || !param) {
		CONFIG_DBG("Params error");
		return 0;
	}
	
	if(!config->loaded && config->filename) {
		config_load(config);
	}

	s = bprintf("%I64u", new_value);
	if(!s) {
		CONFIG_DBG("Memory error");
		return 0;
	}

	if(!config_write(config, param, s)) {
		free(s);
		return 0;
	}

	free(s);
	return 1;
}
