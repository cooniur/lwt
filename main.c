//
//  main.c
//  lwt
//
//  Created by cooniur on 10/17/13.
//  Copyright (c) 2013 cooniur. All rights reserved.
//

#include <stdlib.h>
#include <stdio.h>
#include "lwt.h"

void show_return_value(lwt_t lwt_to_join)
{
	void *data = NULL;
	lwt_t lwt_cur = lwt_current();
	if (lwt_join(lwt_to_join, &data) == 0)
	{
		if (data)
		{
			printf("%d joined thread %d, return value is %d\n", lwt_id(lwt_cur), lwt_id(lwt_to_join), *((int*)data));
			free(data);
		}
	}
}

void* run(void *data)
{
	int *ret = (int*)malloc(sizeof(int));
	*ret = -1;
	lwt_t cur = lwt_current();
	int id = lwt_id(cur);
	printf("--> #%d begins to run", id);
	if (data == NULL)
	{
		printf(" with NULL data.\n");
		return NULL;
	}

	int i;
	int count = *((int*)data);
	printf(" with param %d.\n", count);
	for (i=0; i<count; i++)
	{
		if (i > 40)
		{
			printf("--> #%d running: Thread will die...\n", id);
			*ret = -2;
			lwt_die(ret);
		}
		
		printf("--> #%d running: [%d]\n", id, i);
		lwt_yield();
	}
	
	printf("--> #%d finished.\n", id);
	return ret;
}

void* run_2(void *data)
{
	int *ret = (int*)malloc(sizeof(int));
	*ret = -1;
	lwt_t cur = lwt_current();
	int id = lwt_id(cur);
	printf("--> #%d begins to run", id);
	if (data == NULL)
	{
		printf(" with NULL data.\n");
		return NULL;
	}
	
	int i;
	int count = *((int*)data);
	printf(" with param %d.\n", count);
	for (i=0; i<count; i+=2)
	{
		if (i > 10)
		{
			printf("--> #%d running: Thread will die...\n", id);
			*ret = -2;
			lwt_die(ret);
		}
		
		printf("--> #%d running: [%d]\n", id, i);
		lwt_yield();
	}
	
	printf("--> #%d finished.\n", id);
	return ret;
}

void* run_3(void *data)
{
	int *ret = (int*)malloc(sizeof(int));
	*ret = -1;
	lwt_t cur = lwt_current();
	int id = lwt_id(cur);
	printf("--> #%d begins to run", id);
	if (data == NULL)
	{
		printf(" with NULL data.\n");
		return NULL;
	}

	int *param = (int*)malloc(sizeof(int));
	*param = 100;
	lwt_t lwt= lwt_create(run, param);
	show_return_value(lwt);
	
	int i;
	int count = *((int*)data);
	printf(" with param %d.\n", count);
	for (i=0; i<count; i+=3)
	{
		if (i > 10)
		{
			printf("--> #%d running: Thread will die...\n", id);
			*ret = -2;
			lwt_die(ret);
		}
		
		printf("--> #%d running: [%d]\n", id, i);
		lwt_yield();
	}
	
	printf("--> #%d finished.\n", id);
	return ret;
	
}

int main(int argc, char *argv[])
{
	int a = 10;
	lwt_t lwt = lwt_create(run, &a);
	int b = 20;
	lwt_t lwt2 = lwt_create(run_2, &b);
	int c = 33;
	lwt_t lwt3 = lwt_create(run_3, &c);

	int count = 0;
	while(count < 110)
	{
		printf("Main: [%d]\n", count++);
		lwt_yield();	// should jump to the new thread we just created.
		
		show_return_value(lwt);
		show_return_value(lwt3);
	}

	show_return_value(lwt2);
	return 0;
}
