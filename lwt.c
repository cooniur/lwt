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

#include "lwt.h"
#include "dlinkedlist.h"

/*==================================================*
 *													*
 *				Useful Marcos						*
 *													*
 *==================================================*/
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

/*==================================================*
 *													*
 *				Types Definition					*
 *													*
 *==================================================*/
/**
 Thread status enum
 */
enum __lwt_status_t__
{
	LWT_S_CREATED = 0,		// Thread is just created. Stack is empty
	LWT_S_READY,			// Thread is switched out, and ready to be switched to
	LWT_S_RUNNING,			// Thread is running
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
 Thread queue type
 */
struct __lwt_queue_t__
{
	lwt_t head;
	lwt_t tail;
	size_t size;
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
	 Points to the next thread descriptor
	 Offset: 0x24
	 */
	struct __lwt_t__ *next;
	
	/**
	 Points to the previous thread descriptor
	 Offset: 0x28
	 */
	struct __lwt_t__ *prev;
	
} __attribute__ ((aligned (16), packed));

struct __lwt_chan_t__
{
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
/*==================================================*
 *													*
 *				Global Variables					*
 *													*
 *==================================================*/
/**
 The Run Queue
 run_q.head always points to the current thread
 */
struct __lwt_queue_t__ __run_q = {NULL, NULL, 0};

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

/*==================================================*
 *													*
 *				Declaration							*
 *													*
 *==================================================*/

/**
 A new thread's entry point
 Calls __lwt_start (in assembly)
 */
extern void __lwt_trampoline(lwt_fn_t fn, void* data);
void __lwt_start(lwt_fn_t fn, void* data);

static __attribute__ ((noinline)) void __lwt_dispatch(lwt_t next, lwt_t current);

static lwt_t __lwt_init_lwt();
static void __lwt_init_tcb_pool();
static int __lwt_get_next_threadid();
static void __lwt_main_thread_init();

static inline int __lwt_q_empty(struct __lwt_queue_t__ *queue);
static inline void __lwt_q_inqueue(struct __lwt_queue_t__ *queue, lwt_t lwt);
static inline lwt_t __lwt_q_next(struct __lwt_queue_t__ *queue);
static inline lwt_t __lwt_q_dequeue(struct __lwt_queue_t__ *queue);
static inline void __lwt_q_queue_jmp(struct __lwt_queue_t__ *queue, lwt_t tar);
static inline lwt_t __lwt_q_head(struct __lwt_queue_t__ *queue);
static inline lwt_t __lwt_q_tail(struct __lwt_queue_t__ *queue);

static inline void __lwt_create_init_stack(lwt_t lwt, lwt_fn_t fn, void *data);

extern void __lwt_trampoline();

/*==================================================*
 *													*
 *				Implementation						*
 *													*
 *==================================================*/

/*--------------------------------------------------*
 *													*
 *	Static functions								*
 *													*
 *--------------------------------------------------*/

/**
 Initialize TCB pool
 */
static void __lwt_init_tcb_pool()
{
	int i;
	for (i=0; i<TCB_POOL_SIZE; i++)
	{
		__lwt_q_inqueue(&__dead_q, __lwt_init_lwt());
	}
}

lwt_t __lwt_init_lwt()
{
	lwt_t new_lwt = (struct __lwt_t__*)malloc(sizeof(struct __lwt_t__));
	new_lwt->id = __lwt_get_next_threadid();
	new_lwt->status = LWT_S_CREATED;
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

/*--------------------------------------------------*
 *													*
 *	Run Queue functions								*
 *													*
 *--------------------------------------------------*/
/**
 Returns 1 if the run queue is empty; otherwise, 0
 */
static int __lwt_q_empty(struct __lwt_queue_t__ *queue)
{
	return queue->size == 0;
}

/**
 Adds the new_lwt to the tail of the run queue.
 Makes sure tail->next is always the head
 */
static void __lwt_q_inqueue(struct __lwt_queue_t__ *queue, lwt_t lwt)
{
	if (__lwt_q_empty(queue))
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
		queue->tail = lwt;
	}
	queue->size++;
}

/**
 Returns the head of the run queue, moves the head to head->next
 */
static lwt_t __lwt_q_next(struct __lwt_queue_t__ *queue)
{
	if (__lwt_q_empty(queue))
		return NULL;
	
	lwt_t ret = queue->head;
	
	queue->head = queue->head->next;
	queue->tail = queue->tail->next;
	
	return ret;
}

/**
 Removes the head node from the run queue and returns it.
 Moves head to head->next
 */
static lwt_t __lwt_q_dequeue(struct __lwt_queue_t__ *queue)
{
	if (__lwt_q_empty(queue))
		return NULL;
	
	lwt_t ret = queue->head;
	
	queue->tail->next = queue->head->next;
	queue->head->next->prev = queue->tail;
	queue->head = queue->head->next;
	
	ret->next = NULL;
	ret->prev = NULL;
	
	queue->size--;
	
	if (queue->size == 0)
		queue->head = queue->tail = NULL;

	return ret;
}

/**
 Move tar to the head of the queue
 */
static void __lwt_q_queue_jmp(struct __lwt_queue_t__ *queue, lwt_t tar)
{
	if (!tar)
		return;
	
	if (__lwt_q_empty(queue))
		__lwt_q_inqueue(queue, tar);
	else
	{
		if (tar->prev->next != tar)
		{
			tar->prev->next = tar->next;
			tar->next->prev = tar->prev;
			tar->next = queue->head->next;
			queue->head->next->prev = tar;
			tar->prev = queue->head;
			queue->head->next = tar;
		}
		queue->tail = queue->head;
		queue->head = tar;
	}
}

static inline lwt_t __lwt_q_head(struct __lwt_queue_t__ *queue)
{
	return queue->head;
}

static inline lwt_t __lwt_q_tail(struct __lwt_queue_t__ *queue)
{
	return queue->tail;
}

/*--------------------------------------------------*
 *													*
 *	Scheduler functions								*
 *													*
 *--------------------------------------------------*/
/**
 Switches from the "current" thread to the "next" thread
 Must be noinline function
 */
__attribute__ ((noinline))
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

/*--------------------------------------------------*
 *													*
 *	Entry functions									*
 *													*
 *--------------------------------------------------*/

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
	
