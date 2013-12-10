//
//  kthd_pool.c
//  lwt
//
//  Created by Tongliang Liu on 12/10/13.
//  Copyright (c) 2013 cooniur. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>

#include "kthd_pool.h"

struct __kp_t__
{
	lwt_chan_t mng_ch;
	int dead;
};

struct __kp_request__
{
	lwt_fn_t work;
	lwt_chan_t c;
	int kill_sign;
};

struct __kp_request__* __kp_request_init(lwt_fn_t work, lwt_chan_t c)
{
	struct __kp_request__* req = malloc(sizeof(struct __kp_request__));
	req->work = work;
	req->c = c;
	req->kill_sign = 0;
	return req;
}


void* __kp_manager(void* reserved, lwt_chan_t from)
{
	while (1)
	{
		struct __kp_request__* req = lwt_rcv(from);
		if (req)
		{
			if (req->kill_sign)
				return NULL;
			
			lwt_kthd_create(req->work, NULL, req->c);
		}
	}
	return NULL;
}

kp_t kp_create()
{
	kp_t pool = malloc(sizeof(struct __kp_t__));
	pool->mng_ch = lwt_chan(0, "mng_ch");
	pool->dead = 0;
	lwt_kthd_create(__kp_manager, NULL, pool->mng_ch);
	return pool;
}

void kp_work(kp_t pool, lwt_fn_t work, lwt_chan_t c)
{
	if (pool->dead)
		return;
	
	void* request = __kp_request_init(work, c);
	lwt_snd(pool->mng_ch, request);
}

void kp_destroy(kp_t pool)
{
	struct __kp_request__* req = __kp_request_init(NULL, NULL);
	req->kill_sign = 1;
	lwt_snd(pool->mng_ch, req);
	pool->dead = 1;
}
