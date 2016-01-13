/* 
 *  ircd-ratbox: A slightly useful ircd.
 *  hash.c: Maintains hashtables.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002-2012 ircd-ratbox development team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 *  USA
 *
 *  $Id$
 */

#include <stdinc.h>
#include <struct.h>
#include <hash.h>
#include <s_conf.h>
#include <channel.h>
#include <client.h>
#include <match.h>
#include <ircd.h>
#include <numeric.h>
#include <send.h>
#include <cache.h>
#include <s_newconf.h>
#include <s_log.h>
#include <s_stats.h>


/* Magic value for FNV hash functions */
#define FNV1_32_INIT 0x811c9dc5UL

#define HELP_MAX_BITS 7
#define HELP_MAX (1<<HELP_MAX_BITS)

/* Client hash table size, used in hash.c/s_debug.c */
#define U_MAX_BITS 17
#define U_MAX (1<<U_MAX_BITS)

/* Client connid hash table size, used in hash.c */
#define CLI_CONNID_MAX_BITS 12
#define CLI_CONNID_MAX (1<<CLI_CONNID_MAX_BITS)

#define CLI_ZCONNID_MAX_BITS 7
#define CLI_ZCONNID_MAX (1<<CLI_ZCONNID_MAX_BITS)

/* Channel hash table size, hash.c/s_debug.c */
#define CH_MAX_BITS 16
#define CH_MAX (1<<CH_MAX_BITS)	/* 2^16 */

/* hostname hash table size */
#define HOST_MAX_BITS 17
#define HOST_MAX (1<<HOST_MAX_BITS)	/* 2^17 */

/* RESV/XLINE hash table size, used in hash.c */
#define R_MAX_BITS 10
#define R_MAX (1<<R_MAX_BITS)	/* 2^10 */

/* operhash */
#define OPERHASH_MAX_BITS 10
#define OPERHASH_MAX (1<<OPERHASH_MAX_BITS)

/* scache hash */
#define SCACHE_MAX_BITS 8
#define SCACHE_MAX (1<<SCACHE_MAX_BITS)

/* whowas hash */
#define WHOWAS_MAX_BITS 17
#define WHOWAS_MAX (1<<WHOWAS_MAX_BITS)

/* monitor hash */
#define MONITOR_MAX_BITS 16
#define MONITOR_MAX (1<<MONITOR_MAX_BITS)

/* command hash */
#define COMMAND_MAX_BITS 9
#define COMMAND_MAX (1<<COMMAND_MAX_BITS)

static rb_dlink_list *clientbyconnidTable[CLI_CONNID_MAX];
static rb_dlink_list *clientbyzconnidTable[CLI_ZCONNID_MAX];
static rb_dlink_list *clientTable[U_MAX];
static rb_dlink_list *channelTable[CH_MAX];
static rb_dlink_list *idTable[U_MAX];
static rb_dlink_list *resvTable[R_MAX];
static rb_dlink_list *hostTable[HOST_MAX];
static rb_dlink_list *helpTable[HELP_MAX];
static rb_dlink_list *ohelpTable[HELP_MAX];
static rb_dlink_list *ndTable[U_MAX];
static rb_dlink_list *operhash_table[OPERHASH_MAX];
static rb_dlink_list *scache_table[SCACHE_MAX];
static rb_dlink_list *whowas_table[WHOWAS_MAX];
static rb_dlink_list *monitor_table[MONITOR_MAX];
static rb_dlink_list *command_table[COMMAND_MAX];

