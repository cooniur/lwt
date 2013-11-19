//
//  lwt.c
//  lwt
//
//  Created by cooniur on 10/17/13.
//  Copyright (c) 2013 cooniur. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>

#include "lwt.h"
#include "ring_queue.h"
#include "dlinkedlist.h"

//#define _DEBUG_

/**
 Gets the offset of a field inside a struc
 */
#define LWT_STRUCT_OFFSET(Field) \
[Field] "i" (offsetof(struct __lwt_t__, Field))

/**
 Default size of the stack used by a lwt thread
 */
#define DEFAULT_LWT_STACK_SIZE	(1024 * 16)

/**
 Default TCB pool size
 */
#define TCB_POOL_SIZE (64)

/**
 Thread status enum
 */
enum __lwt_status_t__
{
	LWT_S_CREATED = 0,		// Thread is just created. Stack is empty
	LWT_S_READY,			// Thread is switched out, and ready to be switched to
	LWT_S_RUNNING,			// Thread is running
	LWT_S_BLOCKED,			// Thread is blocked and in wait queue
	LWT_S_FINISHED,			// Thread is finished and is ready to be joined
	LWT_S_DEAD				// Thread is joined and finally dead.
};

/**
 Thread counter
 */
struct __lwt_info_t__
{
	size_t num_runnable;
	size_t num_zombies;
	size_t num_blocked;
};


/**
 Thread Descriptor
 */
struct __lwt_t__
{
	/**
	 Stack Base Pointer
	 Offset: 0x0
	 */
	void* ebp;
	
	/**
	 Stack Pointer
	 Offset: 0x4
	 */
	void* esp;
	
	/**
	 Thread Entry Function Pointer
	 Offset: 0x8
	 */
	lwt_fn_t entry_fn;
	
	/**
	 Thread Entry Function Parameter Pointer
	 Offset: 0xc
	 */
	void* entry_fn_param;
	
	/**
	 Thread Return Value Pointer
	 Offset: 0x10
	 */
	void* return_val;
	
	/**
	 Thread status
	 Offset: 0x14
	 */
	lwt_status_t status;
	
	/**
	 Stack Memory Pointer by malloc
	 Offset: 0x18
	 */
	void* stack;
	
	/**
	 Thread ID
	 Offset: 0x1c
	 */
	int id;
	
	/**
	 Stack Size
	 Offset: 0x20
	 */
	size_t stack_size;
	
	/**
	 In which queue this lwt is
	 */
	struct __lwt_queue_t__* queue;
	
	/**
	 Points to the next thread descriptor
	 Offset: 0x24
	 */
	struct __lwt_t__* next;
	
	/**
	 Points to the previous thread descriptor
	 Offset: 0x28
	 */
	struct __lwt_t__* prev;
	
} __attribute__ ((aligned (16), packed));

/**
 Thread queue type
 */
struct __lwt_queue_t__
{
	struct __lwt_t__* head;
	struct __lwt_t__* tail;
	size_t size;
};

struct __lwt_queue_t__*		lwt_queue_init();
size_t						lwt_queue_size(struct __lwt_queue_t__* queue);
void						lwt_queue_inqueue(struct __lwt_queue_t__* queue, struct __lwt_t__* lwt);
struct __lwt_t__*			lwt_queue_dequeue(struct __lwt_queue_t__* queue);
void						lwt_queue_remove(struct __lwt_queue_t__* queue, struct __lwt_t__* lwt);
struct __lwt_t__*			lwt_queue_peek(struct __lwt_queue_t__* queue);

struct __lwt_queue_t__* lwt_queue_init()
{
	struct __lwt_queue_t__* queue = malloc(sizeof(struct __lwt_queue_t__));
	queue->head = NULL;
	queue->tail = NULL;
	queue->size = 0;
	return queue;
}

size_t lwt_queue_size(struct __lwt_queue_t__* queue)
{
	return queue->size;
}

