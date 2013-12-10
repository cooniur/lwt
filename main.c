//
//  main.c
//  lwt
//
//  Created by cooniur on 10/17/13.
//  Copyright (c) 2013 cooniur. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <math.h>

#include "lwt.h"
#include "debug_print.h"

#define rdtscll(val) __asm__ __volatile__("rdtsc" : "=A" (val))

#define ITER 10000
#define USE_KTHD (1)

/* 
 * My output on an Intel Core i5-2520M CPU @ 2.50GHz:
 *
 * [PERF] 120 <- fork/join
 * [PERF] 13 <- yield
 * [TEST] thread creation/join/scheduling
 * [PERF] 48 <- snd+rcv (buffer size 0)
 * [TEST] multisend (channel buffer size 0)
 * [PERF] 27 <- asynchronous snd->rcv (buffer size 100)
 * [TEST] multisend (channel buffer size 100)
 * [TEST] group wait (channel buffer size 0, grpsz 3)
 * [TEST] group wait (channel buffer size 3, grpsz 3)
 */

 /*
  * Performance of this version:
  * [PERF] 352 <- fork/join
  * [PERF] 33 <- yield
  * [TEST] thread creation/join/scheduling
  * [TEST] thread creation/join/scheduling passed.
  * [PERF] 455 <- snd+rcv (buffer size 0)
  * [TEST] multisend (channel buffer size 0)
  * [TEST] multisend (channel buffer size 0) passed.
  * [PERF] test_perf_async_steam
  * [PERF] 49 <- asynchronous snd->rcv (buffer size 100)
  * [TEST] multisend (channel buffer size 100)
  * [TEST] multisend (channel buffer size 100) passed.
  * [TEST] group wait (channel buffer size 0, grpsz 3)
  * [TEST] group wait (channel buffer size 0, grpsz 3) passed.
  * [TEST] group wait (channel buffer size 3, grpsz 3)
  * [TEST] group wait (channel buffer size 3, grpsz 3) passed. 
  */

void *
fn_bounce(void *d, lwt_chan_t c)
{
	int i;
	unsigned long long start, end;

	lwt_yield(LWT_NULL);
	lwt_yield(LWT_NULL);
	rdtscll(start);
	for (i = 0 ; i < ITER ; i++) lwt_yield(LWT_NULL);
	rdtscll(end);
	lwt_yield(LWT_NULL);
	lwt_yield(LWT_NULL);

	if (!d) printf("[PERF] %lld <- yield\n", (end-start)/(ITER*2));

	return NULL;
}

void *
fn_null(void *d, lwt_chan_t c)
{ return NULL; }

#define IS_RESET()						\
		//printf("r:%u, z:%u, b:%u\n", lwt_info(LWT_INFO_NTHD_RUNNABLE), lwt_info(LWT_INFO_NTHD_ZOMBIES), lwt_info(LWT_INFO_NTHD_BLOCKED)); \
        assert( lwt_info(LWT_INFO_NTHD_RUNNABLE) == 1 + USE_KTHD &&	\
		lwt_info(LWT_INFO_NTHD_ZOMBIES) == 0 &&		\
		lwt_info(LWT_INFO_NTHD_BLOCKED) == 0)

void
test_perf(void)
{
	lwt_t chld1, chld2;
	int i;
	unsigned long long start, end;

	/* Performance tests */
	rdtscll(start);
	for (i = 0 ; i < ITER ; i++) {
		chld1 = lwt_create(fn_null, NULL, 0, NULL);
		lwt_join(chld1, NULL);
	}
	rdtscll(end);
	printf("[PERF] %lld <- fork/join\n", (end-start)/ITER);
	IS_RESET();

	chld1 = lwt_create(fn_bounce, (void*)1, 0, NULL);
	chld2 = lwt_create(fn_bounce, NULL, 0, NULL);
	lwt_join(chld1, NULL);
	lwt_join(chld2, NULL);
	IS_RESET();
}

void *
fn_identity(void *d, lwt_chan_t c)
{ return d; }

