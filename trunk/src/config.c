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
#include <io.h>
#else
#include <stdlib.h>
#include <string.h>
#endif

#include <stdio.h>

#include "config.h"
#include "main.h"
#include "logging.h"
#include "collection.h"
#include "time.h"
#include "crc32.h"
#include "asprintf.h"
#include "adio.h"

struct collection *open_configs = NULL;

/* TODO: use that and parse manualy instead of fgets() in functions below */
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
		if(!strncasecmp(buffer, param, strlen(param))) {
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
		if(!strncasecmp(buffer, param, strlen(param)) && ((buffer[strlen(param)] == ' ') || (buffer[strlen(param)] == '='))) {

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
		if(!strncasecmp(buffer, param, strlen(param)) && ((buffer[strlen(param)] == ' ') || (buffer[strlen(param)] == '='))) {

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

/* called upon destruction of a config_file structure */
static void config_obj_destroy(struct config_file *config) {
	
	collectible_destroy(config);

	collection_destroy(config->fields);
	if(config->filename) {
		free(config->filename);
	}
	free(config);
	
	return;
}

/* Destroy a config structure, discarding any unsaved changes */
void config_close(struct config_file *config) {
	
	if(!config) {
		CONFIG_DBG("Params error");
		return;
	}
	
	config->refs--;
	if(config->refs)
		return;
	else {
		/* save the file if needed. */
		if(config->shouldsave) {
			config_save(config);
		} else {
			obj_destroy(&config->o);
		}
	}
	
	return;
}

static int config_get_field_matcher(struct collection *c, struct config_field *field, void *param) {
	struct {
		const char *name;
		unsigned int namesum;
	} *ctx = param;

	if(field->namesum == ctx->namesum) {
		return !strcasecmp(ctx->name, field->name);
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

	field->comments = NULL;
	
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

/*
	The name sum is calculated without the ending zero for more
	flexibility (less string movement)
*/
unsigned int config_namesum(const char *name, unsigned int length) {
#define IS_MAJ(_c) (((_c) >= 'A') && ((_c) <= 'Z'))
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
#undef IS_MAJ
}

/* Load all fields of the specified config file. */
int config_load(struct config_file *config) {
	struct config_field *field;
	char *line, *value, *tmp;
	unsigned int i;
	unsigned int namesum;
	char *buffer;
	unsigned int length;
	char *comment = NULL;
	
	if(!config) {
		CONFIG_DBG("Params error");
		return 0;
	}
	
	if(!config->filename) {
		// not backed up on disk - can't load
		return 1;
	}
	
	buffer = config_load_file(config->filename, &length);
	if(!buffer) {
		/* The file could not be loaded!
			Maybe it does not yet exist? */
		CONFIG_DBG("Could not load config file \"%s\", maybe the file does not exist?", config->filename);
		return 0;
	}
	
	CONFIG_DBG("Loading file \"%s\"", config->filename);

	// search the parameter
	i = 0;
	while(i < length) {
		if(buffer[i] == '#') {
			//unsigned int j;
			/* this is a comment, and we need to
				check where it stops and copy it to the next field we get. */
			comment = &buffer[i];
			for(;i<length;i++) {
				if(((buffer[i] == '\r') || (buffer[i] == '\n')) && ((buffer[i+1] != '\r') && (buffer[i+1] != '\n') && (buffer[i+1] != '#'))) {
					buffer[i++] = 0;
					break;
				}
			}
			
			CONFIG_DBG("New comment \"%s\"", comment);
			
			// continue on with the next line!
		}
		
		line = &buffer[i];
		
		for(;i<length;i++) {
			if((buffer[i] == '\r') || (buffer[i] == '\n')) {
				buffer[i++] = 0;
				break;
			}
		}
		
		if(!line[0]) continue; // empty line, go on...
		
		//CONFIG_DBG("Loading line: %s", line);
		
		value = strchr(line, '=');
		if(!value) continue; // no equal sign on the line
		
		/* erase the white spaces after the name */
		tmp = value;
		while(((tmp-1) >= buffer) && (*(tmp-1) == ' ')) tmp--;
		*tmp = 0;
		
		/* erase the white spaces after the equal sign */
		value++;
		while(*value == ' ' && *value) value++;
		
		//CONFIG_DBG("Loading field \"%s\" = \"%s\" at %u", line, value, (line-buffer));
		
		namesum = config_namesum(line, strlen(line));
		field = config_new_field(config, line, namesum, value);
		
		if(comment) {
			field->comments = strdup(comment);
			comment = NULL;
		}
	}
	
	if(comment) {
		config->comments = strdup(comment);
		comment = NULL;
	}
	
	return 1;
}

static int config_find_matcher(struct collection *c, struct config_file *config, const char *filename) {
	if(!config->filename) return 0;
	return !strcasecmp(config->filename, filename);
}

static struct config_file *config_find(const char *filename) {
	if(!filename)
		return NULL;
	
	return (struct config_file *)collection_match(open_configs, (collection_f)config_find_matcher, (void *)filename);
}


struct config_file *config_volatile() {
	
	return config_open(NULL);
}

/* Initialize the config structure without loading the file */
struct config_file *config_open(const char *filename) {
	struct config_file *config;
	
	if(!open_configs) {
		open_configs = collection_new(C_CASCADE);
	}

	if(filename) {
		config = config_find(filename);
		if(config) {
			config->refs++;
			return config;
		}
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

	config->comments = NULL;
	
	config->adf = NULL;
	config->op = NULL;
	
	config->refs = 1;
	
	config->is_saving = 0;
	config->shouldsave = 0;

	config->savetime = time_now();

	config->fields = collection_new(C_CASCADE);
	
	config_load(config);

	if(!collection_add(open_configs, config)) {
		if(config->filename) free(config->filename);
		collection_destroy(config->fields);
		free(config);
		return NULL;
	}

	return config;
}

static int config_dump_getlength(struct collection *c, struct config_field *field, unsigned int *length) {
	
	*length += (field->comments ? 2 + strlen(field->comments) + 2 : 0) + // crlf + field's comments
				strlen(field->name) +	// field name
				3 +						// space, equal sign, space
				strlen(field->value) +			// field value
				2;						// crlf
	
	return 1;
}

static int config_dump_print(struct collection *c, struct config_field *field, void *param) {
	struct {
		char *buffer;
		unsigned int pos;
	} *ctx = param;
	unsigned int length;
	
	if(field->comments) {
		length = strlen(field->comments);
		ctx->buffer[ctx->pos++] = '\r';
		ctx->buffer[ctx->pos++] = '\n';
		memcpy(&ctx->buffer[ctx->pos], field->comments, length);
		ctx->pos += length;
		ctx->buffer[ctx->pos++] = '\r';
		ctx->buffer[ctx->pos++] = '\n';
	}
	
	length = strlen(field->name);
	memcpy(&ctx->buffer[ctx->pos], field->name, length);
	ctx->pos += length;
	
	ctx->buffer[ctx->pos++] = ' ';
	ctx->buffer[ctx->pos++] = '=';
	ctx->buffer[ctx->pos++] = ' ';
	
	length = strlen(field->value);
	memcpy(&ctx->buffer[ctx->pos], field->value, length);
	ctx->pos += length;
	
	ctx->buffer[ctx->pos++] = '\r';
	ctx->buffer[ctx->pos++] = '\n';
	
	return 1;
}

static char *config_dump(struct config_file *config, unsigned int *length) {
	struct {
		char *buffer;
		unsigned int pos;
	} ctx = { NULL, 0 };
	
	if(!config || !length) {
		CONFIG_DBG("Params error");
		return NULL;
	}
	
	*length = config->comments ? (2 + strlen(config->comments)) : 0;
	collection_iterate(config->fields, (collection_f)config_dump_getlength, length);
	if(!*length) return NULL;
	
	ctx.buffer = malloc(*length);
	if(!ctx.buffer) {
		*length = 0;
		CONFIG_DBG("Memory error");
		return NULL;
	}
	memset(ctx.buffer, 0, *length);
	
	collection_iterate(config->fields, (collection_f)config_dump_print, &ctx);
	if(config->comments) {
		unsigned int comments_length = strlen(config->comments);
		memcpy(&ctx.buffer[ctx.pos], config->comments, comments_length);
		ctx.pos += comments_length;
	}
	
	return ctx.buffer;
}

/* Force any changes in a config file to be saved to disk */
int config_save(struct config_file *config) {
	char *buffer;
	unsigned int length;
	
	if(!config) {
		CONFIG_DBG("Params error");
		return 0;
	}

	if(!config->filename) {
		// not backed up on disk - can't save
		return 0;
	}
	
	if(!config->shouldsave) {
		/* file not modified, don't need to save. */
		//CONFIG_DBG("The file does not need saving: %s", config->filename);
		return 0;
	}
	
	if(config->is_saving) {
		/* the file is already currently being saved.
			We will mark it again so it will be saved next time. */
		//CONFIG_DBG("File is already being saved: %s", config->filename);
		config->shouldsave = 1;
		return 0;
	}
	
	//CONFIG_DBG("SAVING: %s", config->filename);
	
	/* remove the 'should save' flag and set the 'is saving' flag. */
	config->shouldsave = 0;
	config->is_saving = 1;
	
	/* just remove the file if it become empty */
	if(!collection_size(config->fields)) {
		remove(config->filename);
		return 1;
	}
	
	/* build the actual buffer that we will be writing to disk. */
	buffer = config_dump(config, &length);
	if(!buffer) {
		CONFIG_DBG("Memory error");
		return 0;
	}
	
	config->adf = adio_open(config->filename, 1);
	if(!config->adf) {
		config->shouldsave = 1;
		config->is_saving = 0;
		config->savetime = time_now();
		CONFIG_DBG("Warning: %s: could not open the file to save it! Will try again later...", config->filename);
		free(buffer);
		return 0;
	}
	
	config->op = adio_write(config->adf, buffer, 0, length, config->op);
	if(!config->op) {
		config->shouldsave = 1;
		config->is_saving = 0;
		config->savetime = time_now();
		CONFIG_DBG("Warning: %s: could not write to the file! Will try again later...", config->filename);
		adio_close(config->adf);
		config->adf = NULL;
		free(buffer);
		return 0;
	}

	return 1;
}

static int config_poll_save_modified(struct collection *c, struct config_file *config, void *param) {

	if(!config->filename) {
		/* not backed on disk */
		return 1;
	}
	
	if(!config->shouldsave || config->is_saving) {
		/* should not be saved (not modified) */
		return 1;
	}
	
	if(timer(config->savetime) >= CONFIG_SAVETIME) {
		config_save(config);
	}

	return 1;
}

static int config_poll_process_adio(struct collection *c, struct config_file *config, void *param) {

	if(!config->adf || !config->op) {
		/* not currently saving */
		return 1;
	}
	
	//CONFIG_DBG("Probing config file for save: %s", config->filename);
	
	if(adio_probe(config->op, NULL)) {
		
		free(config->op->buffer);
		adio_complete(config->op);
		adio_close(config->adf);
		
		config->op = NULL;
		config->adf = NULL;
		
		config->is_saving = 0;
		
		if(config->shouldsave) {
			/* shouldsave was set again while we were saving. */
			//CONFIG_DBG("The shouldsave flag was set again while we were saving: %s", config->filename);
			return 1;
		}
		
		if(!config->refs) {
			/* no more references. lets close this config file. */
			obj_destroy(&config->o);
		}
	}
	
	return 1;
}

/* Check for timeouts and save files to disk if needed */
int config_poll() {

	collection_iterate(open_configs, (collection_f)config_poll_process_adio, NULL);
	collection_iterate(open_configs, (collection_f)config_poll_save_modified, NULL);

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
	const char *value = config_pread(config, param, default_value);
	return value ? strdup(value) : NULL;
}

const char *config_pread(struct config_file *config, const char *param, const char *default_value) {
	struct config_field *field;
	unsigned int namesum;
	
	if(!config || !param) {
		CONFIG_DBG("Params error");
		return NULL;
	}
	
	namesum = config_namesum(param, strlen(param));
	field = config_get_field(config, param, namesum);
	if(!field) {
		/* not found ... */
		return default_value;
	}

	/* field->value shouldn't be NULL ... */
	return field->value ? field->value : default_value;
}

unsigned long long int config_read_int(struct config_file *config, char *param, unsigned long long int default_value) {
	unsigned long long int n;
	char *s;

	if(!config || !param) {
		CONFIG_DBG("Params error");
		return 0;
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
	
	namesum = config_namesum(param, strlen(param));
	field = config_get_field(config, param, namesum);
	if(field) {
		/* The field is already present in the file */
		
		if(!new_value) {
			/* we need to delete the field from the file */
			config_field_destroy(field);
			config->shouldsave = 1;
			return 1;
		}
		
		if(!strcmp(field->value, new_value)) {
			/* don't waste time saving if both values are the same */
			return 1;
		}
		
		if(field->value) {
			/* clear the current value of the field */
			free(field->value);
			field->value = NULL;
		}
		
		/* write the new value to the field */
		field->value = strdup(new_value);
		if(!field->value) {
			CONFIG_DBG("Memory error");
			return 0;
		}
		
		config->shouldsave = 1;
		
		return 1;
	}
	
	if(!new_value) {
		/* no new value means we're done */
		return 1;
	}
	
	field = config_new_field(config, param, namesum, new_value);
	if(!field) {
		CONFIG_DBG("Memory error");
		return 0;
	}
	
	config->shouldsave = 1;
	
	return 1;
}

int config_write_int(struct config_file *config, const char *param, unsigned long long int new_value) {
	char *s;

	if(!config || !param) {
		CONFIG_DBG("Params error");
		return 0;
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

int config_field_comment(struct config_file *config, const char *param, const char *new_comment) {
	struct config_field *field;
	unsigned int namesum;
	
	if(!config || !param) {
		CONFIG_DBG("Params error");
		return 0;
	}
	
	namesum = config_namesum(param, strlen(param));
	field = config_get_field(config, param, namesum);
	if(!field)
		return 0;
	
	/* The field is already present in the file */
	
	if(field->comments && new_comment && !strcmp(field->comments, new_comment)) {
		/* don't waste time saving if both values are the same */
		return 1;
	}
	
	if(!field->comments && !new_comment)
		return 0;
	
	if(field->comments) {
		/* clear the current value of the field */
		free(field->comments);
		field->comments = NULL;
	}
	
	/* write the new value to the field */
	if(new_comment) {
		field->comments = strdup(new_comment);
		if(!field->comments) {
			CONFIG_DBG("Memory error");
			return 0;
		}
	}
	
	config->shouldsave = 1;
	
	return 1;
}

int config_file_comment(struct config_file *config, const char *new_comment) {
	
	if(!config) {
		CONFIG_DBG("Params error");
		return 0;
	}
	
	if(!strcmp(config->comments, new_comment)) {
		/* don't waste time saving if both values are the same */
		return 1;
	}
	
	if(!config->comments && !new_comment)
		return 0;
	
	if(config->comments) {
		/* clear the current value of the field */
		free(config->comments);
		config->comments = NULL;
	}
	
	/* write the new value to the field */
	if(new_comment) {
		config->comments = strdup(new_comment);
		if(!config->comments) {
			CONFIG_DBG("Memory error");
			return 0;
		}
	}
	
	config->shouldsave = 1;
	
	return 1;
}
