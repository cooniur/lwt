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
	LWT_S_JOINABLE,			// Thread is finished and is ready to be joined
	LWT_S_DEAD				// Thread is joined and finally dead.
};

/**
 Thread Descriptor
 */
struct __lwt_t__
{
	/**
	 Stack Base Pointer
	 */
	void* ebp;
	
	/**
	 Stack Pointer
	 */
	void* esp;
	
	/**
	 Thread Entry Function Pointer
	 */
	lwt_fn_t entry_fn;
	
	/**
	 Thread Entry Function Parameter Pointer
	 */
	void* entry_fn_param;

	/**
	 Thread Return Value Pointer
	 */
	void* return_val;
	
	/**
	 Thread ID
	 */
	int id;
	
	/**
	 Thread status
	 */
	lwt_status_t status;
	 
	/**
	 Stack Size
	 */
	size_t stack_size;

	/**
	 Stack Memory Pointer by malloc
	 */
	void* stack;

	/**
	 Points to the next thread descriptor
	 */
	struct __lwt_t__ *next;
} __attribute__ ((aligned (16), packed));

/*==================================================*
 *													*
 *				Global Variables					*
 *													*
 *==================================================*/
/**
 The Run Queue
 __lwt_rq_head always points to the current thread
 */
lwt_t __lwt_rq_head = NULL;
lwt_t __lwt_rq_tail = NULL;

/**
 The main thread TCB
 */
lwt_t __main_thread = NULL;

/**
 Stores the next available thread id #
 */
int __lwt_threadid = 1;

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

static int __lwt_get_next_threadid();
static void __lwt_main_thread_init();

static int __lwt_rq_empty();
static void __lwt_rq_inqueue(lwt_t new_lwt);
static lwt_t __lwt_rq_dequeue();
static lwt_t __lwt_rq_remove_head();

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
static int __lwt_rq_empty()
{
	if (!__lwt_rq_head && !__lwt_rq_tail)
		return 1;
	else
		return 0;
}

/**
 Adds the new_lwt to the tail of the run queue.
 Makes sure tail->next is always the head
 */
static void __lwt_rq_inqueue(lwt_t new_lwt)
{
	if (__lwt_rq_empty())
	{
		new_lwt->next = new_lwt;
		__lwt_rq_head = new_lwt;
		__lwt_rq_tail = new_lwt;
	}
	else
	{
		__lwt_rq_tail->next = new_lwt;
		__lwt_rq_tail = new_lwt;
		__lwt_rq_tail->next = __lwt_rq_head;
	}
}

/**
 Returns the head of the run queue, moves the head to head->next
 */
static lwt_t __lwt_rq_dequeue()
{
	if (__lwt_rq_empty())
		return NULL;
	
	lwt_t ret = __lwt_rq_head;
	
	__lwt_rq_head = __lwt_rq_head->next;
	__lwt_rq_tail = __lwt_rq_tail->next;
	
	return ret;
}

/**
 Removes the head node from the run queue and returns it.
 Moves head to head->next
 */
