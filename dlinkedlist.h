//
//  dlinkedlist.h
//  lwt
//
//  Created by Tongliang Liu on 11/8/13.
//  Copyright (c) 2013 cooniur. All rights reserved.
//

#ifndef lwt_dlinkedlist_h
#define lwt_dlinkedlist_h

typedef struct __dlst_t__ dlinkedlist_t;

typedef struct __dlst_el_t__ dlinkedlist_element_t;

struct __dlst_el_t__
{
	void *data;
	dlinkedlist_element_t *prev;
	dlinkedlist_element_t *next;
	
};

dlinkedlist_t *dlinkedlist_init();
void dlinkedlist_free(dlinkedlist_t **list);

dlinkedlist_element_t *dlinkedlist_element_init(void *data);
void dlinkedlist_element_free(dlinkedlist_element_t **e);

dlinkedlist_element_t *dlinkedlist_first(dlinkedlist_t *list);
dlinkedlist_element_t *dlinkedlist_last(dlinkedlist_t *list);
size_t dlinkedlist_size(dlinkedlist_t *list);

int dlinkedlist_add(dlinkedlist_t *list, dlinkedlist_element_t *e);
dlinkedlist_element_t *dlinkedlist_find(dlinkedlist_t *list, void *data);
int dlinkedlist_remove(dlinkedlist_t *list, dlinkedlist_element_t *e);

#define dlinkedlist_foreach_element(e, list) \
	size_t size = dlinkedlist_size(list); \
	dlinkedlist_element_t *(e) = (list)->first;	\
	for (size_t i = 0; i < size; (e) = (e)->next, i++)

#endif
