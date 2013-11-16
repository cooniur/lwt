//
//  hashtable.h
//  lwt
//
//  Created by Tongliang Liu on 11/10/13.
//  Copyright (c) 2013 cooniur. All rights reserved.
//

#ifndef lwt_hashtable_h
#define lwt_hashtable_h

typedef struct __hashtable_t__ hashtable_t;

hashtable_t *hashtable_init();
hashtable_t *hashtable_init_p(size_t sz, double load_factor);

#endif
