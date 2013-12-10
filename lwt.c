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
#include <execinfo.h>
#include <pthread.h>

#include "lwt.h"
#include "ring_queue.h"
#include "dlinkedlist.h"
#include "debug_print.h"

#define LWT_KTHD_LOCAL	__thread
#define LWT_KTHD_GLOBAL

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

#define __ATTR_ALWAYS_INLINE__ __attribute__((always_inline))

typedef struct __lwt_kthd_t__ lwt_kthd_t;

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
	
	/**
	 Flags
	 */
	lwt_flags_t flags;

	/**
	 Indicates who has joined this thread
	 */
	lwt_t joiner;
	
	/**
	 On which pthread the lwt is running
	 */
	lwt_kthd_t* kthd;

} __attribute__ ((aligned (16), packed));

/**
 Thread queue type
 */
struct __lwt_queue_t__
{
	struct __lwt_t__* head;
	size_t size;
	char name[8];
};

struct __lwt_cgrp_t__
{
	/**
	 Channels that have a specific event occurred in the group
		0: snd event queue
		1: rcv event queue
	 */
	dlinkedlist_t* event_queue[2];

	/**
	 lwts that are waiting for events to occur
		0: lwts waiting for snd event to happen
		1: lwts waiting for rcv event to happen
	 */
	dlinkedlist_t* wait_queue[2];
	
	/**
	 Stores lwts that listen to event on the specific direction
		0: lwts listening to snd event to happen
		1: lwts listening to rcv event to happen
	 */
	dlinkedlist_t* listeners[2];
	
	/**
	 Number of channels in this group
	 */
	size_t channel_num;
	
	/**
	 Total number of events that happened on this group
	 */
	size_t total_num_events;
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
	 Sender list
	 */
	dlinkedlist_t* s_list;
	
	/**
	 Tag for the channel
	 */
	void* tag;
	
	/**
	 Belonging Channel group
	 0: group that waits for snd event
	 1: group that waits for rcv event
	 */
	struct __lwt_cgrp_t__* grp[2];
		
	/**
	 Number of events in this channel
	 0: number of snd events happened on the channel
	 1: number of rcv events happened on the channel
	 */
	size_t events_num[2];
	
	/**
	 Indicates whether this channel is queued in event queue.
	 This flag makes sure that when doing a grouped buffered sending,
	 the channel only be added into the group's event queue only once.
	 0: snd event queued
	 1: rcv event queued
	 */
	int event_queued[2];
};

/**
 kernal thread Struct
 */
struct __lwt_kthd_t__
{
	pthread_t pthread_id;
	void* message_queue;
	pthread_mutex_t msg_queue_lock;
};

struct __lwt_kthd_entry_param_t__
{
	lwt_t lwt;
	lwt_fn_t fn;
	void* data;
	lwt_chan_t c;
	lwt_kthd_t* kthd;
};

enum __lwt_kthd_msg_op_t {
	LWT_MSG_YIELD,
	LWT_MSG_WAKEUP,
	LWT_MSG_BLOCK,
	LWT_MSG_SND_BLOCKED,
	LWT_MSG_SND_BUFFERED,
};

struct __lwt_kthd_msg_t__
{
	enum __lwt_kthd_msg_op_t op;
	lwt_t lwt;
	lwt_chan_t c;
	void* c_data;
};

LWT_KTHD_LOCAL struct __lwt_kthd_t__* __current_kthd = NULL;
// =======================================================
/**
 The Run Queue
 run_q.head always points to the current thread
 */
LWT_KTHD_LOCAL struct __lwt_queue_t__ __run_q = {NULL, 0, "r"};

/**
 The Wait Queue
 threads that are blocked will be added into this queue
 */
LWT_KTHD_LOCAL struct __lwt_queue_t__ __wait_q = {NULL, 0, "w"};

/**
 The zombie queue
 threads that have died but not joined will be added to this queue
 */
LWT_KTHD_LOCAL struct __lwt_queue_t__ __zombie_q = {NULL, 0, "z"};

/**
 The Dead Queue: recycled TCBs
 */
LWT_KTHD_LOCAL struct __lwt_queue_t__ __dead_q = {NULL, 0, "d"};

/**
 The main thread TCB
 */
LWT_KTHD_LOCAL lwt_t __main_thread = NULL;

/**
 Idling thread that handles message between pthreads
 */
LWT_KTHD_LOCAL lwt_t __idle_thread = NULL;

/**
 Stores the next available thread id #
 */
LWT_KTHD_GLOBAL int __lwt_threadid = 1;

// =======================================================

static struct __lwt_queue_t__*		lwt_queue_init();
static inline size_t				lwt_queue_size(struct __lwt_queue_t__* queue);
static inline int					lwt_queue_empty(struct __lwt_queue_t__* queue);
static inline void					lwt_queue_insert_before(struct __lwt_queue_t__* queue, struct __lwt_t__* victim, struct __lwt_t__* lwt);
static inline void					lwt_queue_inqueue(struct __lwt_queue_t__* queue, struct __lwt_t__* lwt);
static inline struct __lwt_t__*		lwt_queue_head_next(struct __lwt_queue_t__* queue);
static inline struct __lwt_t__*		lwt_queue_dequeue(struct __lwt_queue_t__* queue);
static inline void					lwt_queue_remove(struct __lwt_queue_t__* queue, struct __lwt_t__* lwt);
static inline struct __lwt_t__*		lwt_queue_peek(struct __lwt_queue_t__* queue);
static inline struct __lwt_t__*		lwt_queue_peek_tail(struct __lwt_queue_t__* queue);

