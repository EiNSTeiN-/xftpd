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

#include "collection.h"
#include "tree.h"

static struct branch *find_branch(struct collection *branches, char *trigger, unsigned int any, char **args);

static unsigned int find_branch_callback(struct collection *c, void *item, void *param) {
	struct {
		char *trigger;
		unsigned int any;
		struct branch *b;
		char *args;
	} *ctx = param;
	struct branch *b = item;
	char *ptr, *args;

	if(strchr(ctx->trigger, ' ')) {
		ptr = strdup(ctx->trigger);
		if(!ptr) {
			TREE_DBG("Memory error");
			return 0;
		}

		args = strchr(ptr, ' ');
		*args = 0;
		args++;

		if(!stricmp(b->name, ptr)) {

			/* continue searching the sub-levels */
			ctx->b = find_branch(b->branches, args, ctx->any, &ctx->args);
			if(!ctx->b && ctx->any) {
				/*
					no upper level match the query and
					we're searching for 'anything' so
					return this level with the remaining
					arguments.
				*/
				ctx->b = b;
				ctx->args = strdup(args);
			}

			free(ptr);
			return 0;
		}
	} else {
		/* we've reached the top level. */
		if(!stricmp(b->name, ctx->trigger)) {
			ctx->b = b;
			return 0;
		}
	}
	
	return 1;
}

/* search a hook in a collection and return it */
static struct branch *find_branch(struct collection *branches, char *trigger, unsigned int any, char **args) {
	struct {
		char *trigger;
		unsigned int any;
		struct branch *b;
		char *args;
	} ctx = { trigger, any, NULL, NULL };

	collection_iterate(branches, find_branch_callback, &ctx);

	if(args)
		*args = ctx.args;
	else
		free(ctx.args);

	return ctx.b;
}

static unsigned int handler_exist_callback(struct collection *c, void *item, void *param) {
	struct {
		unsigned int success;
		char *handler;
	} *ctx = param;
	char *handler = item;

	if(!stricmp(handler, ctx->handler)) {
		ctx->success = 1;
		return 0;
	}

	return 1;
}

static unsigned int handler_exist(struct collection *branches, char *handler) {
	struct {
		unsigned int success;
		char *handler;
	} ctx = { 0, handler };

	collection_iterate(branches, handler_exist_callback, &ctx);

	return ctx.success;
}

/* add a hook to the collection */
unsigned int tree_add(struct collection *branches, char *trigger, char *handler) {
	struct branch *b;
	char *ptr;

	if(!branches) {
		TREE_DBG("Parameter error: branches");
		return 0;
	}

	if(!trigger) {
		TREE_DBG("Parameter error: trigger");
		return 0;
	}

	if(!handler) {
		TREE_DBG("Parameter error: handler");
		return 0;
	}

	/* select the next argument if any */
	ptr = strchr(trigger, ' ');
	if(ptr) {
		*ptr = 0;
		ptr++;
	}

	/* try to find 'trigger' in 'branches' collection */
	b = find_branch(branches, trigger, 0, NULL);
	if(!b) {
		/* the branch does not exist */
		b = malloc(sizeof(struct branch));
		if(!b) {
			TREE_DBG("Memory error");
			return 0;
		}

		b->name = strdup(trigger);
		if(!b->name) {
			TREE_DBG("Memory error");
			free(b);
			return 0;
		}

		b->branches = collection_new();
		b->handlers = collection_new();
		collection_add(branches, b);

		//printf("[branch] created %s\n", b->name);
		TREE_DBG("Created branch %s", b->name);
	}


	if(!ptr) {
		/* add the handler to that branch */
		/* ensure that the function is not already added */
		if(!handler_exist(b->handlers, handler)) {
			collection_add(b->handlers, handler);
		}

		//printf("[branch] handler added to %s\n", b->name);
		TREE_DBG("added handler \"%s\" to branch %s", handler, b->name);
	} else {
		return tree_add(b->branches, ptr, handler);
	}

	return 1;
}

/* return a collection of all handlers available
	for that trigger, wich SHOULD NOT BE KEPT across
	calls */
struct collection *tree_get(struct collection *branches, char *trigger, char **args) {
	struct branch *b;

	if(!branches || !trigger) return 0;

	b = find_branch(branches, trigger, 1, args);
	if(!b) return NULL;

	return b->handlers;
}

static unsigned int tree_destroy_free_handlers(struct collection *c, void *item, void *param) {
	struct branch *b = param;

	collection_delete(b->handlers, item);
	free(item);

	return 1;
}

static unsigned int tree_destroy_callback(struct collection *c, struct branch *b, void *param) {

	TREE_DBG("Deleting branch %s", b->name);

	free(b->name);

	collection_iterate(b->handlers, tree_destroy_free_handlers, b);
	collection_destroy(b->handlers);

	collection_iterate(b->branches, (collection_f)tree_destroy_callback, b->branches);
	collection_destroy(b->branches);

	collection_delete(c, b);

	return 1;
}

/* destroy all branches of the collection */
unsigned int tree_destroy(struct collection *branches) {
	
	collection_iterate(branches, (collection_f)tree_destroy_callback, NULL);

	return 1;
}