/*
 * Hashing.
 *
 *   The server uses a chained hash table to provide quick and efficient
 * hash table maintenance (providing the hash function works evenly over
 * the input range).  The hash table is thus not susceptible to problems
 * of filling all the buckets or the need to rehash.
 *    It is expected that the hash table would look something like this
 * during use:
 *		     +-----+	+-----+	   +-----+   +-----+
 *		  ---| 224 |----| 225 |----| 226 |---| 227 |---
 *		     +-----+	+-----+	   +-----+   +-----+
 *			|	   |	      |
 *		     +-----+	+-----+	   +-----+
 *		     |	A  |	|  C  |	   |  D	 |
 *		     +-----+	+-----+	   +-----+
 *			|
 *		     +-----+
 *		     |	B  |
 *		     +-----+
 *
 * A - GOPbot, B - chang, C - hanuaway, D - *.mu.OZ.AU
 *
 * The order shown above is just one instant of the server. 
 *
 *
 * The hash functions currently used are based Fowler/Noll/Vo hashes
 * which work amazingly well and have a extremely low collision rate
 * For more info see http://www.isthe.com/chongo/tech/comp/fnv/index.html
 *
 * 
 */

/* init_hash()
 *
 * clears the various hashtables
 */
void
init_hash(void)
{
	/* nothing to do here */
}

/* fnv_hash_len_data hashses any data */
static uint32_t
fnv_hash_len_data(const unsigned char *s, unsigned int bits, size_t len)
{
	uint32_t h = FNV1_32_INIT;
	bits = 32 - bits;
	const unsigned char *x = s + len;
	while(s < x)
	{
		h ^= *s++;
		h += (h << 1) + (h << 4) + (h << 7) + (h << 8) + (h << 24);
	}
	h = (h >> bits) ^ (h & ((2 ^ bits) - 1));
	return h;
}

static uint32_t
fnv_hash_upper(const unsigned char *s, unsigned int bits, size_t unused)
{
	uint32_t h = FNV1_32_INIT;
	bits = 32 - bits;
	while(*s)
	{
		h ^= ToUpper(*s++);
		h += (h << 1) + (h << 4) + (h << 7) + (h << 8) + (h << 24);
	}
	h = (h >> bits) ^ (h & ((2 ^ bits) - 1));
	return h;
}

static uint32_t
fnv_hash(const unsigned char *s, unsigned int bits, size_t unused)
{
	uint32_t h = FNV1_32_INIT;
	bits = 32 - bits;
	while(*s)
	{
		h ^= *s++;
		h += (h << 1) + (h << 4) + (h << 7) + (h << 8) + (h << 24);
	}
	h = (h >> bits) ^ (h & ((2 ^ bits) - 1));
	return h;
}

#if 0				/* unused currently */

static uint32_t
fnv_hash_len(const unsigned char *s, unsigned int bits, size_t len)
{
	uint32_t h = FNV1_32_INIT;
	bits = 32 - bits;
	const unsigned char *x = s + len;
	while(*s && s < x)
	{
		h ^= *s++;
		h += (h << 1) + (h << 4) + (h << 7) + (h << 8) + (h << 24);
	}
	h = (h >> bits) ^ (h & ((2 ^ bits) - 1));
	return h;
}
#endif

static uint32_t
fnv_hash_upper_len(const unsigned char *s, unsigned int bits, size_t len)
{
	uint32_t h = FNV1_32_INIT;
	bits = 32 - bits;
	const unsigned char *x = s + len;
	while(*s && s < x)
	{
		h ^= ToUpper(*s++);
		h += (h << 1) + (h << 4) + (h << 7) + (h << 8) + (h << 24);
	}
	h = (h >> bits) ^ (h & ((2 ^ bits) - 1));
	return h;
}

static void
free_hashnode(hash_node * hnode)
{
	rb_free(hnode->key);
	rb_free(hnode);
}

typedef bool hash_cmp(const void *x, const void *y, size_t len);

typedef enum
{
	CMP_IRCCMP = 0,
	CMP_STRCMP = 1,
	CMP_MEMCMP = 2,
} hash_cmptype;

struct _hash_function
{
	const char *name;
	uint32_t(*func) (unsigned const char *, unsigned int, size_t);
	hash_cmptype cmptype;
	rb_dlink_list **htable;
	unsigned int hashbits;
	unsigned int hashlen;
};



