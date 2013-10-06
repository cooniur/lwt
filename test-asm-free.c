//
//  test.c
//  lwt
//
//  Created by Tongliang Liu on 10/19/13.
//  Copyright (c) 2013 cooniur. All rights reserved.
//
#include <stdio.h>
#include <stdlib.h>
#include <mcheck.h>

void no_op(enum mcheck_status status) {}

int main()
{
	mcheck(&no_op);

	void *stack = malloc(sizeof(void) * 16);
	
    printf("%d (should be %d)\n", mprobe(stack), MCHECK_OK);
    printf("%d (should be %d)\n", mprobe(stack), MCHECK_OK);
	
	__asm__ __volatile__ (
						  "movl %0, %%ecx \n\t"
						  "movl %%ecx, (%%esp) \n\t"
						  "call free \n\t"					// free(stack);
						  :
						  : "r" (stack)
						  :
						  );
	
    printf("%d (should be %d)\n", mprobe(stack), MCHECK_FREE);
    printf("%d (should be %d)\n", mprobe(stack), MCHECK_FREE);
	
    return 0;
	
	
//	printf("%d\n");
	
	return 0;
}