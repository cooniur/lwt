//
//  hashtable.c
//  lwt
//
//  Created by Tongliang Liu on 11/10/13.
//  Copyright (c) 2013 cooniur. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include "hashtable.h"

#define HASHTABLE_DEFAULT_SZ		(1009)
#define HASHTABLE_DEFAULT_FACTOR	(0.75)

#define ht_hashcode_t unsigned long

struct __hashtable_pair_t__
{
	char *key;
	char *value;
	struct __hashtable_pair_t__ *next;
};

struct __hashtable_t__
{
	size_t sz;
	double load_factor;
	size_t sz_threhold;
	size_t count;
	struct __hashtable_pair_t__ **list;
};

static ht_hashcode_t __hashtable_get_hashcode(const char *str);
static size_t __hashtable_get_index(hashtable_t *table, ht_hashcode_t hashcode);
static struct __hashtable_pair_t__ *__hashtable_pair_init(const char *key, const char *value);

hashtable_t *hashtable_init()
{
	return hashtable_init_p(HASHTABLE_DEFAULT_SZ, HASHTABLE_DEFAULT_FACTOR);
}

hashtable_t *hashtable_init_p(size_t sz, double load_factor)
{
	hashtable_t *ret = malloc(sizeof(struct __hashtable_t__));
	ret->sz = sz;
	ret->load_factor = load_factor;
	ret->sz_threhold = sz * load_factor;
	ret->list = calloc(ret->sz, sizeof(struct __hashtable_pair_t__*));
	return ret;
}

void hashtable_free(hashtable_t **table)
{
	if (table && (*table))
	{
		for (int i=0; i<(*table)->sz; i++)
		{
			struct __hashtable_pair_t__ *pair = (*table)->list[i];
			struct __hashtable_pair_t__ *next = NULL;
			while (1)
			{
				if (pair)
				{
					next = pair->next;
					free(pair);
					pair = next;
				}
				else
					break;
			}
		}
		free((*table)->list);
		free(*table);
		*table = NULL;
	}
}

static size_t __hashtable_get_index(hashtable_t *table, ht_hashcode_t hashcode)
{
	return hashcode % (table->sz);
}

static ht_hashcode_t __hashtable_get_hashcode(const char *str)
{
	ht_hashcode_t hash = 5381;
	int c;
	
	while ((c = *str++))
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
	
	return hash;
}