void *
fn_nested_joins(void *d, lwt_chan_t c)
{
	lwt_t chld;

	if (d) {
		lwt_yield(LWT_NULL);
		lwt_yield(LWT_NULL);
		assert(lwt_info(LWT_INFO_NTHD_RUNNABLE) == 1 + USE_KTHD);
		lwt_die(NULL);
	}
	chld = lwt_create(fn_nested_joins, (void*)1, 0, NULL);
	lwt_join(chld, NULL);
	return NULL;
}

volatile int sched[2] = {0, 0};
volatile int curr = 0;

void *
fn_sequence(void *d, lwt_chan_t c)
{
	int i, other, val = (int)d;

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
fn_join(void *d, lwt_chan_t c)
{
	lwt_t t = (lwt_t)d;
	void* r;

	lwt_join(d, &r);
	assert(r == (void*)0x37337);
	return NULL;
}

void
test_crt_join_sched(void)
{
	lwt_t chld1, chld2;

	printf("[TEST] thread creation/join/scheduling\n");

	/* functional tests: scheduling */
	lwt_yield(LWT_NULL);

	chld1 = lwt_create(fn_sequence, (void*)1, 0, NULL);
	chld2 = lwt_create(fn_sequence, (void*)2, 0, NULL);
	lwt_join(chld2, NULL);
	lwt_join(chld1, NULL);
	IS_RESET();

	/* functional tests: join */
	chld1 = lwt_create(fn_null, NULL, 0, NULL);
	lwt_join(chld1, NULL);
	IS_RESET();

	chld1 = lwt_create(fn_null, NULL, 0, NULL);
	lwt_yield(LWT_NULL);
	lwt_join(chld1, NULL);
	IS_RESET();

	chld1 = lwt_create(fn_nested_joins, NULL, 0, NULL);
	lwt_join(chld1, NULL);
	IS_RESET();

	/* functional tests: join only from parents */
	chld1 = lwt_create(fn_identity, (void*)0x37337, 0, NULL);
	chld2 = lwt_create(fn_join, chld1, 0, NULL);
	lwt_yield(LWT_NULL);
	lwt_yield(LWT_NULL);
	lwt_join(chld2, NULL);
	lwt_join(chld1, NULL);
	IS_RESET();

	/* functional tests: passing data between threads */
	chld1 = lwt_create(fn_identity, (void*)0x37337, 0, NULL);
	void* data;
	lwt_join(chld1, &data);
	assert((void*)0x37337 == data);
	IS_RESET();

	/* functional tests: directed yield */
	chld1 = lwt_create(fn_null, NULL, 0, NULL);
	lwt_yield(chld1);
	assert(lwt_info(LWT_INFO_NTHD_ZOMBIES) == 1);
	lwt_join(chld1, NULL);
	IS_RESET();
	printf("[TEST] thread creation/join/scheduling passed.\n");
}

void *
fn_chan(void *data, lwt_chan_t c)
{
	lwt_chan_t from;
	lwt_chan_t to = data;
	int i;
	
	from = lwt_chan(0, "thread_rcving_ch");
	lwt_snd_chan(to, from);
	assert(lwt_chan_sending_count(to) == 1);

	for (i = 0 ; i < ITER ; i++) {
		lwt_snd(to, (void*)1);
		assert(2 == (int)lwt_rcv(from));
	}
	lwt_chan_deref(&from);
	
	return NULL;
}

void
test_perf_channels(int chsz)
{
	printf("[PERF] test_perf_channels: snd+rcv (buffer size %d)\n", chsz);

	lwt_chan_t from, to;
	lwt_t t;
	int i;
	unsigned long long start, end;
	size_t snd_cnt;

	assert(LWT_S_RUNNING == lwt_status(lwt_current()));
	from = lwt_chan(chsz, "main_rcving_ch");
	assert(from);
//	lwt_chan_grant(from);
	
	t    = lwt_create(fn_chan, from, 0, NULL);
	to   = lwt_rcv_chan(from);
	assert(lwt_chan_sending_count(from) == 1);
	
	rdtscll(start);
	for (i = 0 ; i < ITER ; i++) {
		assert(1 == (int)lwt_rcv(from));
		lwt_snd(to, (void*)2);
	}
	lwt_chan_deref(&to);
	rdtscll(end);
	
	lwt_join(t, NULL);
	printf("[PERF] %lld <- snd+rcv (buffer size %d)\n",
	       (end-start)/(ITER*2), chsz);
}

struct multisend_arg {
	lwt_chan_t c;
	int snd_val;
};
static int sndrcv_cnt = 0;

void *
fn_snder(void *arg, lwt_chan_t ch)
{
	struct multisend_arg *a = arg;
	lwt_chan_t c = a->c;
	int v = a->snd_val, i;

	for (i = 0 ; i < ITER ; i++) {
		lwt_snd(c, (void*)v);
		sndrcv_cnt++;
	}

	return NULL;
}

void
test_multisend(int chsz)
{
	lwt_chan_t c;
	lwt_t t1, t2;
	int i, ret[ITER*2], sum = 0, maxcnt = 0;
	struct multisend_arg args[2];

	printf("[TEST] multisend (channel buffer size %d)\n", chsz);

	c  = lwt_chan(chsz, "c");
	assert(c);
	for (i = 0 ; i < 2 ; i++) {
		args[i].c       = c;
		args[i].snd_val = i+1;
		//lwt_chan_grant(c);
	}
	t1 = lwt_create(fn_snder, &args[0], 0, NULL);
	t2 = lwt_create(fn_snder, &args[1], 0, NULL);
	for (i = 0 ; i < ITER*2 ; i++) {
		//if (i % 5 == 0) lwt_yield(LWT_NULL);
		ret[i] = (int)lwt_rcv(c);
		if (sndrcv_cnt > maxcnt) maxcnt = sndrcv_cnt;
		sndrcv_cnt--;
	}
	lwt_join(t1, NULL);
	lwt_join(t2, NULL);
	
	for (i = 0 ; i < ITER*2 ; i++) {
		sum += ret[i];
		assert(ret[i] == 1 || ret[i] == 2);
	}
	assert(sum == (ITER * 1) + (ITER*2));
	/* 
	 * This is important: Asynchronous means that the buffer
	 * should really fill up here as the senders never block until
	 * the buffer is full.  Thus the difference in the number of
	 * sends and the number of receives should vary by the size of
	 * the buffer.  If your implementation doesn't do this, it is
	 * doubtful you are really doing asynchronous communication.
	 */
	assert(maxcnt >= chsz);
	printf("[TEST] multisend (channel buffer size %d) passed.\n", chsz);
	return;
}

static int async_sz = 0;

void *
fn_async_steam(void *data, lwt_chan_t c)
{
	lwt_chan_t to = data;
	int i;
	
	for (i = 0 ; i < ITER ; i++) 
		lwt_snd(to, (void*)(i+1));

	lwt_chan_deref(&to);
	return NULL;
}

void
test_perf_async_steam(int chsz)
{
	lwt_chan_t from;
	lwt_t t;
	int i;
	unsigned long long start, end;

	printf("[PERF] test_perf_async_steam\n");

	async_sz = chsz;
	assert(LWT_S_RUNNING == lwt_status(lwt_current()));
	from = lwt_chan(chsz, "af");
	assert(from);
//	lwt_chan_grant(from);
	t = lwt_create(fn_async_steam, from, 0, NULL);
	assert(lwt_info(LWT_INFO_NTHD_RUNNABLE) == 2 + USE_KTHD);
	rdtscll(start);
	for (i = 0 ; i < ITER ; i++) 
		assert(i+1 == (int)lwt_rcv(from));
	rdtscll(end);
	lwt_join(t, NULL);
	printf("[PERF] %lld <- asynchronous snd->rcv (buffer size %d)\n",
	       (end-start)/(ITER*2), chsz);
}

void *
fn_grpwait(void *d, lwt_chan_t ch)
{
	lwt_chan_t c = d;
	int i;

	for (i = 0 ; i < ITER ; i++) {
		if ((i % 7) == 0) {
			int j;

			for (j = 0 ; j < (i % 8) ; j++) lwt_yield(LWT_NULL);
		}
		lwt_snd(c, (void*)lwt_id(lwt_current()));
	}
	return NULL;
}

#define GRPSZ 3

void
test_grpwait(int chsz, int grpsz)
{
	lwt_chan_t cs[grpsz];
	lwt_t ts[grpsz];
	int i;
	lwt_cgrp_t g;

	printf("[TEST] group wait (channel buffer size %d, grpsz %d)\n", 
	       chsz, grpsz);
	g = lwt_cgrp();
	assert(g);
	
	char name[255] = {0};
	for (i = 0 ; i < grpsz ; i++) {
		sprintf(name, "cs[%d]", i);
		cs[i] = lwt_chan(chsz, name);
		// printf("channel %p: %s\n", cs[i], lwt_chan_get_name(cs[i]));
		assert(cs[i]);
//		lwt_chan_grant(cs[i]);
		ts[i] = lwt_create(fn_grpwait, cs[i], 0, NULL);
		lwt_chan_mark_set(cs[i], (void*)lwt_id(ts[i]));
		int rc = lwt_cgrp_add(g, cs[i], LWT_CHAN_SND);
		if (rc != 0)
			printf("test_grpwait: grp[%d] error %d\n", i, rc);
	}
	assert(lwt_cgrp_free(&g) == -1);
	/**
	 * Q: why don't we iterate through all of the data here?
	 * 
	 * A: We need to fix 1) cevt_wait to be level triggered, or 2)
	 * provide a function to detect if there is data available on
	 * a channel.  Either of these would allows us to iterate on a
	 * channel while there is more data pending.
	 */
	for (i = 0 ; i < ((ITER * grpsz)-(grpsz*chsz)) ; i++) {
		lwt_chan_t c;
		int r;

		lwt_chan_dir_t dir;
		c = lwt_cgrp_wait(g, &dir);
		if (dir == LWT_CHAN_RCV)
		{
			assert(c);
			r = (int)lwt_rcv(c);
			assert(r == (int)lwt_chan_mark_get(c));
		}
	}

	for (i = 0 ; i < grpsz ; i++) {
		lwt_cgrp_rem(g, cs[i]);
		lwt_join(ts[i], NULL);
		lwt_chan_deref(&cs[i]);
	}

	// printf("main: free grp\n");
	assert(!lwt_cgrp_free(&g));
	printf("[TEST] group wait (channel buffer size %d, grpsz %d) passed.\n",
	       chsz, grpsz);
	return;
}

void* fn_kthd_test(void* data, lwt_chan_t c)
{
	printf("%p: lwt running. ready to rcv...\n", lwt_current());
	lwt_chan_t to = lwt_rcv(c);

	for (int i=0; i<ITER; i++)
	{
		assert((void*)0x123 == lwt_rcv(c));
	}
	lwt_chan_deref(&c);

	for (int i=0; i<ITER; i++)
	{
		assert(lwt_snd(to, (void*)0x234) == 0);
	}
	lwt_chan_deref(&to);
	printf("lwt end\n");
	return NULL;
}

int
main(void)
{
/*
	test_perf();
	test_crt_join_sched();
	test_perf_channels(0);
	test_multisend(0);
	test_perf_async_steam(ITER/10 < 100 ? ITER/10 : 100);
	test_multisend(ITER/10 < 100 ? ITER/10 : 100);
	test_grpwait(0, 3);
	test_grpwait(3, 3);
*/
	printf("%p: main\n", lwt_current());

	lwt_chan_t c = lwt_chan(0, "r");
	lwt_chan_t from = lwt_chan(10, "r");
	lwt_cgrp_t g = lwt_cgrp();
	lwt_cgrp_add(g, from, LWT_CHAN_SND);
	
	int rc = lwt_kthd_create(&fn_kthd_test, NULL, c);
	printf("%p: lwt_kthd_create: rc=%d\n", lwt_current(), rc);

	assert(lwt_snd(c, from) == 0);

	for (int i=0; i<ITER; i++)
	{
		assert(lwt_snd(c, (void*)0x123) == 0);
	}
	lwt_chan_deref(&c);

	lwt_chan_t rcvable;
	lwt_chan_dir_t dir;
	for (int i=0; i<ITER; i++)
	{
		rcvable = lwt_cgrp_wait(g, &dir);
		assert(lwt_rcv(rcvable) == (void*)0x234);
	}
	lwt_chan_deref(&from);
	printf("end\n");
	getchar();

	assert(lwt_chan_sending_count(c) == 0);
	assert(lwt_chan_sending_count(from) == 0);

	return 0;
}
