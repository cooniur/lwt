//
//  main.c
//  lwt
//
//  Created by cooniur on 10/17/13.
//  Copyright (c) 2013 cooniur. All rights reserved.
//

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "lwt.h"

#define rdtscll(val) __asm__ __volatile__("rdtsc" : "=A" (val))

#define ITER 10000

/*
 * Professor's performance on an Intel Core i5-2520M CPU @ 2.50GHz:
 * Overhead for fork/join is 105
 * Overhead of yield is 26
 * Overhead of yield is 26
 */

int thd_cnt = 0;

void *
fn_bounce(void *d)
{
	int i;
	unsigned long long start, end;
	
	thd_cnt++;
	lwt_yield(LWT_NULL);
	lwt_yield(LWT_NULL);
	rdtscll(start);
	for (i = 0 ; i < ITER ; i++) lwt_yield(LWT_NULL);
	rdtscll(end);
	lwt_yield(LWT_NULL);
	lwt_yield(LWT_NULL);
	
	if (!d) printf("Overhead of yield is %lld\n", (end-start)/ITER);
	
	return NULL;
}

void *
fn_null(void *d)
{ thd_cnt++; return NULL; }

void *
fn_identity(void *d)
{ thd_cnt++; return d; }

void *
fn_nested_joins(void *d)
{
	lwt_t chld;
	
	thd_cnt++;
	if (d) {
		lwt_yield(LWT_NULL);
		lwt_yield(LWT_NULL);
		assert(lwt_info(LWT_INFO_NTHD_RUNNABLE) == 1);
		lwt_die(NULL);
	}
	chld = lwt_create(fn_nested_joins, (void*)1);
	void* ret = NULL;
	lwt_join(chld, &ret);
}

volatile int sched[2] = {0, 0};
volatile int curr = 0;

void *
fn_sequence(void *d)
{
	int i, other, val = (int)d;
	
	thd_cnt++;
	for (i = 0 ; i < ITER ; i++) {
		other = curr;
		curr  = (curr + 1) % 2;
		sched[curr] = val;
		assert(sched[other] != val);
		lwt_yield(LWT_NULL);
	}
	
	return NULL;
}

void *
fn_join(void *d)
{
	lwt_t t = (lwt_t)d;
	void *r;
	
	thd_cnt++;
	lwt_join(t, &r);
	assert(r == (void*)0x37337);
}

#define IS_RESET()						\
assert( lwt_info(LWT_INFO_NTHD_RUNNABLE) == 1 &&	\
lwt_info(LWT_INFO_NTHD_ZOMBIES) == 0 &&		\
lwt_info(LWT_INFO_NTHD_BLOCKED) == 0)

volatile lwt_chan_t public_c = NULL;

void *fn_snd(void *d)
{
	lwt_t lwt = lwt_current();
	public_c = lwt_chan(0, "pu");

	printf("%p: 1. receive channel via %s.\n", lwt, lwt_chan_get_name(public_c));
	printf("%p: receiving via %s...\n", lwt, lwt_chan_get_name(public_c));
	lwt_chan_t snd_c = lwt_rcv_chan(public_c);
	printf("%p: received %s via %s.\n", lwt,  lwt_chan_get_name(snd_c), lwt_chan_get_name(public_c));

	printf("%p: 2. send count via %s.\n", lwt, lwt_chan_get_name(snd_c));
	int count = 10;
	printf("%p: sending count = %d via channel %s...\n", lwt, count, lwt_chan_get_name(snd_c));
	lwt_snd(snd_c, &count);
	printf("%p: finished sending count = %d via channel %s.\n", lwt, count, lwt_chan_get_name(snd_c));
	
	printf("%p: 3. sending loop via %s.\n", lwt, lwt_chan_get_name(snd_c));
	for (int i=0; i<count; i++)
	{
		int *data = malloc(sizeof(int));
		*data = i * 2;
		printf("%p: sending %d...\n", lwt, *data);
		lwt_snd(snd_c, data);
		printf("%p: %d sent.\n", lwt, *data);
	}

	if (lwt_chan_deref(&snd_c))
		printf("channel is freed.\n");
	printf("%p: 4. end\n", lwt);
	
	return NULL;
}
 
