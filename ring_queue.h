//
//  ring_queue.h
//
//  Created by Tongliang Liu on 9/6/13.
//  Copyright (c) 2013 cooniur. All rights reserved.
//

#ifndef ws_ring_queue_h
#define ws_ring_queue_h

typedef struct __ring_queue_t__ ring_queue_t;

// Initialize a new ring queue with the specified capacity
ring_queue_t*	ring_queue_init(size_t capacity);
// Free a ring queue
void			ring_queue_free(ring_queue_t **rq);

// Get the capacity of the ring queue
size_t			ring_queue_capacity(ring_queue_t *rq);
// Get the number of existing elements in the ring queue
size_t			ring_queue_size(ring_queue_t *rq);

// Returns 1 if the queue is empty; otherwise, 0
int				ring_queue_empty(ring_queue_t *rq);
// Returns 1 if the queue is full; otherwise, 0
int				ring_queue_full(ring_queue_t *rq);

// Reset the ring queue (not free them)
void			ring_queue_reset(ring_queue_t *rq);
// Inqueue, returns 1 if succeeded; returns 0 if the queue is full; returns -1 if the data pointer is NULL
int				ring_queue_inqueue(ring_queue_t *rq, void* data);
// Dequeue, returns the pointer of the head element in the queue; returns NULL if the queue is empty
void*			ring_queue_dequeue(ring_queue_t *rq);

#endif
