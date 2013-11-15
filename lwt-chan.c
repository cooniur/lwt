//
//  lwt-chan.c
//  lwt
//
//  Created by Tongliang Liu on 11/15/13.
//  Copyright (c) 2013 cooniur. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include "lwt.h"
#include "lwt-chan.h"

struct __lwt_chan_t__
{
	/**
	 Channel's name
	 */
	char *name;
	
	/**
	 Sender's data
	 */
	void *snd_data;
	
	/**
	 Number of sending threads
	 */
	size_t snd_cnt;
	
	/**
	 Sender queue
	 */
	dlinkedlist_t *s_queue;
	
	/**
	 Sender list
	 */
	dlinkedlist_t *s_list;
	
	/**
	 */
	int rcv_blocked;
	
	/**
	 */
	lwt_t receiver;
};

lwt_chan_t lwt_chan(const char *name)
{
	lwt_chan_t chan = malloc(sizeof(struct __lwt_chan_t__));
	if (name)
	{
		size_t sz = strlen(name) + 1;
		chan->name = calloc(sz, sizeof(char));
		strncpy(chan->name, name, sz-1);
		chan->name[sz-1] = '\0';
	}
	else
	{
		chan->name = malloc(sizeof(char));
		chan->name = '\0';
	}
	chan->s_list = dlinkedlist_init();
	chan->s_queue = dlinkedlist_init();
	chan->snd_data = NULL;
	
	chan->rcv_blocked = 0;
	chan->receiver = lwt_current();
	
	return chan;
}

int lwt_chan_deref(lwt_chan_t *c)
{
	if (!c || !(*c))
		return -1;
	
	lwt_t cur_lwt = lwt_current();
	if ((*c)->receiver == cur_lwt)
		(*c)->receiver = NULL;
	else
	{
		dlinkedlist_element_t *e = dlinkedlist_find((*c)->s_list, cur_lwt);
		if (e)
			dlinkedlist_remove((*c)->s_list, e);
	}
	
	if (!((*c)->receiver) && dlinkedlist_size((*c)->s_list) == 0)
	{
		free((*c)->name);
		free(*c);
		*c = NULL;
		return 1;
	}
	else
		return 0;
}

const char *lwt_chan_get_name(lwt_chan_t c)
{
	if (!c)
		return NULL;
	
	return c->name;
}

int lwt_snd(lwt_chan_t c, void *data)
{
	assert(data != NULL);
	assert(c);
	
	// No receiver exists, returns -1.
	if (!c->receiver)
		return -1;
	
	// Forbit receiver from sending to itself
	lwt_t sndr = lwt_current();
	if (c->receiver == sndr)
		return -2;
	
	// Add sndr to sender queue
	dlinkedlist_add(c->s_queue, dlinkedlist_element_init(sndr));
	
	// If sndr has not sent on this channel before, add it to sender list
	if (!dlinkedlist_find(c->s_list, sndr))
		dlinkedlist_add(c->s_list, dlinkedlist_element_init(sndr));
	
	// spinning if it is not my turn
	while (dlinkedlist_first(c->s_queue)->data != sndr)
		lwt_yield(NULL);
	
	// now it is my turn, set the data
	c->snd_data = data;
	
	// spinning if it is my turn but no receiver is blocked on this channel
	while (!c->rcv_blocked)
		lwt_yield(NULL);
	
	// Now the receiver is ready to receive, yield to it
	lwt_yield(c->receiver);
	
	return 0;
}

void *lwt_rcv(lwt_chan_t c)
{
	assert(c);
	
	// spinning if nobody is sending on this channel
	while (dlinkedlist_size(c->s_queue) == 0)
	{
		c->rcv_blocked = 1;
		lwt_yield(NULL);
	}
	
	// now the data has been sent via the channel, get it, and set receiver's status to non-blocked
	void *data = c->snd_data;
	c->snd_data = NULL;
	c->rcv_blocked = 0;
	
	// remove sender from the sender queue
	dlinkedlist_element_t *e = dlinkedlist_first(c->s_queue);
	dlinkedlist_remove(c->s_queue, e);
	dlinkedlist_element_free(&e);
	
	return data;
}

int lwt_snd_chan(lwt_chan_t c, lwt_chan_t sc)
{
	return lwt_snd(c, sc);
}

lwt_chan_t lwt_rcv_chan(lwt_chan_t c)
{
	return lwt_rcv(c);
}