/* *INDENT-OFF* */  
/* if the cmpfunc field is not set, it is assumed hash_irccmp will be used */

static struct _hash_function hash_function[HASH_LAST] =
{
	[HASH_CLIENT] = { 
		.name = "NICK", .func = fnv_hash_upper, .htable = clientTable, .hashbits = U_MAX_BITS, 
	},
	[HASH_ID] = { 
		.name = "ID", .func = fnv_hash, .htable = idTable, .hashbits = U_MAX_BITS, .cmptype = CMP_STRCMP,
	},
	[HASH_CHANNEL] = { 
		.name = "Channel", .func = fnv_hash_upper_len, .htable = channelTable, .hashbits = CH_MAX_BITS, .hashlen = 30,
	},
	[HASH_HOSTNAME] = { 
		.name = "Host", .func = fnv_hash_upper_len, .htable = hostTable, .hashbits = HOST_MAX_BITS, .hashlen = 30
	},
	[HASH_RESV] = { 
		.name = "Channel RESV", .func = fnv_hash_upper_len, .htable = resvTable, .hashbits = R_MAX_BITS, .hashlen = 30
	},
	[HASH_OPER] = {
		.name = "Operator", .func = fnv_hash_upper, .htable = operhash_table, .hashbits = OPERHASH_MAX_BITS,
	},
	[HASH_SCACHE] = {
		.name = "Server Cache", .func = fnv_hash_upper, .htable = scache_table, .hashbits = SCACHE_MAX_BITS, 
	},
	[HASH_HELP] = {
		.name = "Help", .func = fnv_hash_upper, .htable = helpTable, .hashbits = HELP_MAX_BITS, 
	},
	[HASH_OHELP] = {
		.name = "Operator Help", .func = fnv_hash_upper, .htable = ohelpTable, .hashbits = HELP_MAX_BITS, 
	},
	[HASH_ND] = {
		.name = "Nick Delay", .func = fnv_hash_upper, .htable = ndTable, .hashbits = U_MAX_BITS,
	},
	[HASH_CONNID] = {
		.name = "Connection ID", .func = fnv_hash_len_data, .htable = clientbyconnidTable, .hashbits = CLI_CONNID_MAX_BITS, .cmptype = CMP_MEMCMP, .hashlen = sizeof(uint32_t)
	},
	[HASH_ZCONNID] = {
		.name = "Ziplinks ID", .func = fnv_hash_len_data, .htable = clientbyzconnidTable, .hashbits = CLI_ZCONNID_MAX_BITS, .cmptype = CMP_MEMCMP, .hashlen = sizeof(uint32_t)
	},
	[HASH_WHOWAS] = { 
		.name = "WHOWAS", .func = fnv_hash_upper, .htable = whowas_table, .hashbits = WHOWAS_MAX_BITS, 
	},
	[HASH_MONITOR] = {
		.name = "MONITOR", .func = fnv_hash_upper, .htable = monitor_table, .hashbits = MONITOR_MAX_BITS
	},
	[HASH_COMMAND] = {
		.name = "Command", .func = fnv_hash_upper, .htable = command_table, .hashbits = COMMAND_MAX_BITS
	},

};
/* *INDENT-ON* */  

#define hfunc(type, hashindex, hashlen) (hash_function[type].func((unsigned const char *)hashindex, hash_function[type].hashbits, hashlen))

static inline int
hash_do_cmp(hash_type type, const void *x, const void *y, size_t len)
{
	hash_cmptype cmptype = hash_function[type].cmptype;

	switch (cmptype)
	{
	case CMP_IRCCMP:
		return irccmp(x, y);
	case CMP_STRCMP:
		return strcmp(x, y);
	case CMP_MEMCMP:
		return memcmp(x, y, len);
	}
	return -1;
}

void
hash_free_list(rb_dlink_list * table)
{
	rb_dlink_node *ptr, *next;
	RB_DLINK_FOREACH_SAFE(ptr, next, table->head)
	{
		rb_free(ptr);
	}
	rb_free(table);
}