void __lwt_stack_trace()
{
	void *array[10];
	size_t size;
	char **strings;
	size_t i;
	
	size = backtrace(array, 10);
	strings = backtrace_symbols(array, size);
	
	printf (">>>>>>>>> Obtained %zd stack frames.\n", size);
	
	for (i = 0; i < size; i++)
		printf (">>>>>>>>>> %s\n", strings[i]);
	
	free (strings);
	printf (">>>>>>>>> End of stacktrace.\n");
}

void __lwt_debug_showqueue(struct __lwt_queue_t__* queue)
{
	static int count = 0;
	lwt_t cur = queue->head;
	printf("cur_lwt: %p. ", lwt_current());
	printf("%s: ", queue->name);
	size_t i = 0;
	while (cur && i < queue->size)
	{
		printf("%p, ", cur);
		cur = cur->next;
		i++;
		if (count++ > 10)
			break;
	}
	printf("\n");
	count = 0;
}

#ifndef Q_DEBUG
#define debug_showqueue(q) ((void)0)
#define debug_stacktrace() ((void)0)
#else
#define debug_showqueue(q) \
	__lwt_debug_showqueue(q)
#define debug_stacktrace() \
	__lwt_stack_trace()
#endif

void lwt_show_queue(int q)
{
	switch (q)
	{
		case 1:
			debug_showqueue(&__run_q);
			break;

		case 2:
			debug_showqueue(&__wait_q);
			break;

		case 3:
			debug_showqueue(&__zombie_q);
	}
}

struct __lwt_queue_t__* lwt_queue_init()
{
	struct __lwt_queue_t__* queue = malloc(sizeof(struct __lwt_queue_t__));
	queue->head = NULL;
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
	if (!victim)
	{
		lwt_queue_inqueue(queue, lwt);
	}
	else
	{
		assert(queue == victim->queue);
		if (victim == queue->head)
			queue->head = lwt;

		victim->prev->next = lwt;
		lwt->prev = victim->prev;

		victim->prev = lwt;
		lwt->next = victim;
		queue->size++;
	}
}

void lwt_queue_inqueue(struct __lwt_queue_t__* queue, struct __lwt_t__* lwt)
{
	if (lwt_queue_empty(queue))
	{
		lwt->next = lwt;
		lwt->prev = lwt;
		queue->head = lwt;
	}
	else
	{
		lwt->prev = queue->head->prev;
		lwt->next = queue->head;

		queue->head->prev->next = lwt;
		queue->head->prev = lwt;
	}
	lwt->queue = queue;
	queue->size++;
}

struct __lwt_t__* lwt_queue_head_next(struct __lwt_queue_t__* queue)
{
	assert(queue);
	
	struct __lwt_t__* old_head = queue->head;
	queue->head = old_head->next;
	return old_head;
}


struct __lwt_t__* lwt_queue_dequeue(struct __lwt_queue_t__* queue)
{
	assert(queue);
	
	struct __lwt_t__* old_head = queue->head;
	queue->size--;

	if (queue->size > 0)
	{
		queue->head = queue->head->next;

		old_head->prev->next = queue->head;
		queue->head->prev = old_head->prev;
	}
	else
		queue->head = NULL;

	old_head->prev = old_head->next = NULL;
	old_head->queue = NULL;
	return old_head;
}

void lwt_queue_remove(struct __lwt_queue_t__* queue, struct __lwt_t__* lwt)
{
//	debug_showqueue(queue);
	// printf("%p->%p, %p\n", lwt, lwt->queue, queue);
	// printf("r: %p\n", &__run_q);

	assert(queue);
	assert(!lwt_queue_empty(queue));
	assert(lwt);
	assert(lwt->queue == queue);
	assert(lwt->prev);
	assert(lwt->next);

	if (lwt->queue != queue)
		return;
	
	if (lwt == queue->head)
	{
		lwt_queue_dequeue(queue);
		return;
	}
	
	lwt->prev->next = lwt->next;
	lwt->next->prev = lwt->prev;
	queue->size--;

	lwt->prev = lwt->next = NULL;
	lwt->queue = NULL;
}

struct __lwt_t__* lwt_queue_peek(struct __lwt_queue_t__* queue)
{
	if (!queue)
		return NULL;
	
	return queue->head;
}

struct __lwt_t__* lwt_queue_peek_tail(struct __lwt_queue_t__* queue)
{
	if (!queue)
		return NULL;
	
	return queue->head->prev;
}

// =======================================================
/**
 A new thread's entry point
 Calls __lwt_start (in assembly)
 */
void __lwt_start(lwt_fn_t fn, void* data, lwt_chan_t c);
static __attribute__ ((noinline)) void __lwt_dispatch(lwt_t next, lwt_t current);

static inline lwt_t __lwt_current_inline();

static void __lwt_block();
static void __lwt_block_target(lwt_t lwt);
static void __lwt_block_and_wakeup(lwt_t lwt);
static void __lwt_wakeup(lwt_t blocked_lwt);
static void __lwt_wakeup_all();

static lwt_t	__lwt_init_lwt();
static void		__lwt_init_tcb_pool();
static void		__lwt_main_thread_init();
static int		__lwt_get_next_threadid();

static inline void		__lwt_create_init_existing(lwt_t lwt, lwt_flags_t flags, lwt_fn_t fn, void* data, lwt_chan_t c);
static inline void		__lwt_create_init_stack(lwt_t lwt, lwt_fn_t fn, void* data, lwt_chan_t c);

static inline int		__lwt_flags_get_nojoin(lwt_t lwt);
static inline void		__lwt_flags_set_nojoin(lwt_t lwt);

static inline void		__lwt_snd_buffered(lwt_t sndr, lwt_chan_t c, void* data);
static inline void		__lwt_snd_buffered_do(lwt_t sndr, lwt_chan_t c, void* data);
static inline void*		__lwt_rcv_buffered(lwt_chan_t c);

