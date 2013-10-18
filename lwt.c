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

int __lwt_threadid = 0;

/*==================================================*
 *													*
 *				Declaration							*
 *													*
 *==================================================*/

extern void __lwt_trampoline(lwt_fn_t fn, void* data);		//   __lwt_trampoline calls __lwt_start (in assembly)
void __lwt_start(lwt_fn_t fn, void* data);

static void __lwt_main_thread_init();

static int __lwt_rq_empty();
static void __lwt_rq_inqueue(lwt_t new_lwt);
static lwt_t __lwt_rq_dequeue();
static lwt_t __lwt_rq_remove_head();


void __attribute__ ((always_inline)) __lwt_save_stack_to(lwt_t lwt);
void __attribute__ ((always_inline)) __lwt_load_stack_from(lwt_t lwt);
static inline void __lwt_dispatch(lwt_t next, lwt_t current);
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

/*--------------------------------------------------*
 *													*
 *	Run Queue functions								*
 *													*
 *--------------------------------------------------*/
static int __lwt_rq_empty()
{
	if (!__lwt_rq_head && !__lwt_rq_tail)
		return 1;
	else
		return 0;
}

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

static lwt_t __lwt_rq_dequeue()
{
	if (__lwt_rq_empty())
		return NULL;
	
	lwt_t ret = __lwt_rq_head;
	
	__lwt_rq_head = __lwt_rq_head->next;
	__lwt_rq_tail = __lwt_rq_tail->next;
	
	return ret;
}

static lwt_t __lwt_rq_remove_head()
{
	if (__lwt_rq_empty())
		return NULL;
	
	lwt_t ret = __lwt_rq_head;
	
	__lwt_rq_tail->next = __lwt_rq_head->next;
	__lwt_rq_head = __lwt_rq_head->next;
	
	return ret;
}

/*--------------------------------------------------*
 *													*
 *	Scheduler functions								*
 *													*
 *--------------------------------------------------*/
static inline void __lwt_dispatch(lwt_t next, lwt_t current)
{
	__asm__ __volatile__ (
						  "pushal \n\t"								// push all registers to stack
						  "leal %c[ebp](%0), %%ebx \n\t"			// %ebx = &current->ebp
						  "movl %%ebp, (%%ebx) \n\t"				// current->ebp = %ebp
						  "movl %%esp, 0x4(%%ebx) \n\t"				// current->esp = %esp
//						  "movl 0x4(%%ebp), %%ecx \n\t"				// %ecx = %ebp + 0x4 (puts returning address of __lwt_dispatch to %ecx)
//						  "movl %%ecx, 0x8(%%ebx) \n\t"			// current->eip = %ecx
						  :
						  : "r" (current),
							LWT_STRUCT_OFFSET(ebp)
						  :
						  );

	__asm__ __volatile__ (
						  "leal %c[ebp](%0), %%ebx \n\t"			// %ebx = &next->ebp
						  "movl (%%ebx), %%ebp \n\t"				// %ebp = next->ebp
						  "movl 0x4(%%ebx), %%esp \n\t"				// %esp = next->esp
						  "popal \n\t"								// pop all registers from stack
						  :
						  : "r" (next),
						  LWT_STRUCT_OFFSET(ebp)
						  :
						  );

/*
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
 */
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
void __lwt_start(lwt_fn_t fn, void* data)
{
	fn(data);
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

	// creates stack
	new_lwt->stack_size = DEFAULT_LWT_STACK_SIZE;
	new_lwt->stack = malloc(sizeof(void) * new_lwt->stack_size);
	new_lwt->ebp = new_lwt->stack + new_lwt->stack_size;
	new_lwt->esp = new_lwt->ebp;
	new_lwt->next = NULL;
	new_lwt->id = __lwt_threadid++;
	new_lwt->status = LWT_S_READY;
	
	__lwt_rq_inqueue(new_lwt);
	
	return new_lwt;
}

void lwt_yield()
{
	// Only one thread running
	if (__lwt_rq_head == __lwt_rq_tail)
		return;
	
//	lwt_t current_lwt = __lwt_rq_head;
//	lwt_t next_lwt = __lwt_rq_dequeue();
//	
//	__lwt_dispatch(next_lwt, current_lwt);
	
	__lwt_dispatch(__main_thread, __main_thread);
}

void* lwt_join(lwt_t lwt)
{
	int *p = (int*)malloc(sizeof(int));
	*p = 0xF4;
	return p;
}


