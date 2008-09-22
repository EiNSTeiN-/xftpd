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
#endif

#include <stdio.h>

#include "obj.h"

unsigned long long int obj_balance = 0;

/*
	Initialize an object, given a 'self' and a destructor function.
*/
int FUNC_INLINE obj_init(struct obj *o, void *self, void (*destructor)(void *self)) {
	
	if(!o) {
		OBJ_DBG("ERROR: o == NULL");
		return 0;
	}

	o->ref = 0;
	o->destroyed = 0;

	o->debug = 0;

	o->iscalled = 0;
	o->destructor = destructor;
	o->self = self;
	
	//OBJ_DBG("obj_init(); self == %08x; o == %08x", (int)self, (int)o);

	return 1;
}

int FUNC_INLINE obj_debug(struct obj *o, char debug) {
	
	if(!o) {
		OBJ_DBG("ERROR: o == NULL");
		return 0;
	}

	//o->debug = debug;

	return 1;
}

/*
	Mark the object as deleted or delete it if the
	reference count reach zero. Return 1 if the object
	was destroyed, zero if it was marked as destroyed.
*/
int FUNC_INLINE obj_destroy(struct obj *o) {
	int self, debug;
	
	if(!o) {
		OBJ_DBG("ERROR: o == NULL");
		return 0;
	}

	if(o->iscalled) {
		/* destroy has already been called ! */
		if(o->debug) OBJ_DBG("obj[%08x] Destructor has already been called.", (int)o->self);
		return 1;
	}

	if(o->ref) {
		o->destroyed = 1;
		if(o->debug) OBJ_DBG("obj[%08x] Cannot be destroyed, but marked as such (%u refs).", (int)o->self, o->ref);
		return 0;
	}
	
	o->iscalled = 1;

	self = (int)o->self;
	debug = o->debug;
	if(debug) OBJ_DBG("obj[%08x] (destroy) Calling destructor...", self);
	(*o->destructor)(o->self);
	if(debug) OBJ_DBG("obj[%08x] (destroy) Destroyed.", self);

	return 1;
}

/*
	Return the 'self' passed as a parameter at the
	object's initialization.
*/
void FUNC_INLINE *obj_self(struct obj *o) {
	
	if(!o) {
		OBJ_DBG("ERROR: o == NULL");
		return 0;
	}
	
	return o->self;
}

/*
	Increase the reference count, preventing the
	object of being deleted until the obj_unref is
	called.
*/
int FUNC_INLINE obj_ref(struct obj *o) {
	
	if(!o) {
		OBJ_DBG("ERROR: o == NULL");
		return 0;
	}

	if(o->destroyed) {
		OBJ_DBG("obj[%08x] ERROR: Referencing after destruction ...", (int)o->self);
	}

	o->ref++;

	obj_balance++;

	return 1;
}

/*
	Decrease the reference count, and call the
	destructor function if it's reference count
	reaches zero. Retrun 1 if the object was
	destroyed after the deletion.
*/
int FUNC_INLINE obj_unref(struct obj *o) {
	
	if(!o) {
		OBJ_DBG("ERROR: o == NULL");
		return 0;
	}

	if(!o->ref) {
		/* Bam. Trying to dereference but no references! */
		OBJ_DBG("obj[%08x] ERROR: Calls are not balanced.", (int)o->self);
		return 0;
	}

	obj_balance--;

	o->ref--;

	if(o->destroyed && !o->ref) {
		int self, debug;

		if(o->iscalled) {
			/* MAY happen if there was a referencing/dereferencing inside the destructor call. */
			OBJ_DBG("obj[%08x] ERROR: BAM! Destructor has already been called.", (int)o->self);
			return 1;
		}

		o->iscalled = 1;

		self = (int)o->self;
		debug = o->debug;
		if(debug) OBJ_DBG("obj[%08x] (unref) Calling destructor...", self);
		(*o->destructor)(o->self);
		if(debug) OBJ_DBG("obj[%08x] (unref) Destroyed.", self);
		return 1;
	}

	return 0;
}

/*
	Return zero when the object is marked as destroyed,
	1 otherwise.
*/
int FUNC_INLINE obj_isvalid(struct obj *o) {
	
	if(!o) {
		OBJ_DBG("ERROR: o == NULL");
		return 0;
	}

	return (o->destroyed == 0);

}