static lwt_t __lwt_rq_remove_head()
{
	if (__lwt_rq_empty())
		return NULL;
	
	lwt_t ret = __lwt_rq_head;
	
	__lwt_rq_tail->next = __lwt_rq_head->next;
	__lwt_rq_head = __lwt_rq_head->next;
	ret->next = NULL;
	
	return ret;
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
	if (next->status != LWT_S_CREATED && next->status != LWT_S_READY)
		return;

	if (current->status != LWT_S_RUNNING && current->status != LWT_S_JOINABLE && current->status != LWT_S_DEAD)
		return;
	
	switch (current->status)
	{
			// Current thread is running
		case LWT_S_RUNNING:
			current->status = LWT_S_READY;
			__asm__ __volatile__ (
								  // Save all registers to stack
								  "pushal \n\t"								// push all registers to stack
								  
								  // Save stack pointer and base pointer
								  "leal %c[ebp](%0), %%ebx \n\t"			// %ebx = &current->ebp
								  "movl %%ebp, (%%ebx) \n\t"				// current->ebp = %ebp
								  "movl %%esp, 0x4(%%ebx) \n\t"				// current->esp = %esp
								  :
								  : "r" (current),
								  LWT_STRUCT_OFFSET(ebp)
								  :
								  );
			break;
			// Does nothing but to surpress the warnings.
		default:
			break;
	}

	switch (next->status)
	{
			// The next thread is just created, call __lwt_trampoline
		case LWT_S_CREATED:
			next->status = LWT_S_RUNNING;
			__asm__ __volatile__ (
								  // Restore stack pointer and base pointer
								  "leal %c[ebp](%0), %%ebx \n\t"				// %ebx = &next->ebp
								  "movl (%%ebx), %%ebp \n\t"					// %ebp = next->ebp
								  "movl 0x4(%%ebx), %%esp \n\t"					// %esp = next->esp

								  // Pass function call parameters via stack
								  "sub $0x20, %%esp \n\t"						// allocate stack space for calling __lwt_start
								  "leal %c[entry_fn_param](%0), %%ebx \n\t"		// %%ebx = &next->entry_fn_param
								  "movl (%%ebx), %%ebx \n\t"					// %%ebx = next->entry_fn_param
								  "movl %%ebx, 0x8(%%esp) \n\t"					// push %%ebx
								  "leal %c[entry_fn](%0), %%ebx \n\t"			// %%ebx = &next->entry_fn
								  "movl (%%ebx), %%ebx \n\t"					// %%ebx = next->entry_fn
								  "movl %%ebx, 0x4(%%esp) \n\t"					// push %%ebx
								  "leal lwt_die, %%ebx \n\t"					// %%ebx = &lwt_die
								  "movl %%ebx, (%%esp) \n\t"					// push %%ebx
								  
								  // Jumps to __lwt_trampoline, which will call __lwt_start.
								  // The returning address of __lwt_start is lwt_die()
								  "jmp __lwt_trampoline \n\t"
								  :
								  : "r" (next),
									LWT_STRUCT_OFFSET(ebp),
									LWT_STRUCT_OFFSET(entry_fn),
									LWT_STRUCT_OFFSET(entry_fn_param)
								  :
								  );
			/* +------------------------------------------------------------+ */
			/* | !! Attention !!											| */
			/* +------------------------------------------------------------+ */
			/* |  The function parameters can NOT be accessed here,			| */
			/* |  because they were stored on the "current" thread's stack,	| */
			/* |  but now we have switched to the "next" thread's stack.	| */
			/* +------------------------------------------------------------+ */
			break;
		
			// The next thread is ready to be resumed
		case LWT_S_READY:
			next->status = LWT_S_RUNNING;
			__asm__ __volatile__ (
								  // Restore stack pointer and base pointer
								  "leal %c[ebp](%0), %%ebx \n\t"			// %ebx = &next->ebp
								  "movl (%%ebx), %%ebp \n\t"				// %ebp = next->ebp
								  "movl 0x4(%%ebx), %%esp \n\t"				// %esp = next->esp
								  "popal \n\t"								// resume the next thread
								  :
								  : "r" (next),
								  LWT_STRUCT_OFFSET(ebp)
								  :
								  );
			/* +------------------------------------------------------------+ */
			/* | !! Attention !!											| */
			/* +------------------------------------------------------------+ */
			/* |  The function parameters can NOT be accessed here,			| */
			/* |  because they were stored on the "current" thread's stack,	| */
			/* |  but now we have switched to the "next" thread's stack.	| */
			/* +------------------------------------------------------------+ */
			break;
			// Does nothing but to surpress the warnings.
		default:
			break;
	}

	/* +------------------------------------------------------------+ */
	/* | !! Attention !!											| */
	/* +------------------------------------------------------------+ */
	/* |  The function parameters can NOT be accessed here,			| */
	/* |  because they were stored on the "current" thread's stack,	| */
	/* |  but now we have switched to the "next" thread's stack.	| */
	/* +------------------------------------------------------------+ */
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
	
	__lwt_rq_inqueue(__main_thread);
}

/**
 Thread entry, called by __lwt_trampoline in assembly
 */
void __lwt_start(lwt_fn_t fn, void* data)
{
	void* ret = fn(data);
	lwt_die(ret);
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
	
	// creates tcb
	lwt_t new_lwt = (struct __lwt_t__*)malloc(sizeof(struct __lwt_t__));
	new_lwt->id = __lwt_get_next_threadid();
	new_lwt->status = LWT_S_CREATED;
	new_lwt->next = NULL;
	new_lwt->entry_fn = fn;
	new_lwt->entry_fn_param = data;
	new_lwt->return_val = NULL;

	// creates stack
	new_lwt->stack_size = DEFAULT_LWT_STACK_SIZE;
	new_lwt->stack = malloc(sizeof(void) * new_lwt->stack_size);
	new_lwt->ebp = new_lwt->stack + new_lwt->stack_size;
	new_lwt->esp = new_lwt->ebp;
	
	__lwt_rq_inqueue(new_lwt);
	
	return new_lwt;
}

/**
 Yields to the next available thread
 */
void lwt_yield()
{
	// Only one thread running
	if (__lwt_rq_head == __lwt_rq_tail)
		return;
	
	lwt_t current_lwt = __lwt_rq_dequeue();
	lwt_t next_lwt = __lwt_rq_head;
	
	__lwt_dispatch(next_lwt, current_lwt);
}

/**
 Joins a specified thread and waits for its termination.
 The pointer to the returned value will be passed via retval_ptr
 Returns -1 if fails to join; otherwise, 0
 */
int lwt_join(lwt_t lwt, void **retval_ptr)
{
	if (lwt->status == LWT_S_DEAD)
		return -1;
	
	// Spinning until the joining thread finishes.
	while(lwt->status != LWT_S_JOINABLE)
		lwt_yield();
	
	lwt->status = LWT_S_DEAD;
	*retval_ptr = lwt->return_val;
	free(lwt);
	return 0;
}

/**
 Kill the current thread.
 Return value is passed by data
 */
void lwt_die(void *data)
{
	// head always points to the current thread
	lwt_t lwt_finished = __lwt_rq_remove_head();
	lwt_finished->status = LWT_S_JOINABLE;
	lwt_finished->return_val = data;

	lwt_finished->next = NULL;
	lwt_finished->entry_fn = NULL;
	lwt_finished->entry_fn_param = NULL;

	// release the thread's stack
	free(lwt_finished->stack);
	lwt_finished->stack = lwt_finished->ebp = lwt_finished->esp = NULL;

	// switch to the next available thread
	__lwt_dispatch(__lwt_rq_head, lwt_finished);
}

/**
 Gets the current thread lwt_t
 */
lwt_t lwt_current()
{
	return __lwt_rq_head;
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