int lwt_queue_empty(struct __lwt_queue_t__* queue)
{
	return lwt_queue_size(queue) == 0;
}

void lwt_queue_insert_before(struct __lwt_queue_t__* queue, struct __lwt_t__* victim, struct __lwt_t__* lwt)
{
	assert(queue == victim->queue);
	assert(queue == lwt->queue);

	victim->prev->next = lwt;
	lwt->prev = victim->prev;

	victim->prev = lwt;
	lwt->next = victim;

	if (victim == queue->head)
		queue->head = lwt;

	queue->size++;
}

void lwt_queue_inqueue(struct __lwt_queue_t__* queue, struct __lwt_t__* lwt)
{
	if (lwt_queue_empty(queue))
	{
		lwt->next = lwt;
		lwt->prev = lwt;
		queue->head = lwt;
		queue->tail = lwt;
	}
	else
	{
		lwt->prev = queue->tail;
		lwt->next = queue->head;

		queue->tail->next = lwt;
		queue->head->prev = lwt;

		queue->tail = lwt;
	}
	lwt->queue = queue;
	queue->size++;
}

struct __lwt_t__* lwt_queue_dequeue(struct __lwt_queue_t__* queue)
{
	struct __lwt_t__* ret = queue->head;
	
	if (queue->size > 0)
	{
		queue->head = queue->head->next;
		queue->tail->next = queue->head;
		queue->head->prev = queue->tail;
	}
	else
		queue->head = queue->tail = NULL;
	
	ret->prev = ret->next = NULL;
	ret->queue = NULL;

	queue->size--;
	return ret;
}

void lwt_queue_remove(struct __lwt_queue_t__* queue, struct __lwt_t__* lwt)
{
	assert(lwt->queue == queue);
	assert(!lwt_queue_empty(queue));
	assert(lwt->prev);
	assert(lwt->next);
	
	queue->size--;
	if (lwt_queue_empty(queue))
		queue->head = queue->tail = NULL;
	else
	{
		lwt->prev->next = lwt->next;
		lwt->next->prev = lwt->prev;
	}

	if (lwt == queue->head)
		queue->head = lwt->next;
	else if (lwt == queue->tail)
		queue->tail = lwt->prev;

	lwt->prev = lwt->next = NULL;
}

struct __lwt_t__* lwt_queue_peek(struct __lwt_queue_t__* queue)
{
	if (queue)
		return queue->head;
	else
		return NULL;
}

// =======================================================

/**
 The Run Queue
 run_q.head always points to the current thread
 */
struct __lwt_queue_t__ __run_q = {NULL, NULL, 0};

/**
 The Wait Queue
 threads that are blocked will be added into this queue
 */
struct __lwt_queue_t__ __wait_q = {NULL, NULL, 0};

/**
 The Dead Queue: recycled TCBs
 */
struct __lwt_queue_t__ __dead_q = {NULL, NULL, 0};

/**
 The main thread TCB
 */
lwt_t __main_thread = NULL;

/**
 Stores the next available thread id #
 */
int __lwt_threadid = 1;

/**
 Stores thread info
 */
struct __lwt_info_t__ __lwt_info = {1, 0, 0};

// =======================================================
/**
 A new thread's entry point
 Calls __lwt_start (in assembly)
 */
void __lwt_start(lwt_fn_t fn, void* data);
static __attribute__ ((noinline)) void __lwt_dispatch(lwt_t next, lwt_t current);

static void __lwt_block();
static void __lwt_wakeup(lwt_t blocked_lwt);
static void __lwt_wakeup_all();

static lwt_t	__lwt_init_lwt();
static void		__lwt_init_tcb_pool();
static int		__lwt_get_next_threadid();
static void		__lwt_main_thread_init();

static inline void __lwt_create_init_stack(lwt_t lwt, lwt_fn_t fn, void* data);