	__lwt_q_inqueue(&__run_q, __main_thread);
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
	/* +------------------------------------------------------------+ */
	/* | !! Attention !!											| */
	/* +------------------------------------------------------------+ */
	/* |  Now jump to lwt_die() implicitly.							| */
	/* |  The calling convention does this amazing thing for us		| */
	/* |  automatically! :D											| */
	/* +------------------------------------------------------------+ */
}

static inline void __lwt_create_init_stack(lwt_t lwt, lwt_fn_t fn, void *data)
{
	unsigned int *esp = lwt->esp;
	void *lwt_start_stack;

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
}

/*--------------------------------------------------*
 *													*
 *	Public functions								*
 *													*
 *--------------------------------------------------*/

/**
 Creates a lwt thread, with the entry function pointer fn,
 and the parameter pointer data used by fn
 Returns lwt_t type
 */
lwt_t lwt_create(lwt_fn_t fn, void *data)
{
	__lwt_main_thread_init();
	
	if (__dead_q.size == 0)
		__lwt_init_tcb_pool();
		
	lwt_t new_lwt = __lwt_q_dequeue(&__dead_q);
	new_lwt->id = __lwt_get_next_threadid();
	new_lwt->status = LWT_S_READY;
	new_lwt->prev = NULL;
	new_lwt->next = NULL;
	new_lwt->entry_fn = fn;
	new_lwt->entry_fn_param = data;
	new_lwt->return_val = NULL;
	
	__lwt_create_init_stack(new_lwt, fn, data);
	
	__lwt_q_inqueue(&__run_q, new_lwt);
	
	__lwt_info.num_runnable++;
	
	return new_lwt;
}

/**
 Yields to the next available thread
 */
void lwt_yield(lwt_t target)
{
	// Only one thread running
	if (__run_q.head == __run_q.tail)
		return;
	
	if (target == __run_q.head)
		return;
	
	lwt_t next_lwt;
	lwt_t current_lwt = __lwt_q_next(&__run_q);

	if (target)
		__lwt_q_queue_jmp(&__run_q, target);

	next_lwt = __run_q.head;
	__lwt_dispatch(next_lwt, current_lwt);
}

/**
 Joins a specified thread and waits for its termination.
 The pointer to the returned value will be passed via retval_ptr
 Returns -1 if fails to join; otherwise, 0
 */
int lwt_join(lwt_t lwt, void **retval_ptr)
{
	if (lwt_current() == lwt)
		return -1;

	if (lwt->status == LWT_S_DEAD)
		return -1;

	// Spinning until the joining thread finishes.
	__lwt_info.num_runnable--;
	__lwt_info.num_blocked++;
	while(lwt->status != LWT_S_FINISHED)
		lwt_yield(NULL);
	__lwt_info.num_zombies--;
	__lwt_info.num_blocked--;
	__lwt_info.num_runnable++;
	
	lwt->status = LWT_S_DEAD;
	if (retval_ptr)
		*retval_ptr = lwt->return_val;

	__lwt_q_inqueue(&__dead_q, lwt);
	return 0;
}

/**
 Kill the current thread.
 Return value is passed by data
 */
void lwt_die(void *data)
{
	// head always points to the current thread
	lwt_t lwt_finished = __lwt_q_dequeue(&__run_q);
	lwt_finished->status = LWT_S_FINISHED;
	lwt_finished->return_val = data;

	lwt_finished->next = NULL;
	lwt_finished->entry_fn = NULL;
	lwt_finished->entry_fn_param = NULL;

	__lwt_info.num_zombies++;
	__lwt_info.num_runnable--;
	
	// switch to the next available thread
	__lwt_dispatch(__run_q.head, lwt_finished);
}

/**
 Gets the current thread lwt_t
 */
lwt_t lwt_current()
{
	return __run_q.head;
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

lwt_chan_t lwt_chan(int sz)
{
	lwt_chan_t chan = malloc(sizeof(struct __lwt_chan_t__));
	chan->s_list = dlinkedlist_init();
	chan->s_queue = dlinkedlist_init();
	chan->snd_data = NULL;

	chan->rcv_blocked = 0;
	chan->receiver = lwt_current();
	
	return chan;
}

int lwt_chan_deref(lwt_chan_t c)
{
	lwt_t cur_lwt = lwt_current();
	if (c->receiver == cur_lwt)
		c->receiver = NULL;
	else
	{
		dlinkedlist_element_t *e = dlinkedlist_find(c->s_list, cur_lwt);
		if (e)
			dlinkedlist_remove(c->s_list, e);
	}
	
	if (!c->receiver && dlinkedlist_size(c->s_list) == 0)
		return 1;
	else
		return 0;
}

int lwt_snd(lwt_chan_t c, void *data)
{
	assert(data != NULL);

	// No receiver exists, returns -1.
	if (!c->receiver)
		return -1;

	// Forbit receiver from sending to itself
	lwt_t sndr = lwt_current();
	if (c->receiver == sndr)
		return -2;

	// Add sndr to sender queue
	dlinkedlist_add(c->s_queue, dlinkedlist_element_init(sndr));

	// If sndr is sending on this channel first time, add it to sender list
	if (!dlinkedlist_find(c->s_list, sndr))
		dlinkedlist_add(c->s_list, dlinkedlist_element_init(sndr));
	
	// spinning if it is not my turn
	while (dlinkedlist_first(c->s_queue)->data != sndr)
		lwt_yield(NULL);

	// spinning if it is my turn but the receiver is not blocked on this channel
	while (!c->rcv_blocked)
		lwt_yield(NULL);
	
	// Now the receiver is ready to receive, set the data and yield to it
	c->snd_data = data;
	lwt_yield(c->receiver);
	return 0;
}

void *lwt_rcv(lwt_chan_t c)
{
	// spinning if nobody is sending on this channel
	while (dlinkedlist_size(c->s_queue) == 0)
	{
		c->rcv_blocked = 1;
		lwt_yield(NULL);
	}
	
	// Now there is at lease one sender sending, yield to it
	dlinkedlist_element_t *e = dlinkedlist_first(c->s_queue);
	lwt_t sndr = e->data;
	
	// remove sndr from the sender queue
	dlinkedlist_remove(c->s_queue, e);
	dlinkedlist_element_free(&e);

	// yield to sndr to allow it to prepare data
	lwt_yield(sndr);

	void *data = c->snd_data;
	c->rcv_blocked = 0;
	c->snd_data = NULL;
	
	return data;
}

void lwt_snd_chan(lwt_chan_t c, lwt_chan_t sc)
{
	lwt_snd(c, sc);
}

lwt_chan_t lwt_rcv_chan(lwt_chan_t c)
{
	return lwt_rcv(c);
}

