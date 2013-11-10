//
//  debug_print.h
//  lwt
//
//  Created by cooniur on 11/19/13.
//  Copyright (c) 2013 cooniur. All rights reserved.
//

#ifndef debug_print_h
#define debug_print_h

#include <stdio.h>

#ifdef DEBUG_PRINT
#define debug_print(f, args...) \
	fprintf(stderr, f, ## args)
#else
#define debug_print(f, args...)

#endif /* DEBUG_PRINT */
#endif /* debug_print_h */