static inline void		__lwt_snd_blocked(lwt_t sndr, lwt_chan_t c, void* data);
static inline void		__lwt_snd_blocked_do(lwt_t sndr, lwt_chan_t c, void* data);
static inline void*		__lwt_rcv_blocked(lwt_chan_t c);

static int __lwt_chan_use_buffer(lwt_chan_t c);
static void __lwt_chan_set_name(lwt_chan_t c, const char* name);

static void __lwt_chan_init_snd_buffer(lwt_chan_t c, size_t sz);
static void __lwt_chan_free_snd_buffer(lwt_chan_t c);
static int __lwt_chan_try_to_free(lwt_chan_t* c);

static inline void __lwt_chan_add_sndr(lwt_chan_t c, lwt_t sndr);

void* __lwt_kthd_entry(void* param);
void __lwt_kthd_idle();
static struct __lwt_kthd_t__* __lwt_kthd_init();

static void __lwt_kthd_wakeup(lwt_t blocked_lwt);
static void __lwt_kthd_yield(lwt_t target);

static struct __lwt_kthd_msg_t__*	__lwt_kthd_msg_init(enum __lwt_kthd_msg_op_t op, lwt_t lwt, lwt_chan_t c, void* c_data);
static void							__lwt_kthd_msg_inqueue(struct __lwt_kthd_t__* kthd, struct __lwt_kthd_msg_t__* msg);
static struct __lwt_kthd_msg_t__*	__lwt_kthd_msg_dequeue();

extern void __lwt_trampoline();
// =======================================================

int __lwt_flags_get_nojoin(lwt_t lwt)
{
	return lwt->flags & LWT_F_NOJOIN;
}

void __lwt_flags_set_nojoin(lwt_t lwt)
{
	lwt->flags = lwt->flags | LWT_F_NOJOIN;
}

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
	// creates stack
	new_lwt->stack_size = DEFAULT_LWT_STACK_SIZE;
	new_lwt->stack = malloc(sizeof(void) * new_lwt->stack_size);
	new_lwt->flags = LWT_F_NONE;
	return new_lwt;
}

/**
 Gets the next available thread id #
 */
