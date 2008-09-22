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

#include "collection.h"
#include "logging.h"

/*
	On any error with memory allocation, the program will shut down.
*/

struct collection *collection_new() {
	struct collection *c;

	c = malloc(sizeof(struct collection));
	if(!c) {
		COLLECTION_DBG("collection_new: malloc failed");
		exit(1);
	}

	c->last_checked = NULL;
	c->first = NULL;
	c->current_uid = 1; /* start at 1, the uid 0 does not exist */

	c->ordered = 1;
	c->count = 0;

	c->locked = 0;
	c->deleted = 0;

	c->is_void = 0;

	return c;
}

int collection_is_void(struct collection *c) {

	if(!c) {
		COLLECTION_DBG("collection_is_void: c == NULL");
		return 1;
	}

	return c->is_void;
}

int chk_collection_void(struct collection *c) {

	if(!c) {
		COLLECTION_DBG("collection_void: c == NULL");
		return 0;
	}

	c->is_void = 1;

	return 1;
}

static void c_lock(struct collection *c) {

	c->locked++;

	return;
}

static void collection_free(struct collection *c) {
	struct collection_item *current, *next;

	current = c->first;
	while(current) {
		next = current->next;
		free(current);
		current = next;
	}

	free(c);

	return;
}

/* return 1 if the item was destroyed due to its unlocking */
static int c_unlock(struct collection *c) {

	c->locked--;

	if(!c->locked && c->deleted) {
		collection_free(c);
		return 1;
	}

	return 0;
}

void collection_destroy(struct collection *c) {

	if(!c) {
		COLLECTION_DBG("collection_destroy: c == NULL");
		return;
	}

	if(c->locked) {
		c->deleted = 1;
	} else {
		collection_free(c);
	}

	return;
}

/* copy the contents of collection a to collection b */
// int collection_copy(a, b)

/* add an element to the end of collection */
/*unsigned int collection_add(struct collection *c, void *item) {
	struct collection_item *citem, *current;

	if(!c) {
		COLLECTION_DBG("collection_add: c == NULL");
		return 0;
	}

	if(c->deleted) {
		COLLECTION_DBG("collection_add: collection is deleted");
		return 0;
	}

	if(c->is_void) {
		COLLECTION_DBG("collection_add: collection is void");
		return 0;
	}

	if(!item) {
		COLLECTION_DBG("collection_add: warning: item == NULL");
		return 0;
	}

	citem = malloc(sizeof(struct collection_item));
	if(!citem) {
		COLLECTION_DBG("collection_add: malloc failed");
		exit(1);
	}

	citem->locked = 0;
	citem->deleted = 0;
	citem->uid = c->current_uid++;
	citem->item = item;
	citem->next = NULL;

	if(!c->first) {
		c->first = citem;
	}
	else {
		current = c->first;
		while(current->next) {
			current = current->next;
		}
		current->next = citem;
	}
	
	c->last_checked = citem;

	c->count++;
	
	return 1;
}*/

/* add an element at the beginning of a collection */
unsigned int collection_add(struct collection *c, void *item) {
	struct collection_item *citem;

	if(!c) {
		COLLECTION_DBG("collection_add_first: c == NULL");
		return 0;
	}

	if(c->deleted) {
		COLLECTION_DBG("collection_add_first: collection is deleted");
		return 0;
	}

	if(c->is_void) {
		COLLECTION_DBG("collection_add_first: collection is void");
		return 0;
	}

	citem = malloc(sizeof(struct collection_item));
	if(!citem) {
		logging_write("fatal.log", "collection_add_first: malloc failed");
		exit(1);
	}

	citem->locked = 0;
	citem->deleted = 0;
	citem->uid = c->current_uid++;
	citem->item = item;
	citem->next = c->first;
	c->first = citem;

	c->last_checked = citem;

	c->ordered = 0;
	c->count++;

	return 1;
}

/* return 1 if the element is in the collection */
unsigned int collection_find(struct collection *c, void *item) {
	struct collection_item *current;

	if(!c) {
		COLLECTION_DBG("collection_find: c == NULL");
		return 0;
	}

	if(c->deleted) {
		COLLECTION_DBG("collection_find: collection is deleted");
		return 0;
	}

	if(c->last_checked) {
		if(c->last_checked->item == item && !c->last_checked->deleted) {
			return 1;
		}
	}

	current = c->first;
	while(current) {

		if((current->item == item) && !current->deleted) {
			c->last_checked = current;
			return 1;
		}

		current = current->next;
	}

	return 0;
}

/* unlink an item from its position in the list. the item is still valid */
static unsigned int unlink_item(struct collection *c, struct collection_item *item) {
	struct collection_item *current;

	if(c->last_checked == item) {
		c->last_checked = NULL;
	}

	if(c->first == item) {
		c->first = item->next;
		item->next = NULL;
		return 1;
	}

	current = c->first;
	while(current) {

		if(current->next == item) {
			current->next = item->next;
			item->next = NULL;
			return 1;
		}

		current = current->next;
	}

	return 0;
}