extern void __lwt_trampoline();
// =======================================================


#ifndef _DEBUG_
#define debug_showqueue(q, name)
#else
void __lwt_debug_showqueue(struct __lwt_queue_t__* queue, const char* queue_name)
{
	lwt_t cur = queue->head;
	printf("%s: ", queue_name);
	size_t i = 0;
	while (cur && i < queue->size)
	{
		printf("%p, ", cur);
		cur = cur->next;
		i++;
	}
	printf("\n");
}

#define debug_showqueue(q, name) \
	__lwt_debug_showqueue(q, name)
#endif


/**
 Initialize TCB pool
 */
static void __lwt_init_tcb_pool()
{
	int i;
	for (i=0; i<TCB_POOL_SIZE; i++)
	{
		lwt_queue_inqueue(&__dead_q, __lwt_init_lwt());
	}
}

lwt_t __lwt_init_lwt()
{
	lwt_t new_lwt = (struct __lwt_t__*)malloc(sizeof(struct __lwt_t__));
	new_lwt->id = __lwt_get_next_threadid();
	new_lwt->status = LWT_S_DEAD;
	new_lwt->prev = NULL;
	new_lwt->next = NULL;
	new_lwt->entry_fn = NULL;
	new_lwt->entry_fn_param = NULL;
	new_lwt->return_val = NULL;
	
	// creates stack
	new_lwt->stack_size = DEFAULT_LWT_STACK_SIZE;
	new_lwt->stack = malloc(sizeof(void) * new_lwt->stack_size);
	new_lwt->ebp = new_lwt->stack + new_lwt->stack_size;
	new_lwt->esp = new_lwt->ebp;
	return new_lwt;
}

/**
 Gets the next available thread id #
 */
static int __lwt_get_next_threadid()
{
	return __lwt_threadid++;
}

/**
 Switches from the "current" thread to the "next" thread
 Must be noinline function
 */
static void __lwt_dispatch(lwt_t next, lwt_t current)
{
	__asm__ __volatile__ (
						  // Save all registers to stack
						  "pushal \n\t"							// save the current thread's registers to stack
						  
						  // Save stack pointer and base pointer
						  "leal %c[ebp](%0), %%ebx \n\t"		// %ebx = &(current->ebp)
						  "movl %%ebp, (%%ebx) \n\t"			// current->ebp = %ebp
						  "movl %%esp, 0x4(%%ebx) \n\t"			// current->esp = %esp
						  :
						  : "r" (current),
						  LWT_STRUCT_OFFSET(ebp)
						  :
						  );

	__asm__ __volatile__ (
						  // Restore stack pointer and base pointer
						  "leal %c[ebp](%0), %%ecx \n\t"		// %ecx = &(next->ebp)
						  "movl (%%ecx), %%ebp \n\t"			// %ebp = next->ebp
						  "movl 0x4(%%ecx), %%esp \n\t"			// %esp = next->esp

						  // Restore registers
						  "popal \n\t"							// resume the next thread
						  :
						  : "r" (next),
						  LWT_STRUCT_OFFSET(ebp)
						  :
						  );

}

/**
 Puts the main thread into the run queue
 */
static void __lwt_main_thread_init()
{
	if (__main_thread)
		return;
		
	__main_thread = (struct __lwt_t__*)malloc(sizeof(struct __lwt_t__));
	__main_thread->id = 0;
	__main_thread->status = LWT_S_RUNNING;
	__main_thread->stack = NULL;
	__main_thread->next = NULL;
	
//	__lwt_q_inqueue(&__run_q, __main_thread);
	lwt_queue_inqueue(&__run_q, __main_thread);
}

/**
 Thread entry, called by __lwt_trampoline in assembly
 This function will return to lwt_die() implicitly.
 */