static int __lwt_get_next_threadid()
{
	// TODO: use lock-free to do synchronization
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
void __lwt_main_thread_init()
{
	if (__main_thread)
		return;
		
	__main_thread = (struct __lwt_t__*)malloc(sizeof(struct __lwt_t__));
	__main_thread->id = 0;
	__main_thread->status = LWT_S_RUNNING;
	__main_thread->stack = NULL;
	__main_thread->kthd = __current_kthd;

	lwt_queue_inqueue(&__run_q, __main_thread);
}

/**
 Thread entry, called by __lwt_trampoline in assembly
 This function will return to lwt_die() implicitly.
 */
void __lwt_start(lwt_fn_t fn, void* data, lwt_chan_t c)
{
	void* ret = fn(data, c);
	
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

void __lwt_create_init_existing(lwt_t lwt, lwt_flags_t flags, lwt_fn_t fn, void* data, lwt_chan_t c)
{
	lwt->id = __lwt_get_next_threadid();
	lwt->status = LWT_S_READY;
	lwt->entry_fn = fn;
	lwt->entry_fn_param = data;
	lwt->flags = flags;
	lwt->joiner = NULL;
	lwt->kthd = __current_kthd;
	
	__lwt_create_init_stack(lwt, fn, data, c);
	
	lwt_queue_inqueue(&__run_q, lwt);
}

void __lwt_create_init_stack(lwt_t lwt, lwt_fn_t fn, void* data, lwt_chan_t c)
{
	void* lwt_start_stack;

	lwt->ebp = lwt->stack + lwt->stack_size;
	lwt->esp = lwt->ebp;
	unsigned int* esp = lwt->esp;
	
	*esp = c;
	esp--;
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
}

void __lwt_block()
{
	lwt_t current_lwt = lwt_queue_dequeue(&__run_q);
	current_lwt->status = LWT_S_BLOCKED;
	lwt_queue_inqueue(&__wait_q, current_lwt);
	
	lwt_t next_lwt = lwt_queue_peek(&__run_q);
	if (!next_lwt)
	{
		next_lwt = __idle_thread;
		lwt_queue_remove(&__wait_q, next_lwt);
		lwt_queue_inqueue(&__run_q, next_lwt);
	}
	next_lwt->status = LWT_S_RUNNING;

	__lwt_dispatch(next_lwt, current_lwt);
}

void __lwt_block_target(lwt_t lwt)
{
	if (lwt->status == LWT_S_BLOCKED)
		return;
	
	if (lwt == __lwt_current_inline())
		__lwt_block();
	else
	{
		lwt_queue_remove(&__run_q, lwt);
		lwt->status = LWT_S_BLOCKED;
		lwt_queue_inqueue(&__wait_q, lwt);
	}
}

void __lwt_block_and_wakeup(lwt_t lwt)
{
	lwt_t current_lwt = lwt_queue_dequeue(&__run_q);
	current_lwt->status = LWT_S_BLOCKED;
	lwt_queue_inqueue(&__wait_q, current_lwt);

	lwt_t next_lwt;
	// the lwt is on the same kernal thread
	if (lwt->kthd == __current_kthd)
	{
		if (lwt->status == LWT_S_BLOCKED)
			lwt_queue_remove(&__wait_q, lwt);
		else
			lwt_queue_remove(&__run_q, lwt);
		
		lwt_queue_insert_before(&__run_q, lwt_queue_peek(&__run_q), lwt);
		lwt->status = LWT_S_READY;
		next_lwt = lwt;
	}
	// the lwt is on another kernal thread
	else
	{
		next_lwt = lwt_queue_peek(&__run_q);
		next_lwt->status = LWT_S_RUNNING;

		__lwt_kthd_wakeup(lwt);
	}

	__lwt_dispatch(next_lwt, current_lwt);
}

void __lwt_wakeup(lwt_t blocked_lwt)
{
	if (blocked_lwt->status == LWT_S_BLOCKED)
	{
		// blocked_lwt is on the same kernal thread
		if (blocked_lwt->kthd == __current_kthd)
		{
			lwt_queue_remove(&__wait_q, blocked_lwt);
			blocked_lwt->status = LWT_S_READY;
			lwt_queue_inqueue(&__run_q, blocked_lwt);
		}
		// blocked_lwt is on another kernal thread
		else
		{
			__lwt_kthd_wakeup(blocked_lwt);
		}
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

void __lwt_kthd_wakeup(lwt_t blocked_lwt)
{
	struct __lwt_kthd_msg_t__* msg = __lwt_kthd_msg_init(LWT_MSG_WAKEUP, blocked_lwt, NULL, NULL);
	__lwt_kthd_msg_inqueue(blocked_lwt->kthd, msg);
}

void __lwt_kthd_block(lwt_t lwt)
{
	struct __lwt_kthd_msg_t__* msg = __lwt_kthd_msg_init(LWT_MSG_BLOCK, lwt, NULL, NULL);
	__lwt_kthd_msg_inqueue(lwt->kthd, msg);
}

void __lwt_kthd_yield(lwt_t target)
{
	struct __lwt_kthd_msg_t__* msg = __lwt_kthd_msg_init(LWT_MSG_YIELD, target, NULL, NULL);
	__lwt_kthd_msg_inqueue(target->kthd, msg);
}

// =======================================================

struct __lwt_kthd_msg_t__* __lwt_kthd_msg_init(enum __lwt_kthd_msg_op_t op, lwt_t lwt, lwt_chan_t c, void* c_data)
{
	struct __lwt_kthd_msg_t__* msg = malloc(sizeof(struct __lwt_kthd_msg_t__));
	msg->op = op;
	msg->lwt = lwt;
	msg->c = c;
	msg->c_data = c_data;

	return msg;
}

void __lwt_kthd_msg_inqueue(struct __lwt_kthd_t__* kthd, struct __lwt_kthd_msg_t__* msg)
{
	pthread_mutex_lock(&kthd->msg_queue_lock);
	dlinkedlist_add(kthd->message_queue, dlinkedlist_element_init(msg));
	pthread_mutex_unlock(&kthd->msg_queue_lock);
}

struct __lwt_kthd_msg_t__* __lwt_kthd_msg_dequeue()
{
	struct __lwt_kthd_msg_t__* msg = NULL;
	
	pthread_mutex_lock(&__current_kthd->msg_queue_lock);
	dlinkedlist_element_t* e = dlinkedlist_first(__current_kthd->message_queue);
	dlinkedlist_remove(__current_kthd->message_queue, e);
	pthread_mutex_unlock(&__current_kthd->msg_queue_lock);

	if (e)
	{
		msg = e->data;
		dlinkedlist_element_free(&e);
	}
	return msg;
}

void* __lwt_kthd_entry(void* param)
{
	__lwt_main_thread_init();

	struct __lwt_kthd_entry_param_t__* p = param;

	__current_kthd = p->kthd;
	__idle_thread = __lwt_current_inline();
	__idle_thread->kthd = __current_kthd;
	
	debug_print("%p: creating lwt.....\n", lwt_current());
	__lwt_create_init_existing(p->lwt, LWT_F_NOJOIN, p->fn, p->data, p->c);
	debug_print("%p: new lwt %p created.\n", lwt_current(), p->lwt);

	free(param);
	
	__lwt_kthd_idle();

	return NULL;
}

void __lwt_kthd_idle()
{
//	debug_print("%p: __lwt_kthd_idle in pthread %p. \n", lwt_current(), pthread_self());
	while(1)
	{
		if (lwt_queue_size(&__wait_q) == 0 && lwt_queue_size(&__run_q) == 1)
			break;
		
		struct __lwt_kthd_msg_t__* msg = __lwt_kthd_msg_dequeue();
		if (!msg)
			lwt_yield(LWT_NULL);
		else
		{
			debug_print("%p: msg: %d\n", lwt_current(), msg->op);
			switch (msg->op)
			{
				case LWT_MSG_YIELD:
					debug_print("%p: msg: LWT_MSG_YIELD\n", lwt_current());
					lwt_yield(msg->lwt);
					break;

				case LWT_MSG_WAKEUP:
					debug_print("%p: msg: LWT_MSG_WAKEUP\n", lwt_current());
					__lwt_wakeup(msg->lwt);
					break;
					
				case LWT_MSG_BLOCK:
					debug_print("%p: msg: LWT_MSG_BLOCK\n", lwt_current());
					__lwt_block_target(msg->lwt);
					break;
					
				case LWT_MSG_SND_BLOCKED:
					debug_print("%p: msg: LWT_MSG_SND_BLOCKED\n", lwt_current());
					__lwt_snd_blocked(msg->lwt, msg->c, msg->c_data);
					break;
					
				case LWT_MSG_SND_BUFFERED:
					debug_print("%p: msg: LWT_MSG_SND_BUFFERED\n", lwt_current());
					__lwt_snd_buffered(msg->lwt, msg->c, msg->c_data);
					break;
			}
		}
	}
	
	debug_print("pthread ended.\n");
}

struct __lwt_kthd_t__* __lwt_kthd_init()
{
	struct __lwt_kthd_t__* ret = malloc(sizeof(struct __lwt_kthd_t__));
	ret->message_queue = dlinkedlist_init();
	pthread_mutex_init(&ret->msg_queue_lock, NULL);
	return ret;
}

int lwt_kthd_create(lwt_fn_t fn, void* data, lwt_chan_t c)
{
	struct __lwt_kthd_entry_param_t__* param = malloc(sizeof(struct __lwt_kthd_entry_param_t__));
	if (!param)
		return -1;

	param->kthd = __lwt_kthd_init();
	if (!param->kthd)
		return -1;

	pthread_attr_t attr;
	if (0 != pthread_attr_init(&attr))
		return -1;

	if (0 != pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
		return -1;

	param->lwt = __lwt_init_lwt();
	param->fn = fn;
	param->data = data;
	param->c = c;
	param->lwt->kthd = param->kthd;
	
	if (c)
	{
		__lwt_chan_add_sndr(c, __lwt_current_inline());
		c->receiver = param->lwt;
	}
	
	debug_print("%p: create pthread. Current pthread: %p\n", lwt_current(), pthread_self());
	if (0 != pthread_create(&param->kthd->pthread_id, &attr, &__lwt_kthd_entry, param))
		return -1;
	
	pthread_attr_destroy(&attr);

	return 0;
}

/**
 Creates a lwt thread, with the entry function pointer fn,
 and the parameter pointer data used by fn
 Returns lwt_t type
 */
lwt_t lwt_create(lwt_fn_t fn, void* data, lwt_flags_t flags, lwt_chan_t c)
{
	if (lwt_queue_size(&__dead_q) == 0)
		__lwt_init_tcb_pool();
		
	lwt_t new_lwt = lwt_queue_dequeue(&__dead_q);
	new_lwt->id = __lwt_get_next_threadid();
	new_lwt->status = LWT_S_READY;
	new_lwt->entry_fn = fn;
	new_lwt->entry_fn_param = data;
	new_lwt->flags = flags;
	new_lwt->joiner = NULL;
	new_lwt->kthd = __current_kthd;
	
	__lwt_create_init_stack(new_lwt, fn, data, c);
	
	lwt_queue_inqueue(&__run_q, new_lwt);

	if (c)
	{
		c->receiver = new_lwt;
		debug_print("%p: channel \"%s\" was delegated to %p\n", lwt_current(), lwt_chan_get_name(c), new_lwt);
	}
	
	return new_lwt;
}

lwt_status_t lwt_status(lwt_t lwt)
{
	if (!lwt)
		return LWT_S_DEAD;
	
	return lwt->status;
}

/**
 Yields to the next available thread
 */
void lwt_yield(lwt_t target)
{
	lwt_t current_lwt = lwt_queue_head_next(&__run_q);
	current_lwt->status = LWT_S_READY;
	
	if (target)
	{
		if (target->kthd != __current_kthd)
			__lwt_kthd_yield(target);
		else
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
	lwt_t cur_lwt = __lwt_current_inline();

	if (!lwt)
		return -1;

	if (lwt == cur_lwt)
		return -2;

	if (lwt->status > LWT_S_ZOMBIE)
		return -3;

	if (__lwt_flags_get_nojoin(lwt))
		return -4;

	if (lwt->joiner)
		return -5;
	
	lwt->joiner = cur_lwt;

	// Block until the joining thread finishes.
	// Thread is joinable
	while(lwt->status < LWT_S_FINISHED)	// LWT_S_DEAD > LWT_S_FINISHED
		__lwt_block();
	
	if (retval_ptr)
	{
		*retval_ptr = lwt->return_val;
	}

	if (lwt->status == LWT_S_ZOMBIE)
	{
		lwt_queue_remove(&__zombie_q, lwt);
	}

	lwt->status = LWT_S_DEAD;
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

	if (!lwt_finished->joiner)
	{
		if (__lwt_flags_get_nojoin(lwt_finished))
		{
			lwt_finished->status = LWT_S_DEAD;
			lwt_queue_inqueue(&__dead_q, lwt_finished);
		}
		else
		{
			lwt_queue_inqueue(&__zombie_q, lwt_finished);
			lwt_finished->status = LWT_S_ZOMBIE;
		}
	}
	else
	{
		__lwt_wakeup(lwt_finished->joiner);
	}
	
	lwt_t next_lwt = lwt_queue_peek(&__run_q);
	// ??? is wakeup_all a good solution to avoid an empty run queue ???
	if (!next_lwt)
		__lwt_wakeup_all();

	next_lwt = lwt_queue_peek(&__run_q);
	assert(next_lwt);
	next_lwt->status = LWT_S_RUNNING;
	__lwt_dispatch(next_lwt, lwt_finished);
}

/**
 Gets the current thread lwt_t
 */
lwt_t lwt_current()
{
	return __lwt_current_inline();
}

lwt_t __lwt_current_inline()
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
			return lwt_queue_size(&__run_q);
		case LWT_INFO_NTHD_BLOCKED:
			return lwt_queue_size(&__wait_q);
		case LWT_INFO_NTHD_ZOMBIES:
		default:
			return lwt_queue_size(&__zombie_q);
	}
}

// ===================================================================
// lwt channel
// ===================================================================

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
	dlinkedlist_free(c->s_list);
	dlinkedlist_free(c->s_queue);
}

int __lwt_chan_try_to_free(lwt_chan_t *c)
{
	if (!((*c)->receiver) && dlinkedlist_size((*c)->s_list) == 0)
	{
		__lwt_chan_free_snd_buffer(*c);
		if ((*c)->grp[0])
		{
			lwt_cgrp_rem((*c)->grp[0], *c);
			(*c)->grp[0] = NULL;
		}
		if ((*c)->grp[1])
		{
			lwt_cgrp_rem((*c)->grp[1], *c);
			(*c)->grp[1] = NULL;
		}

		free((*c)->name);
		free(*c);
		*c = NULL;
		return 1;
	}
	else
		return 0;
}

void __lwt_snd_blocked_do(lwt_t sndr, lwt_chan_t c, void* data)
{
	// Add sndr to sender queue
	dlinkedlist_add(c->s_queue, dlinkedlist_element_init(sndr));
	
	// wait until my turn
	while (dlinkedlist_first(c->s_queue)->data != sndr)
	{
		debug_print("%p: __lwt_snd_blocked: wait until my turn.\n", lwt_current());
		__lwt_block();
	}
	
	// now it is my turn, set the data
	c->snd_data = data;
	
	// wait if it is my turn but no receiver exists or is blocked on this channel
	while (!c->rcv_blocked && c->snd_data)
	{
		debug_print("%p: __lwt_snd_blocked: wait no rcv.\n", lwt_current());
		__lwt_block();
	}
	
	// Now the receiver is ready to receive, yield to it
	debug_print("%p: __lwt_snd_blocked: yield to rcver %p.\n", lwt_current(), c->receiver);
	lwt_yield(c->receiver);
}

void __lwt_snd_blocked(lwt_t sndr, lwt_chan_t c, void* data)
{
	debug_print("%p: lwt_snd: -> __lwt_snd_blocked.\n", lwt_current());
	
	lwt_t cur = __lwt_current_inline();
	// executing on the same kthd as the sndr does
	if (cur == sndr)
	{
		// receiver is on another kthd, need msg passing
		if (sndr->kthd != c->receiver->kthd)
		{
			debug_print("%p: not on the same kthd as the receiver\n", sndr);
			struct __lwt_kthd_msg_t__* msg = __lwt_kthd_msg_init(LWT_MSG_SND_BLOCKED, sndr, c, data);
			__lwt_kthd_msg_inqueue(c->receiver->kthd, msg);
			debug_print("%p: msg sent, wait.\n", sndr);
			__lwt_block();
		}
		// receiver is on the same kthd
		else
		{
			__lwt_snd_blocked_do(sndr, c, data);
		}
	}
	// executing on a kthd other than sndr's
	// meaning msg has been passed to the receiver's kthd
	else
	{
		__lwt_snd_blocked_do(sndr, c, data);
	}
}

void* __lwt_rcv_blocked(lwt_chan_t c)
{
	// wait if nobody is sending on this channel
	while (dlinkedlist_size(c->s_queue) == 0)
	{
		debug_print("%p: __lwt_rcv_blocked: wait nobody snd\n", lwt_current());
		c->rcv_blocked = 1;
		__lwt_block();
	}
	
	debug_print("%p: __lwt_rcv_blocked: rcved %p.\n", lwt_current(), c->snd_data);
	// now the data has been sent via the channel, get it, and set receiver's status to non-blocked
	void *data = c->snd_data;
	c->snd_data = NULL;
	c->rcv_blocked = 0;

	// remove sender from the sender queue
	dlinkedlist_element_t* e = dlinkedlist_first(c->s_queue);
	dlinkedlist_remove(c->s_queue, e);
	lwt_t sndr = e->data;

	// sndr is on another kthd
	if (sndr->kthd != c->receiver->kthd)
	{
		__lwt_wakeup(__idle_thread);
		debug_print("%p: wakeup sndr\n", lwt_current());
		__lwt_kthd_wakeup(sndr);
	}
	// sndr is on the same kthd
	else
	{
		__lwt_wakeup(sndr);
	}
					
	dlinkedlist_element_free(&e);

	return data;
}

void __lwt_snd_buffered_do(lwt_t sndr, lwt_chan_t c, void* data)
{
	while (ring_queue_full(c->snd_buffer))
	{
		debug_print("%p: __lwt_snd_buffered: wait until buffer has space.\n", sndr);
		
		// insert into blocking queue
		// (c->s_queue is here used as a queue storing threads blocking on __lwt_snd_buffered)
		dlinkedlist_add(c->s_queue, dlinkedlist_element_init(sndr));
		
		debug_print("%p: __lwt_snd_buffered: call __lwt_block_and_wakeup %p\n", sndr, c->receiver);
		__lwt_block_and_wakeup(c->receiver);
		debug_print("%p: __lwt_snd_buffered: after calling __lwt_block_and_wakeup\n", sndr);
	}
	
	// remove from the block queue
	dlinkedlist_element_t* e = dlinkedlist_find(c->s_queue, sndr);
	dlinkedlist_remove(c->s_queue, e);
	dlinkedlist_element_free(&e);
	
	debug_print("%p: __lwt_snd_buffered: buffer inqueue data %p\n", lwt_current(), data);
	ring_queue_inqueue(c->snd_buffer, data);
	if (sndr->kthd != c->receiver->kthd)
	{
		debug_print("%p: __lwt_snd_buffered: yield to sndr %p\n", lwt_current(), sndr);
		__lwt_kthd_yield(sndr);
	}
	__lwt_wakeup(c->receiver);
}

void __lwt_snd_buffered(lwt_t sndr, lwt_chan_t c, void* data)
{
	lwt_t cur = __lwt_current_inline();
	// executing on the same kthd as the sndr does
	if (cur == sndr)
	{
		// receiver is on another kthd, need msg passing
		if (sndr->kthd != c->receiver->kthd)
		{
			debug_print("%p: not on the same kthd as the receiver\n", sndr);
			struct __lwt_kthd_msg_t__* msg = __lwt_kthd_msg_init(LWT_MSG_SND_BUFFERED, sndr, c, data);
			__lwt_kthd_msg_inqueue(c->receiver->kthd, msg);
			debug_print("%p: msg sent, wait.\n", sndr);
			__lwt_block();
		}
		// receiver is on the same kthd
		else
		{
			__lwt_snd_buffered_do(sndr, c, data);
		}
	}
	// executing on a kthd other than sndr's
	// meaning msg has been passed to the receiver's kthd
	else
	{
		__lwt_snd_buffered_do(sndr, c, data);
	}
}

void* __lwt_rcv_buffered(lwt_chan_t c)
{
	// spinning if buffer is empty
	while (ring_queue_empty(c->snd_buffer))
	{
		debug_print("%p: __lwt_rcv_buffered: blocking buffer empty\n", lwt_current());
		__lwt_block();
	}
	
	void* data = ring_queue_dequeue(c->snd_buffer);
	debug_print("%p: __lwt_rcv_buffered: buffer having data %p\n", lwt_current(), data);

	dlinkedlist_element_t* e = dlinkedlist_first(c->s_queue);
	if (e)
	{
		dlinkedlist_remove(c->s_queue, e);
		lwt_t sndr = e->data;
		debug_print("%p: __lwt_rcv_buffered: wake up %p\n", lwt_current(), sndr);
		__lwt_wakeup(sndr);
		dlinkedlist_element_free(&e);
	}

	return data;
}

void __lwt_chan_add_sndr(lwt_chan_t c, lwt_t sndr)
{
	if (!dlinkedlist_find(c->s_list, sndr))
		dlinkedlist_add(c->s_list, dlinkedlist_element_init(sndr));
}


// =======================================================

lwt_chan_t lwt_chan(size_t sz, const char* name)
{
	lwt_chan_t chan = malloc(sizeof(struct __lwt_chan_t__));
	chan->s_list = dlinkedlist_init();
	chan->s_queue = dlinkedlist_init();
	chan->snd_data = NULL;
	chan->rcv_blocked = 0;
	chan->receiver = __lwt_current_inline();
	chan->grp[0] = NULL;
	chan->grp[1] = NULL;
	chan->tag = NULL;
	chan->events_num[0] = 0;
	chan->events_num[1] = 0;
	chan->event_queued[0] = 0;
	chan->event_queued[1] = 0;

	__lwt_chan_set_name(chan, name);
	__lwt_chan_init_snd_buffer(chan, sz);
	
	return chan;
}

int lwt_chan_deref(lwt_chan_t* c)
{
	if (!c || !(*c))
		return -1;

	lwt_t cur_lwt = __lwt_current_inline();
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
	// Forbit receiver from sending to itself
	lwt_t sndr = __lwt_current_inline();
	if (c->receiver == sndr)
		return -1;
	
	// If sndr has not sent on this channel before, add it to sender list
	__lwt_chan_add_sndr(c, sndr);

	// debug_print("%p: lwt_snd: %s's lwt_list count=%d", lwt_current(), lwt_chan_get_name(c), dlinkedlist_size(c->s_list));
	// debug_print(", sending count=%d\n", lwt_chan_sending_count(c));
	
	// if the channel is added to a group that waits for snd event to happen
	if (c->grp[0] && !c->event_queued[0])
	{
		// add snd event to the snd event queue (id=0)
		dlinkedlist_add(c->grp[0]->event_queue[0], dlinkedlist_element_init(c));
		c->event_queued[0] = 1;
		c->events_num[0]++;
		c->grp[0]->total_num_events++;

		// wakeup all rcvrs that are waiting for snd event on this group
		debug_print("%p: wake up snd-event waiting lwts.\n", lwt_current());

		dlinkedlist_element_t* e;
		dlinkedlist_t* wq = c->grp[0]->wait_queue[0];
		while(dlinkedlist_size(wq) > 0)
		{
			e = dlinkedlist_first(wq);
			dlinkedlist_remove(wq, e);
			__lwt_wakeup(e->data);
			debug_print("%p: waking up lwt %p\n", lwt_current(), e->data);
		}
	}

	if (__lwt_chan_use_buffer(c))
	{
		__lwt_snd_buffered(sndr, c, data);
	}
	else
	{
		__lwt_snd_blocked(sndr, c, data);
	}
	return 0;
}

void* lwt_rcv(lwt_chan_t c)
{
	void* ret = NULL;
	
	// if the channel is added to a group that waits for rcv event to happen
	if (c->grp[1] && !c->event_queued[1])
	{
		// add rcv event to the rcv event queue (id=1)
		dlinkedlist_add(c->grp[1]->event_queue[1], dlinkedlist_element_init(c));
		c->event_queued[1] = 1;
		c->events_num[1]++;
		c->grp[1]->total_num_events++;

		// wakeup all sndrs that are waiting for rcv event on this grp
		dlinkedlist_element_t* e;
		dlinkedlist_t* wq = c->grp[1]->wait_queue[1];
		while(dlinkedlist_size(wq) > 0)
		{
			e = dlinkedlist_first(wq);
			dlinkedlist_remove(wq, e);
			__lwt_wakeup(e->data);
		}
	}
	
	if (__lwt_chan_use_buffer(c))
	{
		ret = __lwt_rcv_buffered(c);
	}
	else
	{
		debug_print("%p: lwt_rcv: -> __lwt_rcv_blocked.\n", lwt_current());
		ret = __lwt_rcv_blocked(c);
	}
	
	return ret;
}

int lwt_snd_chan(lwt_chan_t c, lwt_chan_t sc)
{
	return lwt_snd(c, sc);
}

int lwt_snd_cdeleg(lwt_chan_t c, lwt_chan_t delegating)
{
	// add sender to the sender list of delegating channel
	__lwt_chan_add_sndr(delegating, __lwt_current_inline());

	return lwt_snd(c, delegating);
}

lwt_chan_t lwt_rcv_chan(lwt_chan_t c)
{
	return lwt_rcv(c);
}

lwt_chan_t lwt_rcv_cdeleg(lwt_chan_t c)
{
	lwt_chan_t delegating = lwt_rcv(c);

	// change the receiver of the received channel to current thread
	delegating->receiver = __lwt_current_inline();
	
	return delegating;
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

size_t lwt_chan_sending_count(lwt_chan_t c)
{
	if (!c)
		return 0;
	
	return dlinkedlist_size(c->s_list);
}

lwt_cgrp_t lwt_cgrp()
{
	lwt_cgrp_t grp = malloc(sizeof(struct __lwt_cgrp_t__));
	grp->event_queue[0] = dlinkedlist_init();
	grp->event_queue[1] = dlinkedlist_init();

	grp->wait_queue[0] = dlinkedlist_init();
	grp->wait_queue[1] = dlinkedlist_init();

	grp->listeners[0] = dlinkedlist_init();
	grp->listeners[1] = dlinkedlist_init();

	grp->channel_num = 0;
	grp->total_num_events = 0;
	return grp;
}

int lwt_cgrp_free(lwt_cgrp_t* grp)
{
	if (grp && (*grp))
	{
		if ((*grp)->channel_num > 0)
			return -1;

		dlinkedlist_free(&((*grp)->event_queue[0]));
		dlinkedlist_free(&((*grp)->event_queue[1]));
		dlinkedlist_free(&((*grp)->wait_queue[0]));
		dlinkedlist_free(&((*grp)->wait_queue[1]));
		dlinkedlist_free(&((*grp)->listeners[0]));
		dlinkedlist_free(&((*grp)->listeners[1]));
	
		free(*grp);
		*grp = NULL;
	}
	return 0;
}

int lwt_cgrp_add(lwt_cgrp_t grp, lwt_chan_t c, lwt_chan_dir_t dir)
{
	// add to wait for rcv event to happen
	if (dir == LWT_CHAN_RCV)
	{
		if (c->grp[1])
			return -2;
		
		c->grp[1] = grp;
		c->events_num[1] = 0;
		if (!dlinkedlist_find(grp->listeners[1], __lwt_current_inline()))
			dlinkedlist_add(grp->listeners[1], dlinkedlist_element_init(__lwt_current_inline()));
	}
	// add to wait for snd event to happen
	else if (dir == LWT_CHAN_SND)
	{
		if (c->receiver != __lwt_current_inline())
			return -1;

		if (c->grp[0])
			return -2;
		
		c->grp[0] = grp;
		c->events_num[0] = 0;
		if (!dlinkedlist_find(grp->listeners[0], __lwt_current_inline()))
			dlinkedlist_add(grp->listeners[0], dlinkedlist_element_init(__lwt_current_inline()));
	}
	
	grp->channel_num++;
	return 0;
}

int lwt_cgrp_rem(lwt_cgrp_t grp, lwt_chan_t c)
{
	if (c->events_num[0] > 0 || c->events_num[1] > 0)
		return 1;
	
	if (c->grp[0] == grp)
	{
		c->events_num[0] = 0;
		c->event_queued[0] = 0;
		c->grp[0] = NULL;
		dlinkedlist_element_t* e = dlinkedlist_find(grp->listeners[0], __lwt_current_inline());
		if (e)
		{
			dlinkedlist_remove(grp->listeners[0], e);
			dlinkedlist_element_free(&e);
		}
	}
	else if (c->grp[1] == grp)
	{
		c->events_num[1] = 0;
		c->event_queued[1] = 0;
		c->grp[1] = NULL;
		dlinkedlist_element_t* e = dlinkedlist_find(grp->listeners[1], __lwt_current_inline());
		if (e)
		{
			dlinkedlist_remove(grp->listeners[1], e);
			dlinkedlist_element_free(&e);
		}
	}
	
	grp->channel_num--;
	return 0;
}

lwt_chan_t lwt_cgrp_wait(lwt_cgrp_t grp, lwt_chan_dir_t* dir)
{
	dlinkedlist_t* event_queue = NULL;
	dlinkedlist_t* wq = NULL;
	lwt_chan_dir_t evt_dir;
	
	lwt_t lwt = __lwt_current_inline();
	// 1. check whether lwt is a listener for snd event
	dlinkedlist_element_t* e = dlinkedlist_find(grp->listeners[0], lwt);
	if (e)
	{
		debug_print("%p: is waiting for snd event\n", lwt);
		// set channel to receivable
		evt_dir = LWT_CHAN_RCV;
		event_queue = grp->event_queue[0];
		wq = grp->wait_queue[0];
	}
	else
	{
		e = dlinkedlist_find(grp->listeners[1], lwt);
		if (e)
		{
			debug_print("%p: is waiting for rcv event\n", lwt);
			evt_dir = LWT_CHAN_SND;
			event_queue = grp->event_queue[1];
			wq = grp->wait_queue[1];
		}
	}
	
	if (!event_queue || !wq)
		return NULL;
	
	while (dlinkedlist_size(event_queue) == 0)
	{
		dlinkedlist_add(wq, dlinkedlist_element_init(lwt));
		__lwt_block();
	}
	
	dlinkedlist_element_t* evt = dlinkedlist_first(event_queue);
	dlinkedlist_remove(event_queue, evt);
	lwt_chan_t c = evt->data;
	dlinkedlist_element_free(&evt);

	// the channel is sendable
	if (evt_dir == LWT_CHAN_SND)
	{
		c->events_num[1]--;
		c->event_queued[1]--;
	}
	// the channel is receivable
	else if (evt_dir == LWT_CHAN_RCV)
	{
		c->events_num[0]--;
		c->event_queued[0]--;
	}

	*dir = evt_dir;
	return c;
}


void* __lwt_idle_thread_for_main(void* data, lwt_chan_t c)
{
	__lwt_kthd_idle();
	
	return NULL;
}

/** __lwt_init is loaded automatically,
 * to add main thread as an lwt thread.
 */
__attribute__((constructor))
static void __lwt_init()
{
	__current_kthd = __lwt_kthd_init();
	__current_kthd->pthread_id = pthread_self();

	__lwt_main_thread_init();

	__idle_thread = lwt_create(&__lwt_idle_thread_for_main, NULL, LWT_F_NOJOIN, NULL);

	debug_print("main: %p, idle: %p\n", __main_thread, __idle_thread);
}