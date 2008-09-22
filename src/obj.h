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

#ifndef __OBJ_H
#define __OBJ_H

#include "constants.h"

#include "debug.h"
#if defined(DEBUG_OBJ)
# define OBJ_DBG(format, arg...) { _DEBUG_CONSOLE(format, ##arg) _DEBUG_FILE(format, ##arg) }
#else
# define OBJ_DBG(format, arg...)
#endif

struct obj {
	unsigned short ref; /* more than 65535 refs ? shouldn't be. */
	char destroyed;

	char debug;
	
	char iscalled;
	void (*destructor)(void *self);
	void *self;
} __attribute__((packed));

typedef void (*obj_f)(void *self);

int FUNC_INLINE obj_debug(struct obj *o, char debug);

extern unsigned long long int obj_balance;

/*
	Initialize an object, given a 'self' and a destructor function.
*/
int FUNC_INLINE obj_init(struct obj *o, void *self, void (*destructor)(void *self));

/*
	Mark the object as deleted or delete it if the
	reference count reach zero. Return 1 if the object
	was deleted, zero if it was marked as deleted.
*/
int FUNC_INLINE obj_destroy(struct obj *o);

/*
	Return the 'self' passed as a parameter at the
	object's initialization.
*/
void FUNC_INLINE *obj_self(struct obj *o);

/*
	Increase the reference count, preventing the
	object of being deleted until the obj_unref is
	called.
*/
int FUNC_INLINE obj_ref(struct obj *o);

/*
	Decrease the reference count, and call the
	destructor function if it's reference count
	reaches zero.
*/
int FUNC_INLINE obj_unref(struct obj *o);

/*
	Return zero when the object is marked as destroyed,
	1 otherwise.
*/
int FUNC_INLINE obj_isvalid(struct obj *o);

#endif /* __OBJ_H */
