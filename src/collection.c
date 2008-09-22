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
#else
#include <stdlib.h>
#endif

#include "collection.h"
#include "logging.h"

/*
	On any error with memory allocation, the program will shut down.
*/

//collection_static_list(all_iterators);

static void instance_destroy(struct collectible_instance *instance) {
	
	if(obj_isvalid(&instance->o)) {
		/*
			The obj_isvalid here protect us from decreasing the objects
			count if it was already done before
		*/
		
		instance->self->collectors.count--;
		instance->c->collectibles.count--;
		
		obj_destroy(&instance->o);
	}
	
	return;
}

void collection_iterator_destroy(struct collection_iterator *iter) {
	
	iter->next = NULL;
	
	collection_list_remove(&iter->iterators);
	//collection_list_remove(&iter->all_iterators);
	
	free(iter);
	
	return;
}

/* Empty the collection, call the destructors if needed */
int collection_t_empty(struct collection *c, char *file, int line) {
//	struct collection_list *current, *next;
	struct collection_iterator *iter;
	struct collectible *cb;
	
	COLLECTION_ASSERT(c, "collection_empty: c == NULL");
	COLLECTION_ASSERT(obj_isvalid(&c->o), "collection_empty: 'c->o' is not valid");
	
	obj_ref(&c->o);
	
	/* Unlink all collectibles */
	iter=collection_new_iterator(c);
	for(cb=collection_c_next(c, iter);cb;cb=collection_c_next(c, iter)) {
		
		/* remove the collectible from the collection. */
		collection_c_delete(c, cb);
		
		/* Cascade delete he collectible if needed. */
		if(c->destroy_type == C_CASCADE) {
			/* We destroy the collectible's parent by destroying the collectible itself */
			obj_destroy(&cb->o);
		}
	}
	collection_iterator_destroy(iter);
	
	obj_unref(&c->o);
	
	return 1;
}
/*
int collection_cleanup_iterators() {
	struct collection_list *current, *next;
	
	current = all_iterators.next;
	while(current != &all_iterators) {
		struct collection_iterator *iter = CONTAINING_RECORD(current, struct collection_iterator, all_iterators);
		next = current->next;
		collection_iterator_destroy(iter);
		current = next;
	}
	
	return 1;
}
*/
static void collection_obj_destroy(struct collection *c) {
	struct collection_list *current, *next;
	struct collection_iterator *iter;
	struct collectible *cb;
	
	/* 1. Make sure the collection is void during this process */
	c->is_void = 1;
	
	/* 3. Unlink all collectibles */
	iter = collection_new_iterator(c);
	for(cb=collection_c_next(c, iter);cb;cb=collection_c_next(c, iter)) {
		
		/* remove the collectible from the collection. */
		collection_c_delete(c, cb);
		
		/* Cascade delete he collectible if needed. */
		if(c->destroy_type == C_CASCADE) {
			/* We destroy the collectible's parent by destroying the collectible itself */
			obj_destroy(&cb->o);
		}
	}
	
	/* 2. Destroy all iterators */
	current = c->iterators.list.next;
	while(current != &c->iterators.list) {
		struct collection_iterator *iter = CONTAINING_RECORD(current, struct collection_iterator, iterators);
		next = current->next;
		collection_iterator_destroy(iter);
		current = next;
	}
	
	if(c->collectibles.count) {
		COLLECTION_DBG("collection_obj_destroy: ERROR: collection still contain %u collectibles", c->collectibles.count);
	}
	
	/* 4. Free the structure. */
	free(c);
	
	return;
}

struct collection *collection_t_new(collection_destroy_type destroy_type, char *file, int line) {
	struct collection *c;

	c = malloc(sizeof(struct collection));
	if(!c) {
		COLLECTION_DBG("collection_new: malloc failed");
		COLLECTION_TRACEBACK();
		exit(1);
	}
	
	obj_init(&c->o, c, (obj_f)collection_obj_destroy);
	c->destroy_type = destroy_type;
	
	c->is_void = 0;
	
	collection_counted_list_init(&c->iterators);
	collection_counted_list_init(&c->collectibles);
	
	return c;
}

int collection_t_is_void(struct collection *c, char *file, int line) {

	if(!c) {
		COLLECTION_DBG("collection_is_void: c == NULL");
		COLLECTION_TRACEBACK();
		return 1;
	}

	return c->is_void;
}