void *fn_rcv(void *d)
{
	lwt_t lwt = lwt_current();
	lwt_chan_t rcv_c = lwt_chan(5, "sn");

	printf("%p: 1. send %s via %s.\n", lwt, lwt_chan_get_name(rcv_c), lwt_chan_get_name(public_c));

	printf("%p: sending %s via %s...\n", lwt, lwt_chan_get_name(rcv_c), lwt_chan_get_name(public_c));
	lwt_snd_chan(public_c, rcv_c);
	printf("%p: finished sending %s via %s.\n", lwt, lwt_chan_get_name(rcv_c), lwt_chan_get_name(public_c));
	
	printf("%p: 2. receive count via %s.\n", lwt, lwt_chan_get_name(rcv_c));
	printf("%p: receiving count via %s...\n", lwt, lwt_chan_get_name(rcv_c));
	int *count = lwt_rcv(rcv_c);
	printf("%p: finished receiving count = %d via %s.\n", lwt, *count, lwt_chan_get_name(rcv_c));

	printf("%p: 3. receiving loop via %s.\n", lwt, lwt_chan_get_name(rcv_c));
	for (int i=0; i<*count; i++)
	{
		printf("%p: receiving...\n", lwt);
		int *d = lwt_rcv(rcv_c);
		printf("%p: %d\n", lwt, *d);
		free(d);
	}

	printf("%p: 4. end\n", lwt);
	if (lwt_chan_deref(&rcv_c))
		printf("channel is freed.\n");
	
	return NULL;
}

void test_lwt()
{
	lwt_t chld1, chld2;
	int i;
	unsigned long long start, end;
	void* data;

	/* Performance tests */
	rdtscll(start);
	for (i = 0 ; i < ITER ; i++) {
		chld1 = lwt_create(fn_null, NULL);
		lwt_join(chld1, NULL);
	}
	rdtscll(end);
	printf("Overhead for fork/join is %lld\n", (end-start)/ITER);
	IS_RESET();
	
	chld1 = lwt_create(fn_bounce, NULL);
	chld2 = lwt_create(fn_bounce, NULL);
	lwt_join(chld1, NULL);
	lwt_join(chld2, NULL);
	IS_RESET();
	
	/* functional tests: scheduling */
	lwt_yield(LWT_NULL);
	
	chld1 = lwt_create(fn_sequence, (void*)1);
	chld2 = lwt_create(fn_sequence, (void*)2);
	lwt_join(chld2, NULL);
	lwt_join(chld1, NULL);
	IS_RESET();
	
	/* functional tests: join */
	chld1 = lwt_create(fn_null, NULL);
	lwt_join(chld1, NULL);
	IS_RESET();
	
	chld1 = lwt_create(fn_null, NULL);
	lwt_yield(LWT_NULL);
	lwt_join(chld1, NULL);
	IS_RESET();
	
	chld1 = lwt_create(fn_nested_joins, NULL);
	lwt_join(chld1, NULL);
	IS_RESET();
	
	/* functional tests: join only from parents */
	chld1 = lwt_create(fn_identity, (void*)0x37337);
	chld2 = lwt_create(fn_join, chld1);
	lwt_yield(LWT_NULL);
	lwt_yield(LWT_NULL);
	lwt_join(chld2, NULL);
	lwt_join(chld1, NULL);
	IS_RESET();
	
	/* functional tests: passing data between threads */
	chld1 = lwt_create(fn_identity, (void*)0x37337);
	lwt_join(chld1, &data);
	assert((void*)0x37337 == data);
	IS_RESET();
	
	/* functional tests: directed yield */
	chld1 = lwt_create(fn_null, NULL);
	lwt_yield(chld1);
	assert(lwt_info(LWT_INFO_NTHD_ZOMBIES) == 1);
	lwt_join(chld1, NULL);
	IS_RESET();
	
	assert(thd_cnt == ITER+12);
}

void test_lwt_chan()
{
	lwt_t chld1, chld2;
	void* data;
	
	chld1 = lwt_create(fn_snd, NULL);
	chld2 = lwt_create(fn_rcv, NULL);
	printf("%p, %p, %p\n", lwt_current(), chld1, chld2);
	lwt_yield(chld1);
	
	lwt_join(chld2, &data);
	lwt_join(chld1, &data);
}

void* fn_print(void* data)
{
	int* i = data;
	printf("Thread: %d\n", *i);
	*i += 1;
	return i;
}

int main(int argc, char *argv[])
{
	test_lwt();
	
	int data = 10;
	printf("start\n");
	
	lwt_t chld1 = lwt_create(fn_print, &data);
	lwt_yield(chld1);
	void* ret;
	lwt_join(chld1, &ret);
	int* i = ret;
	printf("%d, end\n", *i);
	
	test_lwt_chan();

	return 0;
}
