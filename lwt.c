#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
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
	LWT_S_READY = 0,
	LWT_S_RUNNING,
	LWT_S_DEAD
} __lwt_status_t__;

/**
 Thread Descriptor
 */
struct __lwt_t__
{
	/**
	 Thread ID
	 */
	unsigned int id;
	
	/**
	 Thread status
	 */
	enum __lwt_status_t__ status;
	 
	/**
	 Stack Size
	 */
	size_t stack_size;

	/**
	 Stack Memory Pointer by malloc
	 */
	void* stack;

	/**
	 Stack Base Pointer
	 */
	void* ebp;
	
	/**
	 Stack Pointer
	 */
	void* esp;

	/**
	 Points to the next thread descriptor
	 */
	struct __lwt_t__ *next;
};

/*==================================================*
 *													*
 *				Global Variables					*
 *													*
 *==================================================*/
/**
 The Run Queue
 */
lwt_t lwt_rq_head = NULL;
lwt_t lwt_rq_tail = NULL;

lwt_t __current;
lwt_t __main_thread = NULL;

int __lwt_threadid = 0;

/*==================================================*
 *													*
 *				Declaration							*
 *													*
 *==================================================*/

extern void __lwt_trampoline(lwt_fn_t fn, void* data);		//   __lwt_trampoline calls __lwt_start (in assembly)
void __lwt_start(lwt_fn_t fn, void* data);
void __lwt_scheduler();


int __lwt_rq_empty();
void __lwt_rq_inqueue(lwt_t new_lwt);
lwt_t __lwt_rq_dequeue();
lwt_t __lwt_rq_remove_head();


void __attribute__ ((always_inline)) __lwt_save_stack_to(lwt_t lwt);
void __attribute__ ((always_inline)) __lwt_load_stack_from(lwt_t lwt);
void __attribute__ ((always_inline)) __lwt_dispatch(lwt_t next, lwt_t current);

/*==================================================*
 *													*
 *				Implementation						*
 *													*
 *==================================================*/

/*--------------------------------------------------*
 *													*
 *	Run Queue functions								*
 *													*
 *--------------------------------------------------*/
int __lwt_rq_empty()
{
	if (!lwt_rq_head && !lwt_rq_tail)
		return 1;
	else
		return 0;
}

void __lwt_rq_inqueue(lwt_t new_lwt)
{
	if (__lwt_rq_empty())
	{
		new_lwt->next = new_lwt;
		lwt_rq_head = new_lwt;
		lwt_rq_tail = new_lwt;
	}
	else
	{
		lwt_rq_tail->next = new_lwt;
		lwt_rq_tail = new_lwt;
		lwt_rq_tail->next = lwt_rq_head;
	}
}

lwt_t __lwt_rq_dequeue()
{
	if (__lwt_rq_empty())
		return NULL;
	
	lwt_t ret = lwt_rq_head;
	
	lwt_rq_head = lwt_rq_head->next;
	lwt_rq_tail = lwt_rq_tail->next;
	
	return ret;
}

lwt_t __lwt_rq_remove_head()
{
	if (__lwt_rq_empty())
		return NULL;
	
	lwt_t ret = lwt_rq_head;
	
	lwt_rq_tail->next = lwt_rq_head->next;
	lwt_rq_head = lwt_rq_head->next;
	
	return ret;
}

/*--------------------------------------------------*
 *													*
 *	Scheduler functions								*
 *													*
 *--------------------------------------------------*/
/**
 Save stack (%esp, %ebp) to a TCB (i.e. lwt_t)
 */
void __attribute__ ((always_inline)) __lwt_load_stack_from(lwt_t lwt)
{
	__asm__ __volatile__ (
		"movl %c[esp](%0), %%esp \n\t"
		"movl %c[ebp](%0), %%ebp \n\t"
		: 
		: "r" (lwt),
			LWT_STRUCT_OFFSET(esp),
			LWT_STRUCT_OFFSET(ebp)
		:
	);
}

/**
 Load stack (%esp, %ebp) from a TCB (i.e. lwt_t)
 */
void __attribute__ ((always_inline)) __lwt_save_stack_to(lwt_t lwt)
{
	__asm__ __volatile__ (
		"movl %%esp, %c[esp](%0) \n\t"
		"movl %%ebp, %c[ebp](%0) \n\t"
		: 
		: "r" (lwt),
			LWT_STRUCT_OFFSET(esp),
			LWT_STRUCT_OFFSET(ebp)
		:
	);
}

void __attribute__ ((always_inline)) __lwt_dispatch(lwt_t next, lwt_t current)
{
	__asm__ __volatile__ (
		"pushal \n\t"
	);

	__lwt_save_stack_to(current);
	if (next->status == LWT_S_RUNNING)
	{
		__lwt_load_stack_from(next);
		__asm__ __volatile__ (
			"popal \n\t"
		);
	}
	else
	{
		__lwt_load_stack_from(next);
	}
}

void test_inline_as(lwt_t lwt)
{
		
}

/**
 Save main thread information (i.e. where the main() function is)
 */
void __lwt_main_thread_init()
{
	if (__main_thread)
		return;
		
	__main_thread = (struct __lwt_t__*)malloc(sizeof(struct __lwt_t__));
	__main_thread->id = 0;
	__main_thread->status = LWT_S_RUNNING;
	__main_thread->stack = NULL;
	
	__main_thread->next = NULL;
	__lwt_save_stack_to(__main_thread);
}

lwt_t lwt_create(lwt_fn_t fn, void *data)
{
	__lwt_main_thread_init();
	
	// creates tcb
	lwt_t lwt = (struct __lwt_t__*)malloc(sizeof(struct __lwt_t__));

	// creates stack
	lwt->stack_size = DEFAULT_LWT_STACK_SIZE;
	lwt->stack = malloc(sizeof(void) * lwt->stack_size);
	lwt->ebp = lwt->stack + lwt->stack_size;
	lwt->esp = lwt->ebp;
	lwt->next = NULL;
	lwt->id = __lwt_threadid++;
	lwt->status = LWT_S_READY;

	__lwt_dispatch(__main_thread, __main_thread);
	
	__lwt_start(fn, data);
	
	return lwt;
}

void __lwt_start(lwt_fn_t fn, void* data)
{
	fn(data);
}
