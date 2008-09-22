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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "collection.h"
#include "time.h"
#include "signal.h"
#include "obj.h"

/*
	On any error with memory allocation, the program will shut down.
*/

//struct collection *signals = NULL;

static unsigned int signal_get_matcher(struct collection *c, struct signal_ctx *ctx, char *name) {
	return !stricmp(ctx->name, name);
}

struct signal_ctx *signal_get(struct collection *signals, const char *name, int create) {
	struct signal_ctx *ctx = NULL;

	if(!signals || !name) {
		SIGNAL_DBG("Params error");
		return NULL;
	}

	ctx = collection_match(signals, (collection_f)signal_get_matcher, (void *)name);

	if(!ctx && create) {
		ctx = malloc(sizeof(struct signal_ctx));
		if(!ctx) {
			SIGNAL_DBG("Memory error");
			exit(1);
		}

		ctx->signals = signals;
		ctx->name = strdup(name);
		if(!ctx->name) {
			SIGNAL_DBG("Memory error");
			free(ctx);
			exit(1);
		}

		ctx->ref = 0;
		ctx->callbacks = collection_new();
		collection_add(signals, ctx);

		SIGNAL_DBG("signal \"%s\" created at %08x.", name, signals);
	}

	return ctx;
}

static void signal_callback_obj_destroy(struct signal_callback *s) {

	free(s);

	return;
}

struct signal_callback *signal_add(struct collection *signals, struct collection *owner, char *name, int (*callback)(void *obj, void *param), void *param) {
	struct signal_callback *s;
	struct signal_ctx *ctx;

	if(!signals || !owner || !name || !callback) {
		SIGNAL_DBG("Params error");
		return NULL;
	}

	ctx = signal_get(signals, name, 1);
	if(!ctx) {
		SIGNAL_DBG("Memory error");
		return NULL;
	}

	s = malloc(sizeof(struct signal_callback));
	if(!s) {
		SIGNAL_DBG("Memory error");
		exit(1);
	}

	obj_init(&s->o, s, (obj_f)signal_callback_obj_destroy);
	s->ctx = ctx;
	s->filter = 0;
	s->callback = callback;
	s->param = param;
	s->timeout_callback = NULL;

	if(!collection_add(owner, s)) {
		SIGNAL_DBG("Collection error");
		free(s);
		return NULL;
	}
	if(!collection_add(ctx->callbacks, s)) {
		SIGNAL_DBG("Collection error");
		collection_delete(owner, s);
		free(s);
		return NULL;
	}

	SIGNAL_DBG("callback %08x added to signal \"%s\".", (int)s, name);

	return s;
}

int signal_filter(struct signal_callback *s, void *obj) {

	if(!s)
		return 0;

	s->filter = 1;
	s->obj = obj;

	return 1;
}

int signal_timeout(struct signal_callback *s, unsigned int timeout, int (*callback)(void *param), void *param) {

	if(!s)
		return 0;
	
	SIGNAL_DBG("timeout of %u(s) added on callback %08x of signal \"%s\".", timeout, (int)s, s->ctx->name);

	s->timeout_callback = callback;
	s->timeout_param = param;
	
	s->timestamp = 0;
	s->timeout = timeout;

	return 1;
}

int signal_ref(struct signal_ctx *signal) {

	if(!signal)
		return 0;

	signal->ref++;

	return 1;
}

int signal_unref(struct signal_ctx *signal) {

	if(!signal)
		return 0;

	signal->ref--;

	if(!collection_size(signal->callbacks) && !signal->ref) {
		
		SIGNAL_DBG("signal \"%s\" destroyed from %08x.", signal->name, signal->signals);

		free(signal->name);
		signal->name = NULL;
		collection_destroy(signal->callbacks);
		signal->callbacks = NULL;
		collection_delete(signal->signals, signal);
		signal->signals = NULL;
		free(signal);
	}

	return 1;
}

