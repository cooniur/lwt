//
//  kthd_pool.h
//  lwt
//
//  Created by Tongliang Liu on 12/10/13.
//  Copyright (c) 2013 cooniur. All rights reserved.
//

#ifndef lwt_kthd_pool_h
#define lwt_kthd_pool_h

#include "lwt.h"

typedef struct __kp_t__ *kp_t;

kp_t kp_create();
void kp_work(kp_t pool, lwt_fn_t work, lwt_chan_t c);
void kp_destroy(kp_t pool);

#endif