void collection_t_void(struct collection *c, char *file, int line) {

	if(!c) {
		COLLECTION_DBG("collection_void: c == NULL");
		COLLECTION_TRACEBACK();
	} else {
		c->is_void = 1;
	}

	return;
}

static void collectible_obj_destroy(struct collectible *cb) {
	struct collection_list *current, *next;
	
	/* 1. Unlink the collectible from all of its collections */
	current = cb->collectors.list.next;
	while(current != &cb->collectors.list) {
		struct collectible_instance *instance = CONTAINING_RECORD(current, struct collectible_instance, collectors);
		next = current->next;
		
		instance_destroy(instance);
		
		current = next;
	}
	
	if(cb->collectors.count) {
		COLLECTION_DBG("collection_obj_destroy: ERROR: collectible still linked to %u collections", cb->collectors.count);
	}
	
	/* 2. At the very end, call for the parent object's destruction */
	//self = cb->self;
	//cb->self = NULL; /* don't null it here cuz if it's locked it won't be destroyed untill unlock so we might need this ref still */
	
	if(cb->self) {
		obj_destroy(cb->self);
	}
	
	/*
		As the collectible structure is embedded in a parent
		structure, after calling the destroy method may very
		well free the memory associated with this collectible
		so from here on, we should never reference *cb again.
	*/
	
	return;
}

int collectible_c_init(struct collectible *cb, struct obj *self) {
	
	obj_init(&cb->o, cb, (obj_f)collectible_obj_destroy);
	collection_counted_list_init(&cb->collectors);
	
	cb->self_locked = 0;
	cb->self = self;
	
	return 1;
}

void collectible_c_destroy(struct collectible *cb) {
	unsigned int i;
	
	if(cb->self_locked) {
		COLLECTION_DBG("collectible_destroy: Unreferencing %u times", cb->self_locked);
		for(i=0;i<cb->self_locked;i++) {
			obj_unref(cb->self);
		}
		cb->self_locked = 0;
	}
	cb->self = NULL;
	obj_destroy(&cb->o);
	
	return;
}

static void instance_obj_destroy(struct collectible_instance *instance) {
	struct collection_list *current, *next;
	
	/* Update all iterators to unreference this instance */
	current = instance->c->iterators.list.next;
	while(current != &instance->c->iterators.list) {
		struct collection_iterator *iter = CONTAINING_RECORD(current, struct collection_iterator, iterators);
		next = current->next;
		
		if(iter->next == instance) {
			if(&instance->c->collectibles.list == instance->collectibles.next) {
				iter->next = NULL;
			} else {
				iter->next = CONTAINING_RECORD(instance->collectibles.next, struct collectible_instance, collectibles);
			}
		}
		current = next;
	}
	
	/* Make this instance unaccessible from the collectible */
	collection_list_remove(&instance->collectors);
	
	/* Make this instance unaccessible from its collection */
	collection_list_remove(&instance->collectibles);
	
	instance->c = NULL;
	instance->self = NULL;
	
	/* Free the memory */
	free(instance);
	
	return;
}

/* add an element at the beginning of a collection */
int collection_t_add(struct collection *c, struct collectible *cb, char *file, int line) {
	struct collectible_instance *instance;

	COLLECTION_ASSERT(c, "collection_add: c == NULL");
	COLLECTION_ASSERT(cb, "collection_add: cb == NULL");

	/*
		Check the validity of all implied objects so
		we don't link during destruction
	*/
	COLLECTION_ASSERT(obj_isvalid(&c->o), "collection_add: collection's object is invalid");
	COLLECTION_ASSERT(obj_isvalid(&cb->o), "collection_add: collectible's object is invalid");
	COLLECTION_ASSERT(obj_isvalid(cb->self), "collection_add: collectible's 'self' is invalid");
	COLLECTION_ASSERT(!c->is_void, "collection_add: collection is void");
	
	instance = malloc(sizeof(struct collectible_instance));
	if(!instance) {
		COLLECTION_DBG("collection_add: malloc failed");
		COLLECTION_TRACEBACK();
		exit(1);
	}
	
	obj_init(&instance->o, instance, (obj_f)instance_obj_destroy);
	
	/* Make the link with the parent collectible */
	cb->collectors.count++;
	collection_list_addfirst(&cb->collectors.list, &instance->collectors);
	instance->self = cb;

	/* Make the link with the parent collection */
	instance->c = c;
	c->collectibles.count++;
	collection_list_addfirst(&c->collectibles.list, &instance->collectibles);
	
	return 1;
}