rb_dlink_list *
hash_find_list_len(hash_type type, const void *hashindex, size_t size)
{
	rb_dlink_list *bucket;
	rb_dlink_list *results;
	size_t hashlen;
	uint32_t hashv;
	rb_dlink_node *ptr;

	if(hashindex == NULL)
		return NULL;

	if(hash_function[type].hashlen == 0)
		hashlen = size;
	else
		hashlen = IRCD_MIN(size, hash_function[type].hashlen);

	hashv = hfunc(type, hashindex, hashlen);
	
	if(hash_function[type].htable[hashv] == NULL)
		return NULL;

	bucket = hash_function[type].htable[hashv];

	results = rb_malloc(sizeof(rb_dlink_list));

	RB_DLINK_FOREACH(ptr, bucket->head)
	{
		hash_node *hnode = ptr->data;
		if(hash_do_cmp(type, hashindex, hnode->key, hashlen) == 0)
			rb_dlinkAddAlloc(hnode->data, results);
	}
	if(rb_dlink_list_length(results) == 0)
	{
		rb_free(results);
		return NULL;
	}
	return results;
}

rb_dlink_list *
hash_find_list(hash_type type, const char *hashindex)
{
	if(EmptyString(hashindex))
		return NULL;
	return hash_find_list_len(type, hashindex, strlen(hashindex) + 1);
}

hash_node *
hash_find_len(hash_type type, const void *hashindex, size_t size)
{
	rb_dlink_list *bucket; // = hash_function[type].table;

	size_t hashlen;
	uint32_t hashv;
	rb_dlink_node *ptr;

	if(hashindex == NULL)
		return NULL;

	if(hash_function[type].hashlen == 0)
		hashlen = size;
	else
		hashlen = IRCD_MIN(size, hash_function[type].hashlen);

	hashv = hfunc(type, hashindex, hashlen);
	
	if(hash_function[type].htable[hashv] == NULL)
		return NULL;
	
	bucket = hash_function[type].htable[hashv];

	RB_DLINK_FOREACH(ptr, bucket->head)
	{
		hash_node *hnode = ptr->data;

		if(hash_do_cmp(type, hashindex, hnode->key, hashlen) == 0)
			return hnode;
	}
	return NULL;
}

hash_node *
hash_find(hash_type type, const char *hashindex)
{
	if(EmptyString(hashindex))
		return NULL;
	return hash_find_len(type, hashindex, strlen(hashindex) + 1);
}

void *
hash_find_data_len(hash_type type, const void *hashindex, size_t size)
{
	hash_node *hnode;
	hnode = hash_find_len(type, hashindex, size);
	if(hnode == NULL)
		return NULL;
	return hnode->data;
}

void *
hash_find_data(hash_type type, const char *hashindex)
{
	if(EmptyString(hashindex))
		return NULL;
	return hash_find_data_len(type, hashindex, strlen(hashindex) + 1);
}

static rb_dlink_list *
hash_allocate_bucket(hash_type type, uint32_t hashv)
{
	if(hash_function[type].htable[hashv] != NULL)
		return hash_function[type].htable[hashv];
	hash_function[type].htable[hashv] = rb_malloc(sizeof(rb_dlink_list));
	return hash_function[type].htable[hashv];
}
static void
hash_free_bucket(hash_type type, uint32_t hashv)
{
	if(*hash_function[type].htable == NULL)
		return;
	if(rb_dlink_list_length(*hash_function[type].htable) > 0)
		return;
	rb_free(hash_function[type].htable);
	hash_function[type].htable = NULL;
}