/* delete an element from the collection */
unsigned int chk_collection_delete(struct collection *c, void *item) {
	struct collection_item *current;

	if(!c) {
		COLLECTION_DBG("collection_delete: c == NULL");
		return 0;
	}

	if(c->deleted) {
		COLLECTION_DBG("collection_delete: collection is deleted");
		return 0;
	}

	if(c->last_checked) {
		if(!c->last_checked->deleted) {
			if(c->last_checked->item == item) {
				// DON'T NULL THIS OUT!
				//c->last_checked->item = NULL;
				if(c->last_checked->locked) {
					c->last_checked->deleted = 1;
				} else {
					current = c->last_checked;
					unlink_item(c, current);
					free(current);
				}
				c->count--;
				return 1;
			}
		}
	}

	current = c->first;
	while(current) {

		if(!current->deleted) {
			if(current->item == item) {
				// DON'T NULL THIS OUT!
				//current->item = NULL;
				if(current->locked) {
					current->deleted = 1;
				} else {
					unlink_item(c, current);
					free(current);
				}
				c->count--;
				return 1;
			}
		}

		current = current->next;
	}
	
	COLLECTION_DBG("collection_delete: could not delete %08x", (int)item);

	return 0;
}

/* move an item to the last position in the collection */
unsigned int collection_movelast(struct collection *c, void *item) {
	struct collection_item *current = NULL, *found = NULL;

	if(!c) {
		COLLECTION_DBG("collection_movelast: c == NULL");
		return 0;
	}

	if(c->deleted) {
		COLLECTION_DBG("collection_movelast: collection is deleted");
		return 0;
	}

	if(c->last_checked) {
		if((c->last_checked->item == item) && !c->last_checked->deleted) {
			found = c->last_checked;
			current = c->last_checked;
		}
	}

	if(!found  && !current) {
		current = c->first;
		while(current) {

			if((current->item == item) && !current->deleted) {
				found = current;
				break;
			}

			current = current->next;
		}
	}

	if(!found || !current) {
		COLLECTION_DBG("collection_movelast: was not found in the collection");
		return 0;
	}

	if(!current->next) {
		/* already last in the collection... */
		//COLLECTION_DBG("collection_movelast: was already last in the collection");
		return 1;
	}

	current = current->next;
	unlink_item(c, found);

	while(current->next) {
		current = current->next;
	}

	current->next = found;
	c->last_checked = found;
	c->ordered = 0;

	return 1;
}

/* count all items in a collection */
unsigned int collection_size(struct collection *c) {

	if(!c) {
		COLLECTION_DBG("collection_size: c == NULL");
		return 0;
	}

	if(c->deleted) {
		COLLECTION_DBG("collection_size: collection is deleted");
		return 0;
	}

	return c->count;
}

static void lock(struct collection *c, struct collection_item *item) {

	item->locked++;

	return;
}

/* return 1 if the item was destroyed due to its unlocking */
static int unlock(struct collection *c, struct collection_item *item) {

	if(!item->locked) {
		COLLECTION_DBG("WARNING!!! unbalanced lock/unlock!");
		return 0;
	}

	item->locked--;

	if(!item->locked && item->deleted) {
		unlink_item(c, item);
		free(item);
		return 1;
	}

	return 0;
}

unsigned int collection_lock(struct collection *c, void *element) {
	struct collection_item *current;

	if(!c) {
		COLLECTION_DBG("collection_lock: c == NULL");
		return 0;
	}

	if(c->deleted) {
		COLLECTION_DBG("collection_lock: collection is deleted");
		return 0;
	}

	if(c->last_checked) {
		if((c->last_checked->item == element) && !c->last_checked->deleted) {
			lock(c, c->last_checked);
			return 1;
		}
	}

	current = c->first;
	while(current) {
		
		if((element == current->item) && !current->deleted) {
			lock(c, current);
			return 1;
		}

		current = current->next;
	}

	return 0;
}

/* return 1 if the item was destroyed due to its unlocking */
unsigned int collection_unlock(struct collection *c, void *element) {
	struct collection_item *current;

	if(!c) {
		COLLECTION_DBG("collection_unlock: c == NULL");
		return 0;
	}

	if(c->deleted) {
		COLLECTION_DBG("collection_unlock: collection is deleted");
		return 0;
	}

	if(c->last_checked) {
		if(c->last_checked->item == element) {
			return unlock(c, c->last_checked);
		}
	}

	current = c->first;
	while(current) {
		
		if(element == current->item) {
			return unlock(c, current);
		}

		current = current->next;
	}

	COLLECTION_DBG("collection_unlock: %08x was nowhere to be unlocked... are you sure of your code?", (int)element);

	return 0;
}