/*
	Given a collection and a collectible, return the associative
	instance in the shortest way possible.
*/
static struct collectible_instance *collection_associative_instance(struct collection *c, struct collectible *cb) {
	struct collection_list *current, *next;
	
	/*
		Iterate the smallest list between the collection
		and the collectible's list and check if the other
		object is linked via the collectible_instance
		structure.
	*/
	if(c->collectibles.count < cb->collectors.count) {
		/* Iterate the collection's instances and check for instance.self == cb */
		
		current = c->collectibles.list.next;
		while(current != &c->collectibles.list) {
			struct collectible_instance *instance = CONTAINING_RECORD(current, struct collectible_instance, collectibles);
			next = current->next;
			
			if(instance->self == cb) {
				/*
					We've found the collectible to be in one of the
					collection's instance ...
				*/
//				c->last = cb;
				return instance;
			}
			
			current = next;
		}
	} else {
		/* Iterate the collectible's instances and check for instance.c == c */
		
		current = cb->collectors.list.next;
		while(current != &cb->collectors.list) {
			struct collectible_instance *instance = CONTAINING_RECORD(current, struct collectible_instance, collectors);
			next = current->next;
			
			if(instance->c == c) {
				/*
					We've found the collection to be in one of the
					collectible's instances ...
				*/
//				c->last = cb;
				return instance;
			}
			
			current = next;
		}
	}
	
	return NULL;
}

/* return 1 if the element is in the collection */
int collection_t_find(struct collection *c, struct collectible *cb, char *file, int line) {
	struct collectible_instance *instance;
	
	COLLECTION_ASSERT(c, "collection_find: c == NULL");
	COLLECTION_ASSERT(cb, "collection_find: cb == NULL");
	
	instance = collection_associative_instance(c, cb);
	COLLECTION_ASSERT(instance, "");
	COLLECTION_ASSERT(obj_isvalid(&instance->o), "");
	
	return 1;
}

/*
	Delete the collectible from the collection
*/
int collection_t_delete(struct collection *c, struct collectible *cb, char *file, int line) {
	struct collectible_instance *instance;

	COLLECTION_ASSERT(c, "collection_delete: c == NULL");
	COLLECTION_ASSERT(cb, "collection_delete: cb == NULL");

	instance = collection_associative_instance(c, cb);
	COLLECTION_ASSERT(instance, "collection_delete: no associative instance between collection and collectible");
	
	if(obj_isvalid(&instance->o)) {
		instance_destroy(instance);
		return 1;
	}
	
	return 0;
}

/*
	Move the collectible to the last position in the collection
	If you use this function while iterating a collection, the
	result will be an indefinite iteration.
*/
int collection_t_movelast(struct collection *c, struct collectible *cb, char *file, int line) {
	struct collectible_instance *instance;

	COLLECTION_ASSERT(c, "collection_movelast: c == NULL");
	COLLECTION_ASSERT(cb, "collection_movelast: cb == NULL");
	COLLECTION_ASSERT(obj_isvalid(&c->o), "collection_movelast: 'c->o' is not valid");
	COLLECTION_ASSERT(obj_isvalid(&cb->o), "collection_movelast: 'cb->o' is not valid");
	COLLECTION_ASSERT(obj_isvalid(cb->self), "collection_movelast: 'cb->self' is not valid");

	instance = collection_associative_instance(c, cb);
	COLLECTION_ASSERT(instance, "collection_movelast: no associative instance between collection and collectible");
	COLLECTION_ASSERT(obj_isvalid(&instance->o), "");

	/* Delete from its current position, and add again at the end of the same collection */
	collection_list_remove(&instance->collectibles);
	collection_list_addlast(&c->collectibles.list, &instance->collectibles);

	return 1;
}

/* count all items in a collection */
unsigned int collection_t_size(struct collection *c, char *file, int line) {

	COLLECTION_ASSERT(c, "collection_size: c == NULL");
	COLLECTION_ASSERT(obj_isvalid(&c->o), "collection_size: 'c->o' is not valid");

	return c->collectibles.count;
}

