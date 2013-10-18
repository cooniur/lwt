//
//  lwt.c
//  lwt
//
//  Created by Tongliang Liu on 10/17/13.
//  Copyright (c) 2013 cooniur. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>

#include "lwt.h"
#include "lwt-test.h"
#include "lwt-const.h"

/*==================================================*
 *													*
 *				Useful Marcos						*
 *													*
 *==================================================*/
#define LWT_STRUCT_OFFSET(Field) \
	[Field] "i" (offsetof(struct __lwt_t__, Field))

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
	LWT_S_DEAD				// Thread called lwt_die(), and is dead. Ready to be joined.
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
	 Instruction Pointer
	 */
	void* eip;
	
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
	unsigned int id;
	
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
 */
lwt_t __lwt_rq_head = NULL;
lwt_t __lwt_rq_tail = NULL;

lwt_t __current;
lwt_t __main_thread = NULL;

unsigned int __lwt_threadid = 1;

/*==================================================*
 *													*
 *				Declaration							*
 *													*
 *==================================================*/

extern void __lwt_trampoline(lwt_fn_t fn, void* data);		//   __lwt_trampoline calls __lwt_start (in assembly)
void* __lwt_start(lwt_fn_t fn, void* data);

unsigned int __lwt_get_next_threadid();
static void __lwt_main_thread_init();

static int __lwt_rq_empty();
static void __lwt_rq_inqueue(lwt_t new_lwt);
static lwt_t __lwt_rq_dequeue();
static lwt_t __lwt_rq_remove_head();

static void __lwt_dispatch(lwt_t next, lwt_t current);
//void __attribute__ ((always_inline)) __lwt_dispatch(lwt_t next, lwt_t current);

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

unsigned int __lwt_get_next_threadid()
{
	return __lwt_threadid++;
}

/*--------------------------------------------------*
 *													*
 *	Run Queue functions								*
 *													*
 *--------------------------------------------------*/
/**
 Returns 1 if the run queue is empty.
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
static void __lwt_dispatch(lwt_t next, lwt_t current)
{
	if (next->status != LWT_S_CREATED && next->status != LWT_S_READY)
		return;

	if (current->status != LWT_S_RUNNING && current->status != LWT_S_DEAD)
		return;
		
	switch (current->status) {
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
	}

	switch (next->status) {
			// Thread is just created, call __lwt_trampoline
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
								  
								  // Jumps to __lwt_trampoline, which will call __lwt_start
								  "jmp __lwt_trampoline \n\t"					// start the new thread
								  :
								  : "r" (next),
									LWT_STRUCT_OFFSET(ebp),
									LWT_STRUCT_OFFSET(entry_fn),
									LWT_STRUCT_OFFSET(entry_fn_param)
								  :
								  );
			break;
		
		case LWT_S_READY:
			next->status = LWT_S_RUNNING;
			__asm__ __volatile__ (
								  "leal %c[ebp](%0), %%ebx \n\t"			// %ebx = &next->ebp
								  "movl (%%ebx), %%ebp \n\t"				// %ebp = next->ebp
								  "movl 0x4(%%ebx), %%esp \n\t"				// %esp = next->esp
								  "popal \n\t"								// resume the next thread
								  :
								  : "r" (next),
								  LWT_STRUCT_OFFSET(ebp)
								  :
								  );
			break;
	}
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
 Thread entry
 */
void* __lwt_start(lwt_fn_t fn, void* data)
{
	return fn(data);
}


/*--------------------------------------------------*
 *													*
 *	Public functions								*
 *													*
 *--------------------------------------------------*/

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

void lwt_yield()
{
	// Only one thread running
	if (__lwt_rq_head == __lwt_rq_tail)
		return;
	
	lwt_t current_lwt = __lwt_rq_dequeue();
	lwt_t next_lwt = __lwt_rq_head;
	
	__lwt_dispatch(next_lwt, current_lwt);
}

void* lwt_join(lwt_t lwt)
{
	int *p = (int*)malloc(sizeof(int));
	*p = 0xF4;
	return p;
}

void lwt_die(void *data)
{
	lwt_t lwt_dead = __lwt_rq_remove_head();
	lwt_dead->status = LWT_S_DEAD;

	__lwt_dispatch(__lwt_rq_head, lwt_dead);
}