hash_node *
hash_add_len(hash_type type, const void *hashindex, size_t indexlen, void *pointer)
{
	rb_dlink_list *bucket; // = hash_function[type].table;
	hash_node *hnode;
	uint32_t hashv;

	if(hashindex == NULL || pointer == NULL)
		return NULL;

	hashv = hfunc(type, hashindex, IRCD_MIN(indexlen, hash_function[type].hashlen));
	bucket = hash_allocate_bucket(type, hashv);
	hnode = rb_malloc(sizeof(hash_node));
	hnode->key = rb_malloc(indexlen);
	hnode->keylen = indexlen;
	memcpy(hnode->key, hashindex, indexlen);
	hnode->hashv = hashv;
	hnode->data = pointer;
	rb_dlinkAdd(hnode, &hnode->node, bucket);
	return hnode;
}

hash_node *
hash_add(hash_type type, const char *hashindex, void *pointer)
{
	if(EmptyString(hashindex))
		return NULL;
	return hash_add_len(type, hashindex, strlen(hashindex) + 1, pointer);
}

void
hash_del_len(hash_type type, const void *hashindex, size_t size, void *pointer)
{
	rb_dlink_list *bucket; // = hash_function[type].table;
	rb_dlink_node *ptr;
	uint32_t hashv;
	size_t hashlen;

	if(pointer == NULL || hashindex == NULL)
		return;

	if(hash_function[type].hashlen == 0)
		hashlen = size;
	else
		hashlen = IRCD_MIN(size, hash_function[type].hashlen);

	hashv = hfunc(type, hashindex, hashlen);
	bucket = hash_function[type].htable[hashv];

	if(bucket == NULL)
		return;
	
	RB_DLINK_FOREACH(ptr, bucket->head)
	{
		hash_node *hnode = ptr->data;
		if(hnode->data == pointer)
		{
			rb_dlinkDelete(&hnode->node, bucket);
			free_hashnode(hnode);
			hash_free_bucket(type, hashv);
			return;
		}
	}
}

void
hash_del(hash_type type, const char *hashindex, void *pointer)
{
	if(EmptyString(hashindex))
		return;
	hash_del_len(type, hashindex, strlen(hashindex) + 1, pointer);
}

void
hash_del_hnode(hash_type type, hash_node * hnode)
{
	rb_dlink_list *bucket; 
	uint32_t hashv;	
	if(hnode == NULL)
		return;

	hashv = hnode->hashv;
	bucket = hash_function[type].htable[hashv];

	if(bucket == NULL)
		return;

	rb_dlinkDelete(&hnode->node, bucket);
	free_hashnode(hnode);
	hash_free_bucket(type, hashv);
}

void
hash_destroyall(hash_type type, hash_destroy_cb * destroy_cb)
{
	for(int i = 0; i < (1 << hash_function[type].hashbits); i++)
	{
		rb_dlink_list *ltable;
		rb_dlink_node *ptr, *nptr;
		
		ltable = hash_function[type].htable[i];
		if(ltable == NULL)
			continue;
		RB_DLINK_FOREACH_SAFE(ptr, nptr, ltable->head)
		{
			hash_node *hnode = ptr->data;
			void *cbdata = hnode->data;
			
			rb_dlinkDelete(ptr, ltable);
			free_hashnode(hnode);
			destroy_cb(cbdata);
		}
		hash_free_bucket(type, i);
	}
}

void
hash_walkall(hash_type type, hash_walk_cb * walk_cb, void *walk_data)
{
	for(unsigned int i = 0; i < ( 1 << hash_function[type].hashbits); i++)
	{
		rb_dlink_list *ltable;
		rb_dlink_node *ptr, *next_ptr;
		
		ltable = hash_function[type].htable[i];
		if(ltable == NULL)
			continue;

		RB_DLINK_FOREACH_SAFE(ptr, next_ptr, ltable->head)
		{
			hash_node *hnode = ptr->data;
			void *cbdata = hnode->data;
			walk_cb(cbdata, walk_data);
		
		}
	}
}

rb_dlink_list
hash_get_channel_block(int i)
{
	return *channelTable[i];
}