/*
	Lock a collectible so it won't be deleted until it is unlocked.
*/
int collection_t_lock(struct collection *c, struct collectible *cb, char *file, int line) {
	struct collectible_instance *instance;
	
	COLLECTION_ASSERT(c, "collection_lock: c == NULL");
	COLLECTION_ASSERT(cb, "collection_lock: cb == NULL");
	COLLECTION_ASSERT(obj_isvalid(&c->o), "collection_lock: 'c->o' is not valid");
	COLLECTION_ASSERT(obj_isvalid(&cb->o), "collection_lock: 'c->o' is not valid");
	COLLECTION_ASSERT(obj_isvalid(cb->self), "collection_lock: 'c->self' is not valid");
	COLLECTION_ASSERT(!c->is_void, "collection_lock: collection is void");

	instance = collection_associative_instance(c, cb);
	COLLECTION_ASSERT(instance, "collection_lock: no associative instance between collection and collectible");
	COLLECTION_ASSERT(obj_isvalid(&instance->o), "collection_lock: associative instance is invalid");
	
	/*
		Reference both the collectible and the collection so none
		of them will be deleted during the lock. Also, just to be sure,
		lock the collectible's parent object.
	*/
	obj_ref(&c->o);
	obj_ref(&cb->o);
	obj_ref(cb->self);
	cb->self_locked++;
	obj_ref(&instance->o);

	return 1;
}

/* return 1 if the item was destroyed due to its unlocking */
int collection_t_unlock(struct collection *c, struct collectible *cb, char *file, int line) {
	struct collectible_instance *instance;
	int ret;

	COLLECTION_ASSERT(c, "collection_unlock: c == NULL");
	COLLECTION_ASSERT(cb, "collection_unlock: cb == NULL");

	/*
		The instance may no longer exist at the time of the unlocking.
		The caller must make sure he unlock the right element from the
		right collection.
	*/
	instance = collection_associative_instance(c, cb);
	COLLECTION_ASSERT(instance, "collection_unlock: no associative instance between collection and collectible");
	
	ret = (!obj_isvalid(cb->self) || !obj_isvalid(&cb->o));

	/* The unreferencing must be in this exact order */
	obj_unref(&instance->o); /* associative instance NEED 'c' and 'cb' */
	cb->self_locked--;
	obj_unref(&cb->o); /* 'cb' NEED 'cb->self' AND 'c' */
	if(cb->self) obj_unref(cb->self); /* the collectible's parent must be destroyed *after* its collectible. */
	obj_unref(&c->o); /* needs nothing, when this is destroyed everything else should be GONE. */
	
	return ret;
}

/* iterate the collection */
/* return 1 if the iteration was completed */
int collection_t_iterate(struct collection *c, int (*callback)(struct collection *c, void *item, void *param), void *param, char *file, int line) {
	struct collection_iterator *iter;
	struct collectible *cb;
	int ret;

	COLLECTION_ASSERT(c, "collection_iterate: c == NULL");
	COLLECTION_ASSERT(obj_isvalid(&c->o), "collection_iterate: 'c->o' is not valid");

	obj_ref(&c->o);
	
	iter = collection_new_iterator(c);
	for(cb=collection_c_next(c, iter);cb;cb=collection_c_next(c, iter)) {
		
		ret = (*callback)(c, obj_self(cb->self), param);
		
		if(!obj_isvalid(&c->o)) {
			COLLECTION_DBG("collection_iterate: collection destroyed during iteration.");
			break;
		}
		
		if(!ret) {
			obj_unref(&c->o);
			collection_iterator_destroy(iter);
			return 0;
		}
	}
	collection_iterator_destroy(iter);
	
	obj_unref(&c->o);

	return 1;
}

