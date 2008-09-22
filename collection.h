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

#ifndef __COLLECTION_H
#define __COLLECTION_H

#include "constants.h"

#ifndef NO_FTPD_DEBUG
#  define DEBUG_COLLECTION
#endif

#ifdef DEBUG_COLLECTION
# ifdef FTPD_DEBUG_TO_CONSOLE
#  define COLLECTION_DBG(format, arg...) printf("["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg)
#  define C_CHECKED(exp) { \
	if(!exp) { \
		printf("["__FILE__ ":\t%d ]\tC_CHECKED: you shoud check that.\n", __LINE__); \
	} \
}
# else
#  include "logging.h"
#  define COLLECTION_DBG(format, arg...) logging_write("debug.log", "["__FILE__ ":\t%d ]\t" format "\n", __LINE__, ##arg)
#  define C_CHECKED(exp) { \
	if(!exp) { \
		logging_write("debug.log", "["__FILE__ ":\t%d ]\tC_CHECKED: you shoud check that.\n", __LINE__); \
	} \
}
# endif
#else
#  define COLLECTION_DBG(format, arg...)
#  define C_CHECKED(exp) exp
#endif

typedef void* collection_item; /* for lua */
typedef struct collection _collection;

struct collection {
	unsigned int ordered;
	unsigned int count;

	struct collection_item *first;
	unsigned long long int current_uid;

	/*
		reference the lastly checked item to optimize such
		syntax as :
		if(collection_find(c, p)) {
			collection_delete(c, p);
		}
	*/
	struct collection_item *last_checked;

	/* we need this to not delete the collection as long as its locked */
	int locked;
	int deleted;

	/* 1 if the collection is void (will be deleted so no new items are allowed) */
	int is_void;
} __attribute__((packed));

/* this collection_item is not the same than the one in the lua .pkg */
struct collection_item {
	struct collection_item *next;

	unsigned long long int uid;
	void *item;

	/* we need this to not delete the item as long as its locked */
	int locked;
	int deleted;

} __attribute__((packed));	

typedef unsigned int (*collection_f)(struct collection *c, void *item, void *param);

/* constructor/destructors */
struct collection *collection_new();
void collection_destroy(struct collection *c);

/* print debug information on a collection's internal structure. */
void collection_debug(struct collection *c);

/* i/o */
unsigned int collection_add(struct collection *c, void *item);
#define collection_add_first collection_add
//unsigned int collection_add_first(struct collection *c, void *item);
unsigned int chk_collection_delete(struct collection *c, void *item);
#define collection_delete(a,b) { \
	C_CHECKED(chk_collection_delete(a,b)); \
}

/* utilities */
unsigned int collection_movelast(struct collection *c, void *item);
unsigned int collection_find(struct collection *c, void *item);
unsigned int collection_size(struct collection *c);
void *collection_first(struct collection *c);
int collection_is_void(struct collection *c);
int chk_collection_void(struct collection *c);
#define collection_void(a) { \
	C_CHECKED(chk_collection_void(a)); \
}

/* iterators */
unsigned int collection_iterate(struct collection *c, unsigned int (*callback)(struct collection *c, void *item, void *param), void *param);
void *collection_match(struct collection *c, unsigned int (*callback)(struct collection *c, void *item, void *param), void *param);
void *collection_next(struct collection *c, unsigned int *uid);
void *collection_first(struct collection *c);

/* [un]lock */
unsigned int collection_unlock(struct collection *c, void *element);
unsigned int collection_lock(struct collection *c, void *element);

#endif /* __COLLECTION_H */