void __lwt_start(lwt_fn_t fn, void* data)
{
	void* ret = fn(data);
	
	// prepare to return to lwt_die()
	__asm__ __volatile__ (
						  // 0x4(%ebp) is the returning address of __lwt_start(), currently pointing to lwt_die()
						  // 0x8(%ebp) will be the returning address of lwt_die()
						  // 0xc(%ebp) will be the address of the first parameter used by lwt_die()
						  "leal (%0), %%ebx \n\t"				// %ebx = ret
						  "movl %%ebx, 0xc(%%ebp) \n\t"			// 0xc(%ebp) = ret
						  :
						  : "r" (ret)
						  :
						  );
}

static inline void __lwt_create_init_stack(lwt_t lwt, lwt_fn_t fn, void* data)
{
	unsigned int* esp = lwt->esp;
	void* lwt_start_stack;

//	assert(lwt->stack);
	lwt->ebp = lwt->stack + lwt->stack_size;
	lwt->esp = lwt->ebp;
	
	*esp = data;
	esp--;
	*esp = fn;
	esp--;
	*esp = &lwt_die;
	esp--;
	*esp = &__lwt_trampoline;
	esp--;
	*esp = lwt->ebp;

	// original esp before pushal
	lwt_start_stack = esp;
	esp--;
	
	// pushal: eax, ebx, ecx, edx, esp, ebp, esi, edi
	*esp = 0xB;
	esp--;
	*esp = 0x17;
	esp--;
	*esp = 0x13;
	esp--;
	*esp = 0x55;
	esp--;

	*esp = lwt_start_stack;
	esp--;
	*esp = lwt->ebp;
	esp--;

	*esp = 0x6;
	esp--;
	*esp = 0xA;

	lwt->esp = esp;
	lwt->status = LWT_S_READY;
}

void __lwt_block()
{
	lwt_t current_lwt = lwt_queue_dequeue(&__run_q);
	current_lwt->status = LWT_S_BLOCKED;
	lwt_queue_inqueue(&__wait_q, current_lwt);
	
	lwt_t next_lwt = lwt_queue_peek(&__run_q);
	next_lwt->status = LWT_S_RUNNING;
	
	__lwt_dispatch(next_lwt, current_lwt);
}

static void __lwt_wakeup(lwt_t blocked_lwt)
{
	if (blocked_lwt->status == LWT_S_BLOCKED)
	{
		lwt_queue_remove(&__wait_q, blocked_lwt);
		blocked_lwt->status = LWT_S_READY;
		lwt_queue_inqueue(&__run_q, blocked_lwt);
	}
}

void __lwt_wakeup_all()
{
	while (lwt_queue_size(&__wait_q) > 0)
	{
		lwt_t blocked_lwt = lwt_queue_dequeue(&__wait_q);
		blocked_lwt->status = LWT_S_READY;
		lwt_queue_inqueue(&__run_q, blocked_lwt);
	}
}

// =======================================================

/**
 Creates a lwt thread, with the entry function pointer fn,
 and the parameter pointer data used by fn
 Returns lwt_t type
 */
lwt_t lwt_create(lwt_fn_t fn, void* data)
{
	__lwt_main_thread_init();
	
	if (lwt_queue_size(&__dead_q) == 0)
		__lwt_init_tcb_pool();
		
	lwt_t new_lwt = lwt_queue_dequeue(&__dead_q);
	new_lwt->id = __lwt_get_next_threadid();
	new_lwt->status = LWT_S_CREATED;
	new_lwt->prev = NULL;
	new_lwt->next = NULL;
	new_lwt->entry_fn = fn;
	new_lwt->entry_fn_param = data;
	new_lwt->return_val = NULL;
	new_lwt->queue = NULL;
	
	__lwt_create_init_stack(new_lwt, fn, data);
	
	lwt_queue_inqueue(&__run_q, new_lwt);
	
	debug_showqueue(&__run_q, "run queue");
	
	__lwt_info.num_runnable++;
	
	return new_lwt;
}

