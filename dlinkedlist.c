//
//  dlinkedlist.c
//  lwt
//
//  Created by Tongliang Liu on 11/8/13.
//  Copyright (c) 2013 cooniur. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "dlinkedlist.h"

struct __dlst_t__
{
	dlinkedlist_element_t* first;
	dlinkedlist_element_t* last;
	size_t size;
};

dlinkedlist_t* dlinkedlist_init()
{
	dlinkedlist_t* list = malloc(sizeof(dlinkedlist_t));
	if (!list)
		return NULL;
	
	list->first = list->last = NULL;
	list->size = 0;
	return list;
}

void dlinkedlist_free(dlinkedlist_t** list)
{
	if (list && *list)
	{
		(*list)->first = (*list)->last = NULL;
		(*list)->size = 0;
		free(*list);
		*list = NULL;
	}
}

dlinkedlist_element_t* dlinkedlist_element_init(void* data)
{
	dlinkedlist_element_t* e = malloc(sizeof(dlinkedlist_element_t));
	e->data = data;
	e->next = e->prev = NULL;
	return e;
}

void dlinkedlist_element_free(dlinkedlist_element_t **e)
{
	if (e && *e)
	{
		(*e)->prev = (*e)->next = NULL;
		(*e)->data = NULL;
		free(*e);
	}
}

dlinkedlist_element_t* dlinkedlist_first(dlinkedlist_t* list)
{
	if (!list)
		return NULL;
	
	return list->first;
}

dlinkedlist_element_t* dlinkedlist_last(dlinkedlist_t* list)
{
	if (!list)
		return NULL;
	
	return list->last;
}

size_t dlinkedlist_size(dlinkedlist_t* list)
{
	if (!list)
		return 0;

	return list->size;
}

int dlinkedlist_add(dlinkedlist_t* list, dlinkedlist_element_t* e)
{
	if (!list)
		return -1;
	
	if (!e)
		return -2;
	
	if (dlinkedlist_size(list) == 0)
	{
		list->first = e;
		list->last = e;
		e->prev = e;
		e->next = e;
	}
	else
	{
		list->last->next = e;
		e->prev = list->last;
		e->next = list->first;
		list->first->prev = e;
		list->last = e;
	}
	
	list->size++;

	return 0;
}

dlinkedlist_element_t* dlinkedlist_find(dlinkedlist_t* list, void* data)
{
	if (!list || !data)
		return NULL;

	if (dlinkedlist_size(list) == 0)
		return NULL;
	
	int c = 0;
	dlinkedlist_element_t* ret = NULL;
	dlinkedlist_foreach_element(e, list)
	{
		if (e->data == data)
		{
			ret = e;
			break;
		}
	}

	return ret;
}

int dlinkedlist_remove(dlinkedlist_t* list, dlinkedlist_element_t* e)
{
	if (!list)
		return -1;
	if (!e)
		return -2;
	
	e->prev->next = e->next;
	e->next->prev = e->prev;

	if (e == list->first)
		list->first = e->next;
	else if (e == list->last)
		list->last = e->prev;

	e->next = e->prev = NULL;
	
	list->size--;
	
	if (dlinkedlist_size(list) == 0)
		list->first = list->last = NULL;
	
	return 0;
}