void collection_debug(struct collection *c) {
	struct collection_item *current;

	if(!c) {
		COLLECTION_DBG("collection_iterate: c == NULL");
		return;
	}

	COLLECTION_DBG("collection_debug: starting");

	COLLECTION_DBG("ordered: %u", c->ordered);
	COLLECTION_DBG("count: %u", c->count);
	COLLECTION_DBG("locked: %u", c->locked);
	COLLECTION_DBG("deleted: %u", c->deleted);
	COLLECTION_DBG("first: %08x", (int)c->first);

	current = c->first;
	while(current) {

		COLLECTION_DBG("%08x: locked: %u", (int)current, (int)current->locked);
		COLLECTION_DBG("%08x: deleted: %u", (int)current, (int)current->deleted);
		COLLECTION_DBG("%08x: item: %08x", (int)current, (int)current->item);
		COLLECTION_DBG("%08x: next: %08x", (int)current, (int)current->next);

		current = current->next;
	}

	COLLECTION_DBG("collection_debug: over");

	return;
}

/* iterate the collection */
/* return 1 if the iteration was completed */
unsigned int collection_iterate(struct collection *c, unsigned int (*callback)(struct collection *c, void *item, void *param), void *param) {
	struct collection_item *current, *next;
	unsigned int ret;

	if(!c) {
		COLLECTION_DBG("collection_iterate: c == NULL");
		return 0;
	}

	if(c->deleted) {
		COLLECTION_DBG("collection_iterate: collection is deleted");
		return 0;
	}

	c_lock(c);

	current = c->first;
	while(current) {
		if(!current->deleted) {

			lock(c, current);

			c->last_checked = current;
			ret = (*callback)(c, current->item, param);
			next = current->next;

			unlock(c, current);

			if(c->deleted) {
				COLLECTION_DBG("collection_iterate: deleted during iteration.");
				break;
			}

			if(!ret) {
				c_unlock(c);
				return 0;
			}

			current = next;
		} else {
			/* skip if already deleted */
			current = current->next;
		}
	}

	c_unlock(c);

	return 1;
}

/* iterate the collection */
/* return the element chosen by the matcher */
void *collection_match(struct collection *c, unsigned int (*callback)(struct collection *c, void *item, void *param), void *param) {
	struct collection_item *current, *next;
	unsigned int ret;

	if(!c) {
		COLLECTION_DBG("collection_match: c == NULL");
		return NULL;
	}

	if(c->deleted) {
		COLLECTION_DBG("collection_match: collection is deleted");
		return NULL;
	}

	c_lock(c);

	current = c->first;
	while(current) {
		if(!current->deleted) {

			lock(c, current);

			c->last_checked = current;
			ret = (*callback)(c, current->item, param);
			next = current->next;

			if(unlock(c, current)) {
				/* deleted during unlock: bad, bad, bad */
				COLLECTION_DBG("collection_match: deleting items while matching is bad behaviour.");
			}

			if(c->deleted) {
				COLLECTION_DBG("collection_match: collection was deleted while matching");
				break;
			}

			if(ret) {
				c_unlock(c);
				return current->item;
			}

			current = next;
		} else {
			/* skip if already deleted */
			current = current->next;
		}
	}

	c_unlock(c);

	return NULL;
}

static void collection_order(struct collection *c) {
	struct collection_item *current;
	unsigned long long int uid = 1;

	current = c->first;
	while(current) {
		current->uid = uid++;
		current = current->next;
	}

	c->ordered = 1;

	return;
}

/* get the next item from a uid and return the uid of the returned item */
/* uid must be 0 on the first call */
/* CANNOT BE USED along with collection_add_first at the same time */
void *collection_next(struct collection *c, unsigned int *uid) {
	struct collection_item *current;

	if(!c) {
		COLLECTION_DBG("collection_next: c == NULL");
		return NULL;
	}

	if(!uid) {
		COLLECTION_DBG("collection_next: uid == NULL");
		return NULL;
	}

	if(c->deleted) {
		COLLECTION_DBG("collection_next: collection is deleted");
		return NULL;
	}

	/* order the collection if it's not ordered */
	if(!c->ordered) {
		collection_order(c);
	}

	if(c->last_checked && (c->last_checked->uid <= *uid)) {
		current = c->last_checked;
	} else {
		current = c->first;
	}

	while(current) {
		if((current->uid > *uid) && !current->deleted) {
			c->last_checked = current;
			*uid = current->uid;
			return current->item;
		}
		current = current->next;
	}

	return NULL;
}

void *collection_first(struct collection *c) {
	struct collection_item *current;

	if(!c) {
		COLLECTION_DBG("collection_first: c == NULL");
		return NULL;
	}
	if(c->deleted) {
		COLLECTION_DBG("collection_first: collection is deleted");
		return NULL;
	}

	current = c->first;
	while(current) {
		if(!current->deleted) {
			c->last_checked = current;
			return current->item;
		}
		current = current->next;
	}

	return NULL;
}
