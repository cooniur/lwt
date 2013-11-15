//
//  lwt.h
//  lwt
//
//  Created by cooniur on 10/17/13.
//  Copyright (c) 2013 cooniur. All rights reserved.
//

#ifndef lwt_h
#define lwt_h

/*==================================================*
 *													*
 *				Types Definition					*
 *													*
 *==================================================*/
/**
 LWT_NULL: Defines the marco of a NULL thread descriptor
 */
#define LWT_NULL NULL

/**
 lwt_fn_t: Type of a pointer to a thread entry function
 */
typedef void*(*lwt_fn_t)(void*);

/**
 lwt_t: Type of a pointer to a thread descriptor.
 */
typedef struct __lwt_t__ *lwt_t;

/**
 lwt_status_t: Defines the status enum of a thread.
 */
typedef enum __lwt_status_t__ lwt_status_t;

/**
 lwt_info_type_t: Defines types of thread information
 */
typedef enum __lwt_info_type_t__
{
	LWT_INFO_NTHD_RUNNABLE = 0,
	LWT_INFO_NTHD_ZOMBIES,
	LWT_INFO_NTHD_BLOCKED
} lwt_info_type_t;

/*==================================================*
 *													*
 *				Declaration							*
 *													*
 *==================================================*/

/**
 Creates a lwt thread, with the entry function pointer fn,
 and the parameter pointer data used by fn
 Returns lwt_t type
 */
lwt_t lwt_create(lwt_fn_t fn, void *data);

/**
 Yields to a specific thread. If NULL passed, yields to next available thread
 */
void lwt_yield(lwt_t target);

/**
 Kill the current thread.
 Return value is passed by data
 */
void lwt_die(void *data);

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
int lwt_join(lwt_t lwt, void **retval_ptr);

/**
 Gets the state of the lwt library
 */
size_t lwt_info(lwt_info_type_t type);

// lwt channel support
#include "lwt-chan.h"

#endif
