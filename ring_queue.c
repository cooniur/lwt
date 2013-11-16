//
//  ring_queue.c
//
//  Created by Tongliang Liu on 9/6/13.
//  Copyright (c) 2013 cooniur. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <assert.h>

#include "ring_queue.h"

// The ring queue struct
struct __ring_queue_t__
{
	void** buf;
	size_t head;
	size_t tail;
	size_t capacity;
};

ring_queue_t *ring_queue_init(size_t capacity)
{
	ring_queue_t *rq = malloc(sizeof(struct __ring_queue_t__));
	if (rq)
	{
		rq->capacity = capacity;
		rq->buf = malloc(sizeof(void*) * capacity);
		if (rq->buf)
			ring_queue_reset(rq);
		else
		{
			// allocate buffer failed
			free(rq);
			rq = NULL;
		}
	}
	return rq;
}

void ring_queue_free(ring_queue_t **rq)
{
	size_t i;
	if (rq && *rq)
	{
		for (i=0; i<(*rq)->capacity; i++)
		{
			free((*rq)->buf[i]);
		}
		free((*rq)->buf);
		free(*rq);
		*rq = NULL;
	}
}

void ring_queue_reset(ring_queue_t *rq)
{
	rq->head = rq->tail = 0;
}

size_t ring_queue_capacity(ring_queue_t *rq)
{
	return rq->capacity;
}

size_t ring_queue_size(ring_queue_t *rq)
{
	return (rq->tail + rq->capacity - rq->head) % rq->capacity;
}

int ring_queue_empty(ring_queue_t *rq)
{
	if (rq->head == rq->tail)
		return 1;
	else
		return 0;
}

int ring_queue_full(ring_queue_t *rq)
{
	if (((rq->tail + 1) % rq->capacity) == rq->head)
		return 1;
	else
		return 0;
}

int ring_queue_inqueue(ring_queue_t *rq, void* data)
{
	if (NULL == data)
		return -1;

	if (ring_queue_full(rq))
		return 0;
	
	rq->buf[rq->tail] = data;
	rq->tail = (rq->tail + 1) % rq->capacity;
	return 1;
}

void* ring_queue_dequeue(ring_queue_t *rq)
{
	if (ring_queue_empty(rq))
		return NULL;
	
	void* ret = rq->buf[rq->head];
	assert(ret != NULL);

	rq->head = (rq->head + 1) % rq->capacity;
	return ret;
}