rb_dlink_list *
hash_get_tablelist(int type)
{
	rb_dlink_list *alltables;

	alltables = rb_malloc(sizeof(rb_dlink_list));

	for(int i = 0; i < (1 << hash_function[type].hashbits); i++)
	{
		rb_dlink_list *table = hash_function[type].htable[i];
		
		if(table == NULL || rb_dlink_list_length(table) == 0)
			continue;
		rb_dlinkAddAlloc(table, alltables);
	}

	if(rb_dlink_list_length(alltables) == 0)
	{
		rb_free(alltables);
		alltables = NULL;
	}
	return alltables;
}

void
hash_free_tablelist(rb_dlink_list * table)
{
	rb_dlink_node *ptr, *next;
	RB_DLINK_FOREACH_SAFE(ptr, next, table->head)
	{
		rb_free(ptr);
	}
	rb_free(table);
}

static void
output_hash(struct Client *source_p, const char *name, unsigned long length, unsigned long *counts,
	    unsigned long deepest)
{
	unsigned long total = 0;

	sendto_one_numeric(source_p, RPL_STATSDEBUG, "B :%s Hash Statistics", name);

	sendto_one_numeric(source_p, RPL_STATSDEBUG, "B :Size: %lu Empty: %lu (%.3f%%)",
			   length, counts[0], (float) ((counts[0] * 100) / (float) length));

	for(unsigned long i = 1; i < 11; i++)
	{
		total += (counts[i] * i);
	}

	/* dont want to divide by 0! --fl */
	if(counts[0] != length)
	{
		sendto_one_numeric(source_p, RPL_STATSDEBUG,
				   "B :Average depth: %.3f%%/%.3f%% Highest depth: %lu",
				   (float) (total / (length - counts[0])), (float) (total / length), deepest);
	}

	for(unsigned long i = 1; i < IRCD_MIN(11, deepest + 1); i++)
	{
		sendto_one_numeric(source_p, RPL_STATSDEBUG, "B :Nodes with %lu entries: %lu", i, counts[i]);
	}
}

static void
count_hash(struct Client *source_p, rb_dlink_list ** table, unsigned int length, const char *name)
{
	unsigned long counts[11];
	unsigned long deepest = 0;
	unsigned long i;

	memset(counts, 0, sizeof(counts));

	for(i = 0; i < length; i++)
	{
		if(table[i] == NULL)
			continue;
	
		if(rb_dlink_list_length(table[i]) >= 10)
			counts[10]++;
		else
			counts[rb_dlink_list_length(table[i])]++;

		if(rb_dlink_list_length(table[i]) > deepest)
			deepest = rb_dlink_list_length(table[i]);
	}

	output_hash(source_p, name, length, counts, deepest);
}

void
hash_stats(struct Client *source_p)
{
	for(unsigned int i = 0; i < HASH_LAST; i++)
	{
		count_hash(source_p, hash_function[i].htable, 1 << hash_function[i].hashbits, hash_function[i].name);
		sendto_one_numeric(source_p, RPL_STATSDEBUG, "B :--");
	}
}

void
hash_get_memusage(hash_type type, size_t * entries, size_t * memusage)
{
	rb_dlink_list **htable;
	rb_dlink_node *ptr;
	hash_node *hnode;
	size_t mem = 0, cnt = 0;
	unsigned int max, i;
	max = 1 << hash_function[type].hashbits;

	htable = hash_function[type].htable;
	for(i = 0; i < max; i++)
	{
		if(htable[i] == NULL)
			continue;

		mem += sizeof(rb_dlink_list);
		RB_DLINK_FOREACH(ptr, htable[i]->head)
		{
			hnode = ptr->data;
			fprintf(stderr, "memusage adding %zu %zu\n", hnode->keylen, sizeof(hash_node));
			mem += hnode->keylen + sizeof(hash_node);
			cnt++;
		} 
	}
	if(memusage != NULL)
		*memusage = mem;
	if(entries != NULL)
		*entries = cnt;
}
