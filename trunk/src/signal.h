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

#ifndef __SIGNAL_H
#define __SIGNAL_H

#include "constants.h"
#include "obj.h"
#include "collection.h"

#include "debug.h"
#if defined(DEBUG_SIGNAL)
# define SIGNAL_DBG(format, arg...) { _DEBUG_CONSOLE(format, ##arg) _DEBUG_FILE(format, ##arg) }
#else
# define SIGNAL_DBG(format, arg...)
#endif

struct signal_ctx {
	struct obj o;
	struct collectible c;

	/* parent "signals" holder */
	struct collection *signals;

	int ref;

	char *name;

	struct collection *callbacks;
} __attribute__((packed));

typedef int (*signal_f)(void *obj, void *param);
typedef int (*timeout_f)(void *param);

struct signal_callback {
	struct obj o;
	struct collectible c;

	/* parent signal context */
	struct signal_ctx *ctx;

	int (*callback)(void *obj, void *param);
	void *param;

	int (*timeout_callback)(void *param);
	void *timeout_param;
	unsigned long long int timestamp;
	unsigned int timeout;

	int filter;
	void *obj;
} __attribute__((packed));

/* Must be called periodally, call the timeouts when needed. */
int signal_poll(struct collection *signals);

/* Get a signal from its name */
struct signal_ctx *signal_get(struct collection *signals, const char *name, int create);

int signal_ref(struct signal_ctx *signal);
int signal_unref(struct signal_ctx *signal);

/* Raise a signal and call all callbacks for it */
int signal_raise(struct signal_ctx *signal, void *obj);

/*
	Add a new signal to an owner list.
	The grouping scheme is at the discretion of the caller.
*/
struct signal_callback *signal_add(struct collection *signals, struct collection *group, const char *name, int (*callback)(void *obj, void *param), void *param);

/* Add a filter to a signal callback */
int signal_filter(struct signal_callback *s, void *obj);

/* Add a timeout on a signal callback. If the callback is not triggered in time, the timeout callback will be. */
int signal_timeout(struct signal_callback *s, unsigned int timeout, int (*callback)(void *param), void *param);

/* Delete a callback that is registered in the specified group */
int signal_del(struct collection *group, struct signal_callback *s);

/* Clear all callbacks from a group */
int signal_clear(struct collection *group);

/*
	Clear all callbacks from a group that have a
	filter registered on the specified object.
*/
int signal_clear_all_with_filter(struct collection *group, void *obj);

/*
	Clear all callbacks from a group that have a
	filter registered on the specified object
	and the specified signal.
*/
int signal_clear_with_filter(struct collection *group, char *name, void *obj);

#endif /* __SIGNAL_H */
