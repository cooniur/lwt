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
	LWT_S_FINISHED,			// Thread is finished and is ready to be joined
	LWT_S_DEAD				// Thread is joined and finally dead.
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

	if (current->status != LWT_S_RUNNING && current->status != LWT_S_FINISHED && current->status != LWT_S_DEAD)
		return;

	__asm__ __volatile__(
						 // 0. Put current thread's TCB pointer to %esi, next thread's TCB pointer to %edi
						 "movl %0, %%esi \n\t"				// %esi = current
						 "movl %1, %%edi \n\t"				// %edi = next

						 // 1. Save current thread's context if its status is LWT_S_RUNNING
						 "movl 0x14(%%esi), %%ebx \n\t"		// %ebx = current->status
						 "cmp $0x2, %%ebx \n\t"				// if (current->status == LWT_S_RUNNING)
						 "jne __switch_to_next \n\t"		// {
						 "pushal \n\t"						//   pushal
						 "movl $0x1, 0x14(%%esi) \n\t"		//   current->status = LWT_S_READY
						 // Save stack pointer and base pointer
						 "movl %%ebp, (%%esi) \n\t"			//   current->ebp = %ebp
						 "movl %%esp, 0x4(%%esi) \n\t"		//   current->esp = %esp
															// }
						 
						 // 2. Switch context to the next thread
						 "__switch_to_next: \n\t"
						 "movl 0x14(%%edi), %%ebx \n\t"		// %ebx = next->status

						 "movl $0x2, 0x14(%%edi) \n\t"		// next->status = LWT_S_RUNNING;
						 
						 // Restore stack pointer and base pointer
						 "movl (%%edi), %%ebp \n\t"			// %ebp = next->ebp
						 "movl 0x4(%%edi), %%esp \n\t"		// %esp = next->esp

						 "cmp $0x0, %%ebx \n\t"				// switch (next->status)
						 "je __next_case_lwt_s_created \n\t"	// case LWT_S_CREATED
						 "cmp $0x1, %%ebx \n\t"
						 "je __next_case_lwt_s_ready \n\t"		// case LWT_S_READY
						 "jmp __after_switched \n\t"
						 
						 "__next_case_lwt_s_created: \n\t"	// case LWT_S_CREATED: {
						 // Push parameters to stack for calling __lwt_start
						 "sub $0x40, %%esp \n\t"			//   allocate stack space for calling __lwt_start
						 "movl 0xc(%%edi), %%ebx \n\t"		//   %ebx = next->entry_fn_param
						 "movl %%ebx, 0x8(%%esp) \n\t"		//   push %ebx
						 "movl 0x8(%%edi), %%ebx \n\t"		//   %ebx = next->entry_fn
						 "movl %%ebx, 0x4(%%esp) \n\t"		//   push %ebx
						 
						 // Set the returning address of __lwt_start() to lwt_die()
						 "leal lwt_die, %%ebx \n\t"			//   %ebx = lwt_die
						 "movl %%ebx, (%%esp) \n\t"			//   push %ebx
						 
						 // Jump to __lwt_trampoline, which will call __lwt_start.
						 // The returning address of __lwt_start is lwt_die()
						 "jmp __lwt_trampoline \n\t"		//   jmp __lwt_trampoline
															//  }
						 
						 "__next_case_lwt_s_ready: \n\t"	// case LWT_S_READY: {
						 "popal \n\t"						//   popal
															//  }
						 "__after_switched: \n\t"
						 "movl 0x14(%%esi), %%ebx \n\t"		// %ebx = current->status

						 "cmp $0x3, %%ebx \n\t"				// if (current->status == LWT_S_FINISHED)
						 "jne __dispatch_end \n\t"			// {
						 
						 "__free_stack: \n\t"
						 "movl 0x18(%%esi), %%ebx \n\t"		//    %ebx = current->stack
						 "pushal \n\t"
						 "sub $0x20, %%esp \n\t"			//    allocate stack space for calling free
						 "movl %%ebx, (%%esp) \n\t"			//    push %ebx
						 "call free \n\t"					//    free(current->stack)
						 "popal \n\t"
						 "movl $0x0, 0x18(%%esi) \n\t"		//	  current->stack = NULL
						 "movl $0x0, (%%esi) \n\t"			//    current->ebp = NULL
						 "movl $0x0, 0x4(%%esi) \n\t"		//    current->esp = NULL
						 "movl $0x0, 0x8(%%esi) \n\t"		//    current->entry_fn = NULL
						 "movl $0x0, 0xc(%%esi) \n\t"		//    current->entry_fn_param = NULL
															//  }
						 "__dispatch_end: \n\t"
						 
						 :
						 : "r"(current), "r"(next)
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
	
	__lwt_rq_inqueue(__main_thread);
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
	/* |  The compiler does this amazing thing for us 				| */
	/* |  automatically! :D											| */
	/* +------------------------------------------------------------+ */
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
	if (lwt_current() == lwt)
		return -1;

	if (lwt->status == LWT_S_DEAD)
		return -1;
	
	// Spinning until the joining thread finishes.
	while(lwt->status != LWT_S_FINISHED)
		lwt_yield();
	
	lwt->status = LWT_S_DEAD;
	*retval_ptr = lwt->return_val;
	//free(lwt);
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
	lwt_finished->status = LWT_S_FINISHED;
	lwt_finished->return_val = data;

	lwt_finished->next = NULL;
	lwt_finished->entry_fn = NULL;
	lwt_finished->entry_fn_param = NULL;

//	// release the thread's stack
//	free(lwt_finished->stack);
//	lwt_finished->stack = lwt_finished->ebp = lwt_finished->esp = NULL;

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