/**
 Yields to the next available thread
 */
void lwt_yield(lwt_t target)
{
	debug_showqueue(&__run_q, "run queue");

	lwt_t current_lwt = lwt_queue_dequeue(&__run_q);
	lwt_queue_inqueue(&__run_q, current_lwt);
	current_lwt->status = LWT_S_READY;

	debug_showqueue(&__run_q, "run queue");

	if (target)
	{
		if (target->status == LWT_S_BLOCKED)
		{
			lwt_queue_remove(&__wait_q, target);
			lwt_queue_insert_before(&__run_q, lwt_queue_peek(&__run_q), target);
		}
		else if (target->status == LWT_S_READY)
		{
			lwt_queue_remove(&__run_q, target);
			lwt_queue_insert_before(&__run_q, lwt_queue_peek(&__run_q), target);
		}
	}
	
	lwt_t next_lwt = lwt_queue_peek(&__run_q);
	next_lwt->status = LWT_S_RUNNING;

	__lwt_dispatch(next_lwt, current_lwt);
}

/**
 Joins a specified thread and waits for its termination.
 The pointer to the returned value will be passed via retval_ptr
 Returns -1 if fails to join; otherwise, 0
 */
int lwt_join(lwt_t lwt, void** retval_ptr)
{
	if (lwt_current() == lwt)
		return -1;

	if (lwt->status == LWT_S_DEAD)
		return -1;

	// Spinning until the joining thread finishes.
	__lwt_info.num_runnable--;
	__lwt_info.num_blocked++;
	while(lwt->status != LWT_S_FINISHED)
		__lwt_block();

	__lwt_info.num_zombies--;
	__lwt_info.num_blocked--;
	__lwt_info.num_runnable++;
	
	lwt->status = LWT_S_DEAD;
	if (retval_ptr)
		*retval_ptr = lwt->return_val;

	lwt_queue_inqueue(&__dead_q, lwt);
	return 0;
}

/**
 Kill the current thread.
 Return value is passed by data
 */
void lwt_die(void* data)
{
	lwt_t lwt_finished = lwt_queue_dequeue(&__run_q);
	lwt_finished->status = LWT_S_FINISHED;
	lwt_finished->return_val = data;

	lwt_finished->next = NULL;
	lwt_finished->entry_fn = NULL;
	lwt_finished->entry_fn_param = NULL;

	__lwt_info.num_zombies++;
	__lwt_info.num_runnable--;

	if (lwt_queue_size(&__run_q) == 0)
		__lwt_wakeup_all();

	lwt_t next_lwt = lwt_queue_peek(&__run_q);
	next_lwt->status = LWT_S_RUNNING;

	__lwt_wakeup_all();

	__lwt_dispatch(next_lwt, lwt_finished);
}

/**
 Gets the current thread lwt_t
 */
lwt_t lwt_current()
{
	return lwt_queue_peek(&__run_q);
}

/**
 Gets the thread id of a specified thread.
 Returns -1 if the thread not exists
 */
int lwt_id(lwt_t lwt)
{
	if (!lwt)
		return -1;
	
	return lwt->id;
}

size_t lwt_info(lwt_info_type_t type)
{
	switch (type) {
		case LWT_INFO_NTHD_RUNNABLE:
			return __lwt_info.num_runnable;
		case LWT_INFO_NTHD_BLOCKED:
			return __lwt_info.num_blocked;
		
		case LWT_INFO_NTHD_ZOMBIES:
		default:
			return __lwt_info.num_zombies;
	}
}

// ===================================================================
// lwt channel
// ===================================================================
struct __lwt_cgrp_t__
{
	
};

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
	dlinkedlist_t* s_queue;
	
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
	ring_queue_t* snd_buffer;
	
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
	dlinkedlist_t* s_list;
	
	/**
	 Tag for the channel
	 */
	void* tag;
	
	/**
	 Belonging Channel group
	 */
	struct __lwt_cgrp_t__* grp;
};