int signal_del(struct collection *owner, struct signal_callback *s) {
	struct signal_ctx *ctx;

	if(!owner || !s) {
		SIGNAL_DBG("Params error");
		return 0;
	}

	SIGNAL_DBG("callback %08x deleted from signal \"%s\".", (int)s, s->ctx->name);

	collection_delete(owner, s);
	collection_delete(s->ctx->callbacks, s);
	ctx = s->ctx;
	s->ctx = NULL;

	/* the actual freeing if the structure will be done thru the following call */
	obj_destroy(&s->o);
	s = NULL;

	if(!collection_size(ctx->callbacks) && !ctx->ref) {
		
		SIGNAL_DBG("signal \"%s\" destroyed from %08x.", ctx->name, ctx->signals);

		free(ctx->name);
		ctx->name = NULL;
		collection_destroy(ctx->callbacks);
		ctx->callbacks = NULL;
		collection_delete(ctx->signals, ctx);
		ctx->signals = NULL;
		free(ctx);
	}

	return 1;
}

static unsigned int signal_clear_with_filter_iterator(struct collection *group, struct signal_callback *s, void *param) {
	struct  {
		char *name;
		void *obj;
	} *ctx = param;

	if(s->filter && (s->obj == ctx->obj) && !stricmp(s->ctx->name, ctx->name)) {
		signal_del(group, s);
	}

	return 1;
}

int signal_clear_with_filter(struct collection *group, char *name, void *obj) {
	struct  {
		char *name;
		void *obj;
	} ctx = { name, obj };
	
	if(!group)
		return 0;

	SIGNAL_DBG("deleting callbacks for signal \"%s\" with filter obj == %08x.", name, (int)obj);

	collection_iterate(group, (collection_f)signal_clear_with_filter_iterator, &ctx);

	return 1;
}

static unsigned int signal_clear_all_with_filter_iterator(struct collection *group, struct signal_callback *s, void *obj) {

	if(s->filter && (s->obj == obj)) {
		signal_del(group, s);
	}

	return 1;
}

int signal_clear_all_with_filter(struct collection *group, void *obj) {

	if(!group)
		return 0;

	SIGNAL_DBG("deleting all callbacks with filter obj == %08x.", (int)obj);

	collection_iterate(group, (collection_f)signal_clear_all_with_filter_iterator, obj);

	return 1;
}

int signal_clear(struct collection *group) {
	
	if(!group)
		return 0;

	while(collection_size(group)) {
		void *first = collection_first(group);
		if(!signal_del(group, first)) {
			SIGNAL_DBG("ERROR!!! Could not delete signal!");
			collection_delete(group, first);
		}
	}

	return 1;
}

static unsigned int signal_raise_iterator(struct collection *c, struct signal_callback *s, void *obj) {

	if(!s->filter || (s->obj == obj)) {

		obj_ref(&s->o);

		(*s->callback)(obj, s->param);
		s->timestamp = time_now();

		obj_unref(&s->o);
	}

	return 1;
}

int signal_raise(struct signal_ctx *signal, void *obj) {

	if(!signal)
		return 0;
	
	//SIGNAL_DBG("signal \"%s\" raised with obj == %08x.", name, (int)obj);

	collection_iterate(signal->callbacks, (collection_f)signal_raise_iterator, obj);

	return 1;
}

static unsigned int signal_timeout_callbacks_iterator(struct collection *c, struct signal_callback *s, void *param) {

	obj_ref(&s->o);

	if(s->timeout_callback) {
		if(!s->timestamp)
			/* first pass, we just set the timestamp to 'now'. */
			s->timestamp = time_now();
		else {
			if(timer(s->timestamp) > s->timeout) {
				/*
					call the timeout callback, then set the
					timestamp to 'now' so we don't call again
					on the next pass.
				*/
				SIGNAL_DBG("timeout of %u(s) reached on callback %08x of signal \"%s\".", s->timeout, (int)s, s->ctx->name);

				(*s->timeout_callback)(s->timeout_param);
				s->timestamp = time_now();
			}
		}
	}
				
	obj_unref(&s->o);

	return 1;
}

static unsigned int signal_timeout_signals_iterator(struct collection *c, struct signal_ctx *s, void *param) {

	collection_iterate(s->callbacks, (collection_f)signal_timeout_callbacks_iterator, NULL);

	return 1;
}

int signal_poll(struct collection *signals) {

	if(!signals)
		return 0;

	collection_iterate(signals, (collection_f)signal_timeout_signals_iterator, NULL);

	return 1;
}