/* iterate the collection */
/* return the element chosen by the matcher */
void *collection_t_match(struct collection *c, int (*callback)(struct collection *c, void *item, void *param), void *param, char *file, int line) {
	struct collection_iterator *iter;
	struct collectible *cb;
	int ret;
	
	COLLECTION_ASSERT(c, "collection_match: c == NULL");
	COLLECTION_ASSERT(obj_isvalid(&c->o), "collection_match: 'c->o' is not valid");
	
	obj_ref(&c->o);
	
	iter = collection_new_iterator(c);
	for(cb=collection_c_next(c, iter);cb;cb=collection_c_next(c, iter)) {
		
		collection_c_lock(c, cb);
		
		ret = (*callback)(c, obj_self(cb->self), param);
		
		if(!obj_isvalid(&c->o)) {
			COLLECTION_DBG("collection_iterate: collection destroyed during iteration.");
			COLLECTION_TRACEBACK();
			break;
		}
		
		if(!obj_isvalid(&cb->o) || !obj_isvalid(cb->self)) {
			/* deleted during unlock: bad, bad, bad */
			COLLECTION_DBG("collection_match: deleting items while matching is bad behaviour.");
			COLLECTION_TRACEBACK();
			collection_c_unlock(c, cb);
			if(ret) {
				obj_unref(&c->o);
				collection_iterator_destroy(iter);
				return NULL;
			}
			continue;
		}
		
		collection_c_unlock(c, cb);
		
		if(ret) {
			obj_unref(&c->o);
			collection_iterator_destroy(iter);
			return obj_self(cb->self);
		}
	}
	collection_iterator_destroy(iter);
	
	obj_unref(&c->o);
	
	return NULL;
}

void *collection_t_first(struct collection *c, char *file, int line) {
	struct collection_list *current;
	
	COLLECTION_ASSERT(c, "collection_first: c == NULL");
	COLLECTION_ASSERT(obj_isvalid(&c->o), "collection_first: 'c->o' is not valid");

	current = c->collectibles.list.next;
	while(current != &c->collectibles.list) {
		struct collectible_instance *instance = CONTAINING_RECORD(current, struct collectible_instance, collectibles);
		struct collectible *cb = instance->self;
		
		if(obj_isvalid(&instance->o) && obj_isvalid(&cb->o) && obj_isvalid(cb->self)) {
			
			return obj_self(cb->self);
		}
		current = current->next;
	}

	return NULL;
}

struct collection_iterator *collection_t_new_iterator(struct collection *c, char *file, int line) {
	struct collection_iterator *iter;
	
	COLLECTION_ASSERT(c, "collection_iterator: c == NULL");
	COLLECTION_ASSERT(obj_isvalid(&c->o), "collection_iterator: 'c->o' is not valid");
	
	iter = malloc(sizeof(struct collection_iterator));
	if(!iter) {
		COLLECTION_DBG("memory error");
		COLLECTION_TRACEBACK();
		exit(0);
	}
	
	collection_list_init(&iter->iterators);
	//collection_list_init(&iter->all_iterators);
	
	c->iterators.count++;
	collection_list_addfirst(&c->iterators.list, &iter->iterators);
	//collection_list_addfirst(&all_iterators, &iter->all_iterators);
	
	if(!collection_size(c)) {
		iter->next = NULL;
	} else {
		iter->next = CONTAINING_RECORD(c->collectibles.list.next, struct collectible_instance, collectibles);
	}
	
	return iter;
}

void *collection_t_next(struct collection *c, struct collection_iterator *iter, char *file, int line) {
	struct collection_list *current;
	
	COLLECTION_ASSERT(c, "collection_next: c == NULL");
	COLLECTION_ASSERT(obj_isvalid(&c->o), "collection_next: isvalid(c)");
	COLLECTION_ASSERT(iter, "collection_next: iter == NULL");
	COLLECTION_ASSERT(iter->next, "");
	
	current = &iter->next->collectibles;
	while(current != &c->collectibles.list) {
		struct collectible_instance *instance = CONTAINING_RECORD(current, struct collectible_instance, collectibles);
		struct collectible *cb = instance->self;
		
		if(obj_isvalid(&instance->o) && obj_isvalid(&cb->o) && obj_isvalid(cb->self)) {
			
			/* switch to the next instance */
			if(&instance->c->collectibles.list == instance->collectibles.next) {
				iter->next = NULL;
			} else {
				iter->next = CONTAINING_RECORD(instance->collectibles.next, struct collectible_instance, collectibles);
			}
			
			return cb;
		}
		current = current->next;
	}

	return NULL;
}

void *collection_next(struct collection *c, struct collection_iterator *iter) {
	struct collectible *cb;
	
	cb = collection_c_next(c, iter);
	if(cb) {
		/* we found the instance we need to return this time. */
		return obj_self(cb->self);
	}
	
	return NULL;
}