void __lwt_snd_blocked(lwt_t sndr, lwt_chan_t c, void* data);
void __lwt_snd_buffered(lwt_chan_t c, void* data);

void* __lwt_rcv_blocked(lwt_chan_t c);
void* __lwt_rcv_buffered(lwt_chan_t c);

int __lwt_chan_use_buffer(lwt_chan_t c);
void __lwt_chan_set_name(lwt_chan_t c, const char* name);

void __lwt_chan_init_snd_buffer(lwt_chan_t c, size_t sz);
void __lwt_chan_free_snd_buffer(lwt_chan_t c);
int __lwt_chan_try_to_free(lwt_chan_t* c);

int __lwt_chan_use_buffer(lwt_chan_t c)
{
	return c->snd_buffer_size > 0;
}

void __lwt_chan_set_name(lwt_chan_t c, const char* name)
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
		ring_queue_free(&(c->snd_buffer));
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

void __lwt_snd_blocked(lwt_t sndr, lwt_chan_t c, void* data)
{
	// Add sndr to sender queue
	dlinkedlist_add(c->s_queue, dlinkedlist_element_init(sndr));
	// spinning if it is not my turn
	while (dlinkedlist_first(c->s_queue)->data != sndr)
		__lwt_block();
	
	// now it is my turn, set the data
	c->snd_data = data;
	
	// spinning if it is my turn but no receiver is blocked on this channel
	while (!c->rcv_blocked)
		__lwt_block();
	
	// Now the receiver is ready to receive, yield to it
	lwt_yield(c->receiver);
}

void __lwt_snd_buffered(lwt_chan_t c, void* data)
{
	assert(c->snd_buffer);
	
	while (ring_queue_full(c->snd_buffer))
		__lwt_block();
	
	ring_queue_inqueue(c->snd_buffer, data);
}

void* __lwt_rcv_blocked(lwt_chan_t c)
{
	// spinning if nobody is sending on this channel
	while (dlinkedlist_size(c->s_queue) == 0)
	{
		c->rcv_blocked = 1;
		__lwt_block();
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
	
	// spinning if buffer is empty
	while (ring_queue_empty(c->snd_buffer))
		__lwt_block();
	
	void* data = ring_queue_dequeue(c->snd_buffer);
	return data;
}

// =======================================================

lwt_chan_t lwt_chan(size_t sz, const char* name)
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

int lwt_chan_deref(lwt_chan_t* c)
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

const char* lwt_chan_get_name(lwt_chan_t c)
{
	if (!c)
		return NULL;
	
	return c->name;
}

int lwt_snd(lwt_chan_t c, void* data)
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

void* lwt_rcv(lwt_chan_t c)
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

int lwt_snd_chan(lwt_chan_t c, lwt_chan_t sc)
{
	return lwt_snd(c, sc);
}

lwt_chan_t lwt_rcv_chan(lwt_chan_t c)
{
	return lwt_rcv(c);
}

void* lwt_chan_mark_get(lwt_chan_t c)
{
	if (c)
		return c->tag;
	else
		return NULL;
}

void lwt_chan_mark_set(lwt_chan_t c, void* tag)
{
	if (!c)
		return;
	
	c->tag = tag;
}

lwt_cgrp_t lwt_cgrp()
{
	lwt_cgrp_t grp = malloc(sizeof(struct __lwt_cgrp_t__));
	return grp;
}

int lwt_cgrp_free(lwt_cgrp_t* grp)
{
	return 0;
}

int lwt_cgrp_add(lwt_cgrp_t grp, lwt_chan_t c)
{
	return 0;
}

int lwt_cgrp_rem(lwt_cgrp_t grp, lwt_chan_t c)
{
	return 0;
}

lwt_chan_t lwt_cgrp_wait(lwt_cgrp_t grp)
{
	return NULL;
}

