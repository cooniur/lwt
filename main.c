#include <stdlib.h>
#include <stdio.h>
#include "lwt-test.h"
#include "lwt.h"

void *run(void* data)
{
	if (data != NULL)
	{
		int i = 10;
		int count = *((int*)data);
		for (i=0; i<count; i++)
		{
			if (i > 3)
				lwt_die(NULL);

			printf("--> run: loop #%d\n", i);
			lwt_yield();
		}
	}
	return NULL;
}

int main(int argc, char *argv[])
{
	int p = 10;
//	lwt_fn_t fn = run;
//	fn(&p);
	
	lwt_t lwt = lwt_create(run, &p);
	int count = 0;
	while(count < 20)
	{
		printf("Main: loop #%d\n", count++);
		lwt_yield();	// should jump to the new thread we just created.
	}
	

//	printf("0x%X\n", p);
//	
//	void* ptr = lwt_join(lwt);
//	p = *((unsigned int*)ptr);
//	printf("0x%X\n", p);

	return 0;
}
