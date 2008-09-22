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

#include "debug.h"
#if defined(DEBUG_COLLECTION)
# define COLLECTION_DBG(format, arg...) { _DEBUG_CONSOLE(format, ##arg) _DEBUG_FILE(format, ##arg) }
#else
# define COLLECTION_DBG(format, arg...)
#endif

#include "obj.h"

#ifndef CONTAINING_RECORD
#define CONTAINING_RECORD(address, type, field) ((type *)( \
                                                  (char *)(address) - \
                                                  (char *)(&((type *)0)->field)))
#endif

#define collection_static_list(_name) static struct collection_list _name = { &_name, &_name };

#define collection_list_init(_list) { \
	(_list)->prev = (_list); \
	(_list)->next = (_list); \
}

#define collection_counted_list_init(_list) { \
	collection_list_init(&(_list)->list); \
	(_list)->count = 0; \
}

#define collection_list_remove(_list) { \
	(_list)->next->prev = (_list)->prev; \
	(_list)->prev->next = (_list)->next; \
	collection_list_init(_list); \
}

#define collection_list_addlast(_head, _list) { \
	(_list)->next = (_head); \
	(_list)->prev = (_head)->prev; \
	(_head)->prev->next = (_list); \
	(_head)->prev = (_list); \
}

#define collection_list_addfirst(_head, _list) { \
	(_list)->prev = (_head); \
	(_list)->next = (_head)->next; \
	(_head)->next->prev = (_list); \
	(_head)->next = (_list); \
}

/*
	All items in the collections are in
	doubly-linked lists.
*/
struct collection_list {
	struct collection_list *prev;
	struct collection_list *next;
} __attribute__((packed));

/*
	A counted list keep track of how many items
	there is in the collection_list
*/
struct collection_counted_list {
	struct collection_list list;
	unsigned int count;
} __attribute__((packed));

/*
	Iterators are stored at collection level and always
	reflect the changes made on the collection. The
	iterators are garbage collected once every cycle.
*/
typedef struct collection_iterator collection_iterator;
struct collection_iterator {
	//struct collection_list all_iterators; /* linked to the global iterator lists. */
	
	struct collection_list iterators; /* linked to the parent collection.iterators list */
	
	/*
		Specify the next item to be iterated. NULL if there is none next.
		It is important to keep track of the 'next' rather than the 'current'
		because then we can delete the current and it'll still work.
	*/
	struct collectible_instance *next;
} __attribute__((packed));

/*
	When the destroy type is CASCDE, the collection's collectibles
	objects will be destroyed altogether with the collection.
*/
typedef enum {
	C_NONE,
	C_CASCADE,
} collection_destroy_type;

/*
	Base holder for all collectibles
*/
typedef struct collection collection;
struct collection {
	
	struct obj o;
	collection_destroy_type destroy_type;
	
	/* 1 if the collection is void (no new items are allowed) */
	char is_void;
	
	struct collection_counted_list iterators; /* head of iterator. */
	struct collection_counted_list collectibles; /* head of instance.collectibles list  */

} __attribute__((packed));

/*
	A collectible_instance is created every time a collectible
	is added to a collection.
*/
struct collectible_instance {
	struct obj o;
	
	struct collection_list collectors; /* linked to the parent collectible.collectors list */
	
	/* reference to the parent instance of the collectible object. */
	struct collectible *self;
	
	/* collection of wich this object is part of */
	struct collection *c;
	struct collection_list collectibles; /* linked to the c.collectibles.list list */
} __attribute__((packed));

/*
	this structure must be embedded in a 'host' structure and initialized
	with the parent host structre's object as 'self'
*/
typedef struct collectible collectible;
struct collectible {
	struct obj o;

	struct collection_counted_list collectors; /* head of instance.collectors list */
	
	char self_locked;
	struct obj *self;

} __attribute__((packed));

#define COLLECTION_TRACEBACK() { \
	COLLECTION_DBG("traceback: %s:%u", file, line); \
}

#define COLLECTION_ASSERT(o, _s) { \
	if(!(o)) { \
		if((_s) && *(_s)) { \
			COLLECTION_DBG("%s", (char *)(_s)); \
			COLLECTION_TRACEBACK(); \
		} \
		return 0; \
	} \
}

