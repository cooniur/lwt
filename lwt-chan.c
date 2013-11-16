//
//  lwt-chan.c
//  lwt
//
//  Created by Tongliang Liu on 11/15/13.
//  Copyright (c) 2013 cooniur. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "lwt.h"
#include "lwt-chan.h"
#include "dlinkedlist.h"
#include "ring_queue.h"

#define DEFAULT_SND_BUFFER_SIZE	(10)

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
	 Sender queue
	 */
	dlinkedlist_t *s_queue;
	
	/**
	 Indicates whether a receiver is blocked on this channel
	 */
	int rcv_blocked;
	
	/**
	 The receiver thread
	 */
	lwt_t receiver;

	/**
	 Sender's data buffer
	 */
	ring_queue_t *snd_buffer;
	
	/**
	 Sender's data buffer size;
	 */
	size_t snd_buffer_size;
	
	/**
	 Number of sending threads
	 */
	size_t snd_cnt;
	
	/**
	 Sender list
	 */
	dlinkedlist_t *s_list;
	
};

void __lwt_snd_blocked(lwt_t sndr, lwt_chan_t c, void *data);
void __lwt_snd_buffered(lwt_chan_t c, void *data);

void* __lwt_rcv_blocked(lwt_chan_t c);
void* __lwt_rcv_buffered(lwt_chan_t c);

int __lwt_chan_use_buffer(lwt_chan_t c);
void __lwt_chan_set_name(lwt_chan_t c, const char *name);

void __lwt_chan_init_snd_buffer(lwt_chan_t c, size_t sz);
void __lwt_chan_free_snd_buffer(lwt_chan_t c);
int __lwt_chan_try_to_free(lwt_chan_t *c);

int __lwt_chan_use_buffer(lwt_chan_t c)
{
	return c->snd_buffer_size > 0;
}

void __lwt_chan_set_name(lwt_chan_t c, const char *name)
{
	if (name)
	{
		size_t sz = strlen(name) + 1;
		c->name = calloc(sz, sizeof(char));
		strncpy(c->name, name, sz-1);
		c->name[sz-1] = '\0';
	}
	else
	{
		c->name = calloc(1, sizeof(char));
		c->name[0] = '\0';
	}
}

void __lwt_chan_init_snd_buffer(lwt_chan_t c, size_t sz)
{
	c->snd_buffer_size = sz;
	if (sz == 0)
		c->snd_buffer = NULL;
	else
	{
		c->snd_buffer = ring_queue_init(sz);
	}
}

void __lwt_chan_free_snd_buffer(lwt_chan_t c)
{
	if (c->snd_buffer)
		ring_queue_free(&c->snd_buffer);
}

int __lwt_chan_try_to_free(lwt_chan_t *c)
{
	if (!((*c)->receiver) && dlinkedlist_size((*c)->s_list) == 0)
	{
		__lwt_chan_free_snd_buffer(*c);
		free((*c)->name);
		free(*c);
		*c = NULL;
		return 1;
	}
	else
		return 0;
}


lwt_chan_t lwt_chan(size_t sz, const char *name)
{
	lwt_chan_t chan = malloc(sizeof(struct __lwt_chan_t__));
	chan->s_list = dlinkedlist_init();
	chan->s_queue = dlinkedlist_init();
	chan->snd_data = NULL;
	chan->rcv_blocked = 0;
	chan->receiver = lwt_current();
	
	__lwt_chan_set_name(chan, name);
	__lwt_chan_init_snd_buffer(chan, sz);
	
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
	
	return __lwt_chan_try_to_free(c);
}

const char *lwt_chan_get_name(lwt_chan_t c)
{
	if (!c)
		return NULL;
	
	return c->name;
}

int lwt_snd(lwt_chan_t c, void *data)
{
	assert(data);
	assert(c);
	
	// No receiver exists, returns -1.
	if (!c->receiver)
		return -1;
	
	// Forbit receiver from sending to itself
	lwt_t sndr = lwt_current();
	if (c->receiver == sndr)
		return -2;
	
	// If sndr has not sent on this channel before, add it to sender list
	if (!dlinkedlist_find(c->s_list, sndr))
		dlinkedlist_add(c->s_list, dlinkedlist_element_init(sndr));

	if (__lwt_chan_use_buffer(c))
	{
		// Send data buffered
		__lwt_snd_buffered(c, data);
	}
	else
	{
		// Send data blocked
		__lwt_snd_blocked(sndr, c, data);
	}

	return 0;
}

void __lwt_snd_blocked(lwt_t sndr, lwt_chan_t c, void *data)
{
	// Add sndr to sender queue
	dlinkedlist_add(c->s_queue, dlinkedlist_element_init(sndr));
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
}

void __lwt_snd_buffered(lwt_chan_t c, void *data)
{
	assert(c->snd_buffer);
	
	while (ring_queue_full(c->snd_buffer))
		lwt_yield(NULL);
	
	ring_queue_inqueue(c->snd_buffer, data);
}

void *lwt_rcv(lwt_chan_t c)
{
	assert(c);
	
	if (__lwt_chan_use_buffer(c))
	{
		return __lwt_rcv_buffered(c);
	}
	else
	{
		return __lwt_rcv_blocked(c);
	}
}

void* __lwt_rcv_blocked(lwt_chan_t c)
{
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

void* __lwt_rcv_buffered(lwt_chan_t c)
{
	assert(c->snd_buffer);
	
	// spinning if nobody is sending on this channel
	while (ring_queue_empty(c->snd_buffer))
		lwt_yield(NULL);
	
	void* data = ring_queue_dequeue(c->snd_buffer);
	
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
