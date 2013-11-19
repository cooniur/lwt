//
//  lwt.h
//  lwt
//
//  Created by cooniur on 10/17/13.
//  Copyright (c) 2013 cooniur. All rights reserved.
//

#ifndef lwt_h
#define lwt_h

/**
 LWT_NULL: Defines the marco of a NULL thread descriptor
 */
#define LWT_NULL (NULL)

/**
 lwt_fn_t: Type of a pointer to a thread entry function
 */
typedef void*(*lwt_fn_t)(void*);

/**
 lwt_t: Type of a pointer to a thread descriptor.
 */
typedef struct __lwt_t__* lwt_t;

/**
 lwt_status_t: Defines the status enum of a thread.
 */
typedef enum __lwt_status_t__
{
	LWT_S_CREATED = 0,		// Thread is just created. Stack is empty
	LWT_S_READY,			// Thread is switched out, and ready to be switched to
	LWT_S_RUNNING,			// Thread is running
	LWT_S_BLOCKED,			// Thread is blocked and in wait queue
	LWT_S_FINISHED,			// Thread is finished and is ready to be joined
	LWT_S_DEAD				// Thread is joined and finally dead.
}lwt_status_t;

typedef enum __lwt_flags_t__
{
	LWT_F_NONE = 0,
	LWT_F_NOJOIN = 1
} lwt_flags_t;

/**
 lwt_info_type_t: Defines types of thread information
 */
typedef enum __lwt_info_type_t__
{
	LWT_INFO_NTHD_RUNNABLE = 0,
	LWT_INFO_NTHD_ZOMBIES,
	LWT_INFO_NTHD_BLOCKED
} lwt_info_type_t;

/**
 Creates a lwt thread, with the entry function pointer fn,
 and the parameter pointer data used by fn
 Returns lwt_t type
 */
lwt_t lwt_create(lwt_fn_t fn, void* data, lwt_flags_t flags);

lwt_status_t lwt_status(lwt_t lwt);

/**
 Yields to a specific thread. If NULL passed, yields to next available thread
 */
void lwt_yield(lwt_t target);

/**
 Kill the current thread.
 Return value is passed by data
 */
void lwt_die(void* data);

/**
 Gets the current thread lwt_t
 */
lwt_t lwt_current();

/**
 Gets the thread id of a specified thread.
 Returns -1 if the thread not exists
 */
int lwt_id(lwt_t lwt);

/**
 Joins a specified thread and waits for its termination.
 The pointer to the returned value will be passed via retval_ptr
 Returns -1 if fails to join; otherwise, 0
 */
int lwt_join(lwt_t lwt, void** retval_ptr);

/**
 Gets the state of the lwt library
 */
size_t lwt_info(lwt_info_type_t type);

// ===================================================================
// lwt channel
// ===================================================================
typedef struct __lwt_cgrp_t__* lwt_cgrp_t;

typedef struct __lwt_chan_t__* lwt_chan_t;

lwt_chan_t lwt_chan(size_t sz, const char* name);

/**
 Returns -1: channel c is NULL
 Returns 1: channel c is freed;
 Returns 0: channel c is not freed;
 */
int lwt_chan_deref(lwt_chan_t* c);

const char* lwt_chan_get_name(lwt_chan_t c);

/**
 Returns -1: no existing receiver
 Returns -2: cannot sending to itself
 */
int lwt_snd(lwt_chan_t c, void* data);
int lwt_snd_chan(lwt_chan_t c, lwt_chan_t sc);

void* lwt_rcv(lwt_chan_t c);
lwt_chan_t lwt_rcv_chan(lwt_chan_t c);

size_t lwt_chan_sending_count(lwt_chan_t c);

void* lwt_chan_mark_get(lwt_chan_t c);
void lwt_chan_mark_set(lwt_chan_t c, void* tag);

lwt_cgrp_t lwt_cgrp();
int lwt_cgrp_free(lwt_cgrp_t* grp);
int lwt_cgrp_add(lwt_cgrp_t grp, lwt_chan_t c);
int lwt_cgrp_rem(lwt_cgrp_t grp, lwt_chan_t c);
lwt_chan_t lwt_cgrp_wait(lwt_cgrp_t grp);

#endif
