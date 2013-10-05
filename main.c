#include <stdlib.h>
#include <stdio.h>
#include "lwt-test.h"
#include "lwt.h"

void *run(void* data)
{
	if (data != NULL)
	{
		int* p = (int*)data;
		printf("0x%X\n", *p);
		
	}
	return NULL;
}

int main(int argc, char *argv[])
{
	int p = 0xAA;
	lwt_t lwt = lwt_create(run, &p);
	
	return 0;
}
