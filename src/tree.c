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

static int find_branch_callback(struct collection *c, struct branch *b, void *param) {
	struct {
		char *trigger;
		unsigned int any;
		struct branch *b;
		char *args;
	} *ctx = param;
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

	collection_iterate(branches, (collection_f)find_branch_callback, &ctx);

	if(args) {
		*args = ctx.args;
	} else {
		free(ctx.args);
	}

	return ctx.b;
}

static int handler_exist_matcher(struct collection *c, void *item, void *param) {
	struct {
		struct collectible *cb;
		int (*cmp)(void *a, void *b);
	} *ctx = param;

	return !(*ctx->cmp)(item, obj_self(ctx->cb->self));
}

static unsigned int handler_exist(struct collection *handlers, struct collectible *cb, int (*cmp)(void *a, void *b)) {
	struct {
		struct collectible *cb;
		int (*cmp)(void *a, void *b);
	} ctx = { cb, cmp };

	return (collection_match(handlers, (collection_f)handler_exist_matcher, &ctx) != NULL);
}

static void branch_obj_destroy(struct branch *b) {
	
	collectible_destroy(b);
	
	TREE_DBG("Branch destruction (%s)", b->name);

	if(b->branches) {
		collection_destroy(b->branches);
		b->branches = NULL;
	}

	if(b->handlers) {
		collection_destroy(b->handlers);
		b->handlers = NULL;
	}

	free(b->name);
	b->name = NULL;
	
	free(b);

	return;
}

/* add a hook to the collection */
unsigned int tree_add(struct collection *branches, char *trigger, struct collectible *cb, int (*cmp)(void *a, void *b)) {
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

	if(!cb) {
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
		
		obj_init(&b->o, b, (obj_f)branch_obj_destroy);
		collectible_init(b);

		b->name = strdup(trigger);
		if(!b->name) {
			TREE_DBG("Memory error");
			free(b);
			return 0;
		}

		b->branches = collection_new(C_CASCADE);
		b->handlers = collection_new(C_CASCADE);
		collection_add(branches, b);

		//printf("[branch] created %s\n", b->name);
		TREE_DBG("Created branch %s", b->name);
	}

	if(!ptr) {
		/* add the handler to that branch */
		/* ensure that the function is not already added */
		if(!handler_exist(b->handlers, cb, cmp)) {
			collection_c_add(b->handlers, cb);
		}

		//printf("[branch] handler added to %s\n", b->name);
		TREE_DBG("added handler to branch %s", b->name);
	} else {
		return tree_add(b->branches, ptr, cb, cmp);
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

/* destroy all branches of the collection */
unsigned int tree_destroy(struct collection *branches) {
	
	collection_empty(branches);

	return 1;
}

