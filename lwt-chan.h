//
//  lwt-chan.h
//  lwt
//
//  Created by Tongliang Liu on 11/15/13.
//  Copyright (c) 2013 cooniur. All rights reserved.
//

#ifndef lwt_lwt_chan_h
#define lwt_lwt_chan_h

typedef struct __lwt_chan_t__ *lwt_chan_t;

lwt_chan_t lwt_chan(const char* name);

const char *lwt_chan_get_name(lwt_chan_t c);

/**
 Returns -1: channel c is NULL
 Returns 1: channel c is freed;
 Returns 0: channel c is not freed;
 */
int lwt_chan_deref(lwt_chan_t *c);

/**
 Returns -1: no existing receiver
 Returns -2: cannot sending to itself
 */
int lwt_snd(lwt_chan_t c, void *data);
int lwt_snd_chan(lwt_chan_t c, lwt_chan_t sc);

void *lwt_rcv(lwt_chan_t c);
lwt_chan_t lwt_rcv_chan(lwt_chan_t c);


#endif