typedef int (*collection_f)(struct collection *c, void *item, void *param);

/* constructor/destructors */
struct collection *collection_t_new(collection_destroy_type destroy_type, char *file, int line);
#define collection_new(_c) collection_t_new(_c, __FILE__, __LINE__)

#define collection_destroy(c) obj_destroy(&c->o);

/* Empty the collection, call the destructors if needed */
int collection_t_empty(struct collection *c, char *file, int line);
#define collection_empty(_c) collection_t_empty(_c, __FILE__, __LINE__)

int collectible_c_init(struct collectible *cb, struct obj *self);
#define collectible_init(_cb) collectible_c_init(&(_cb)->c, &(_cb)->o)
void collectible_c_destroy(struct collectible *cb);
#define collectible_destroy(_cb) collectible_c_destroy(&(_cb)->c)

/* i/o */
int collection_t_add(struct collection *c, struct collectible *cb, char *file, int line);
#define collection_c_add(_c, _cb) collection_t_add(_c, _cb, __FILE__, __LINE__)
#define collection_add(_c, _cb) collection_c_add(_c, &(_cb)->c)
int collection_t_delete(struct collection *c, struct collectible *cb, char *file, int line);
#define collection_c_delete(_c, _cb) collection_t_delete(_c, _cb, __FILE__, __LINE__)
#define collection_delete(_c, _cb) collection_c_delete(_c, &(_cb)->c)

/* utilities */
void *collection_t_first(struct collection *c, char *file, int line);
#define collection_first(_c) collection_t_first(_c, __FILE__, __LINE__)
int collection_t_movelast(struct collection *c, struct collectible *cb, char *file, int line);
#define collection_c_movelast(_c, _cb) collection_t_movelast(_c, _cb, __FILE__, __LINE__)
#define collection_movelast(_c, _cb) collection_c_movelast(_c, &(_cb)->c)
int collection_t_find(struct collection *c, struct collectible *cb, char *file, int line);
#define collection_c_find(_c, _cb) collection_t_find(_c, _cb, __FILE__, __LINE__)
#define collection_find(_c, _cb) collection_c_find(_c, &(_cb)->c)
unsigned int collection_t_size(struct collection *c, char *file, int line);
#define collection_size(_c) collection_t_size(_c, __FILE__, __LINE__)
int collection_t_is_void(struct collection *c, char *file, int line);
#define collection_is_void(_c) collection_t_is_void(_c, __FILE__, __LINE__)
void collection_t_void(struct collection *c, char *file, int line);
#define collection_void(_c) collection_t_void(_c, __FILE__, __LINE__)


/* callback iterators stuff */
int collection_t_iterate(struct collection *c, int (*callback)(struct collection *c, void *item, void *param), void *param, char *file, int line);
#define collection_iterate(_c, _f, _p) collection_t_iterate(_c, _f, _p, __FILE__, __LINE__)
void *collection_t_match(struct collection *c, int (*callback)(struct collection *c, void *item, void *param), void *param, char *file, int line);
#define collection_match(_c, _f, _p) collection_t_match(_c, _f, _p, __FILE__, __LINE__)

/* non-callback iterators stuff */
//int collection_cleanup_iterators();
struct collection_iterator *collection_t_new_iterator(struct collection *c, char *file, int line);
#define collection_new_iterator(_c) collection_t_new_iterator(_c, __FILE__, __LINE__)
void *collection_t_next(struct collection *c, struct collection_iterator *iter, char *file, int line);
#define collection_c_next(_c, _i) collection_t_next(_c, _i, __FILE__, __LINE__)
void *collection_next(struct collection *c, struct collection_iterator *iter);

/* [un]lock */
int collection_t_lock(struct collection *c, struct collectible *cb, char *file, int line);
#define collection_c_lock(_c, _cb) collection_t_lock(_c, _cb, __FILE__, __LINE__)
#define collection_lock(_c, _cb) collection_c_lock(_c, &(_cb)->c)
int collection_t_unlock(struct collection *c, struct collectible *cb, char *file, int line);
#define collection_c_unlock(_c, _cb) collection_t_unlock(_c, _cb, __FILE__, __LINE__)
#define collection_unlock(_c, _cb) collection_c_unlock(_c, &(_cb)->c)

#endif /* __COLLECTION_H */
