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


#define hash_nick(x) (fnv_hash_upper((const unsigned char *)(x), U_MAX_BITS, 0))
#define hash_id(x) (fnv_hash((const unsigned char *)(x), U_MAX_BITS, 0))
#define hash_channel(x) (fnv_hash_upper_len((const unsigned char *)(x), CH_MAX_BITS, 30))
#define hash_hostname(x) (fnv_hash_upper_len((const unsigned char *)(x), HOST_MAX_BITS, 30))
#define hash_resv(x) (fnv_hash_upper_len((const unsigned char *)(x), R_MAX_BITS, 30))
#define hash_cli_connid(x)	(x % CLI_CONNID_MAX)
#define hash_zconnid(x)	(x % ZCONNID_MAX)
// #define hash_opername(x) fnv_hash_upper((const unsigned char *)(x), OPERHASH_MAX_BITS, 0)


static rb_dlink_list clientbyconnidTable[CLI_CONNID_MAX];
static rb_dlink_list clientbyzconnidTable[CLI_ZCONNID_MAX];	
static rb_dlink_list clientTable[U_MAX];
static rb_dlink_list channelTable[CH_MAX];
static rb_dlink_list idTable[U_MAX];
static rb_dlink_list resvTable[R_MAX];
static rb_dlink_list hostTable[HOST_MAX];
static rb_dlink_list helpTable[HELP_MAX];
static rb_dlink_list ohelpTable[HELP_MAX];
static rb_dlink_list ndTable[U_MAX];
static rb_dlink_list operhash_table[OPERHASH_MAX];
static rb_dlink_list scache_table[SCACHE_MAX];
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
d *		     |	A  |	|  C  |	   |  D	 |
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
fnv_hash_len_data(const unsigned char *s, unsigned int bits, unsigned int len)
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




uint32_t
fnv_hash_upper(const unsigned char *s, unsigned int bits, unsigned int unused)
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

uint32_t
fnv_hash(const unsigned char *s, unsigned int bits, unsigned int unused)
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

uint32_t
fnv_hash_len(const unsigned char *s, unsigned int bits, unsigned int len)
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

uint32_t
fnv_hash_upper_len(const unsigned char *s, unsigned int bits, unsigned int len)
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
free_hashnode(hash_node *hnode)
{
	rb_free(hnode->key);
	rb_free(hnode);
}


static
bool hash_irccmp(const void *x, const void *y, size_t unusued)
{
	/* the size parameter is unused as it is assumed if you are using
	 * hash_irccmp, you are using C strings... */
	return (irccmp(x, y) != 0) ? false : true;
}

static 
bool hash_strcmp(const void *x, const void *y, size_t unusued)
{
	return (strcmp(x, y) != 0) ? false : true;
}

static
bool connid_cmp(const void *x, const void *y, size_t cmpsize)
{
	return (memcmp(x, y, cmpsize) != 0) ? false : true;
}


typedef bool hash_cmp(const void *x, const void *y, size_t len);


struct _hash_function
{
	const char *name;
	uint32_t(*func) (unsigned const char *, unsigned int, unsigned int);
	hash_cmp *cmpfunc;
	rb_dlink_list *table;
	unsigned int hashbits;
	unsigned int hashlen;
};


/* if the cmpfunc field is not set, it is assumed hash_irccmp will be used */

static struct _hash_function hash_function[HASH_LAST] =
{
	[HASH_CLIENT] = { 
		.name = "NICK", .func = fnv_hash_upper, .table = clientTable, .hashbits = U_MAX_BITS, 
	},
	[HASH_ID] = { 
		.name = "ID", .func = fnv_hash, .table = idTable, .hashbits = U_MAX_BITS, .cmpfunc = hash_strcmp,
	},
	[HASH_CHANNEL] = { 
		.name = "Channel", .func = fnv_hash_upper_len, .table = channelTable, .hashbits = CH_MAX_BITS, .hashlen = 30,
	},
	[HASH_HOSTNAME] = { 
		.name = "Host", .func = fnv_hash_upper_len, .table = hostTable, .hashbits = HOST_MAX_BITS, .hashlen = 30
	},
	[HASH_RESV] = { 
		.name = "Channel RESV", .func = fnv_hash_upper_len, .table = resvTable, .hashbits = R_MAX_BITS, .hashlen = 30
	},
	[HASH_OPER] = {
		.name = "Operator", .func = fnv_hash_upper, .table = operhash_table, .hashbits = OPERHASH_MAX_BITS, .cmpfunc = hash_irccmp,
	},
	[HASH_SCACHE] = {
		.name = "Server Cache", .func = fnv_hash_upper, .table = scache_table, .hashbits = OPERHASH_MAX_BITS, .cmpfunc = hash_irccmp,
	},
	[HASH_HELP] = {
		.name = "Help", .func = fnv_hash_upper, .table = helpTable, .hashbits = HELP_MAX_BITS, .cmpfunc = hash_irccmp
	},
	[HASH_OHELP] = {
		.name = "Operator Help", .func = fnv_hash_upper, .table = ohelpTable, .hashbits = HELP_MAX_BITS, .cmpfunc = hash_irccmp
	},
	[HASH_ND] = {
		.name = "Nick Delay", .func = fnv_hash_upper, .table = ndTable, .hashbits = U_MAX_BITS, .cmpfunc = hash_irccmp
	},
	[HASH_CONNID] = {
		.name = "Connection ID", .func = fnv_hash_len_data, .table = clientbyconnidTable, .hashbits = CLI_CONNID_MAX_BITS, .cmpfunc = connid_cmp, .hashlen = sizeof(uint32_t)
	},
	[HASH_ZCONNID] = {
		.name = "Ziplinks ID", .func = fnv_hash_len_data, .table = clientbyzconnidTable, .hashbits = CLI_ZCONNID_MAX_BITS, .cmpfunc = connid_cmp, .hashlen = sizeof(uint32_t)
	}
};



hash_node *
hash_find_len(hash_type type, const void *hashindex, size_t size)
{
	rb_dlink_list *table = hash_function[type].table;
	hash_cmp *hcmpfunc;
	size_t hashlen;
	uint32_t hashv;
	rb_dlink_node *ptr;
	
	hcmpfunc = hash_function[type].cmpfunc;
	
	if(hashindex == NULL)
		return NULL;

	if(hcmpfunc == NULL)
		hcmpfunc = hash_irccmp;

	if(hash_function[type].hashlen == 0)
		hashlen = size;
	else
		hashlen = IRCD_MIN(size, hash_function[type].hashlen);


	/* we return the hashv regardless of true/false */	
	hashv = hash_function[type].func((unsigned const char *)hashindex, hash_function[type].hashbits, hashlen);

	/* this code assumes that the compare function will not try to mess with
	 * the link list behind our backs.... */
	RB_DLINK_FOREACH(ptr, table[hashv].head)
	{
		hash_node *hnode = ptr->data;
		
		if(hcmpfunc(hashindex, hnode->key, hashlen) == true)
		{
			return hnode;
		}
	}	
	return NULL;
}

hash_node *
hash_find(hash_type type, const char *hashindex)
{
	return hash_find_len(type, hashindex, strlen(hashindex));
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
	return hash_find_data_len(type, hashindex, strlen(hashindex)+1);
}


void
hash_add_len(hash_type type, const void *hashindex, size_t indexlen, void *pointer)
{
	rb_dlink_list *table = hash_function[type].table;
	hash_node *hnode;
	uint32_t hashv;

	if(hashindex == NULL || pointer == NULL)
		return;

	hashv = (hash_function[type].func) ((const unsigned char *)hashindex,
					    hash_function[type].hashbits, IRCD_MIN(indexlen, hash_function[type].hashlen));

	hnode = rb_malloc(sizeof(hash_node));
	hnode->key = rb_malloc(indexlen);
	hnode->keylen = indexlen;
	memcpy(hnode->key, hashindex, indexlen);
	hnode->hashv = hashv;	 
	hnode->data = pointer;
	rb_dlinkAdd(hnode, &hnode->node, &table[hashv]);
}

void
hash_add(hash_type type, const char *hashindex, void *pointer)
{
	hash_add_len(type, hashindex, strlen(hashindex)+1, pointer);
}


void
hash_del_len(hash_type type, const void *hashindex, size_t size, void *pointer)
{
	rb_dlink_list *table = hash_function[type].table;
	rb_dlink_node *ptr;
	uint32_t hashv;
	size_t hashlen;

	if(pointer == NULL || hashindex == NULL) 
		return;

	if(hash_function[type].hashlen == 0)
		hashlen = size;
	else
		hashlen = IRCD_MIN(size, hash_function[type].hashlen);

	hashv = (hash_function[type].func) ((const unsigned char *)hashindex,
					    hash_function[type].hashbits, hashlen);
	RB_DLINK_FOREACH(ptr, table[hashv].head)
	{
		hash_node *hnode = ptr->data;
		if(hnode->data == pointer)
		{
			rb_dlinkDelete(&hnode->node, &table[hashv]);
			free_hashnode(hnode);
			return;		
		}
	}					    
	

}


void
hash_del(hash_type type, const char *hashindex, void *pointer)
{
	hash_del_len(type, hashindex, strlen(hashindex) + 1, pointer);
}

void
hash_del_hnode(hash_type type, hash_node *hnode)
{
	rb_dlink_list *table = hash_function[type].table;

	if(hnode == NULL)
		return;

	rb_dlinkDelete(&hnode->node, &table[hnode->hashv]);
	free_hashnode(hnode);
}


void
hash_destroyall(hash_type type, hash_destroy_cb *destroy_cb)
{
	rb_dlink_node *ptr;
	rb_dlink_node *next_ptr;
	rb_dlink_list *table;
	unsigned int i, max;
	
	max = 1<<hash_function[type].hashbits;
	table = hash_function[type].table;
	HASH_WALK_SAFE(i, max, ptr, next_ptr, table)
	{
		hash_node *hnode = ptr->data;
		void *cbdata = hnode->data;
				
		rb_dlinkDelete(ptr, table);
		free_hashnode(hnode);
		destroy_cb(cbdata);
	}
	HASH_WALK_END;


}

void
hash_walkall(hash_type type, hash_walk_cb *walk_cb, void *walk_data)
{
	rb_dlink_node *ptr;
	rb_dlink_node *next_ptr;
	rb_dlink_list *table;
	unsigned int i, max;
	
	max = 1<<hash_function[type].hashbits;
	table = hash_function[type].table;
	HASH_WALK_SAFE(i, max, ptr, next_ptr, table)
	{
		hash_node *hnode = ptr->data;
		void *cbdata = hnode->data;
		walk_cb(cbdata, walk_data);
	}
	HASH_WALK_END;
}



struct Client *
find_id(const char *name)
{
	return hash_find_data(HASH_ID, name);
}

/* hash_find_masked_server()
 * 
 * Whats happening in this next loop ? Well, it takes a name like
 * foo.bar.edu and proceeds to earch for *.edu and then *.bar.edu.
 * This is for checking full server names against masks although
 * it isnt often done this way in lieu of using matches().
 *
 * Rewrote to do *.bar.edu first, which is the most likely case,
 * also made const correct
 * --Bleep
 */
static struct Client *
hash_find_masked_server(struct Client *source_p, const char *name)
{
	char buf[HOSTLEN + 1];
	char *p = buf;
	char *s;
	struct Client *server;

	if('*' == *name || '.' == *name)
		return NULL;

	/* copy it across to give us a buffer to work on */
	rb_strlcpy(buf, name, sizeof(buf));

	while((s = strchr(p, '.')) != 0)
	{
		*--s = '*';
		/*
		 * Dont need to check IsServer() here since nicknames cant
		 * have *'s in them anyway.
		 */
		if((server = find_server(source_p, s)))
			return server;
		p = s + 2;
	}

	return NULL;
}

/* find_any_client()
 *
 * finds a client/server/masked server entry from the hash
 */
struct Client *
find_any_client(const char *name)
{
	rb_dlink_node *ptr;
	uint32_t hashv;

	s_assert(name != NULL);
	if(EmptyString(name))
		return NULL;

	/* hunting for an id, not a nick */
	if(IsDigit(*name))
		return hash_find_data(HASH_ID, name);

	hashv = hash_nick(name);

	RB_DLINK_FOREACH(ptr, clientTable[hashv].head)
	{
		struct Client *target_p = ptr->data;

		if(irccmp(name, target_p->name) == 0)
			return target_p;
	}

	/* wasnt found, look for a masked server */
	return hash_find_masked_server(NULL, name);
}

/* find_client()
 *
 * finds a client/server entry from the client hash table
 */
struct Client *
find_client(const char *name)
{
	s_assert(name != NULL);
	if(EmptyString(name))
		return NULL;

	/* hunting for an id, not a nick */
	if(IsDigit(*name))
		return hash_find_data(HASH_ID, name);

	return hash_find_data(HASH_CLIENT, name);
}

/* find_named_client()
 *
 * finds a client/server entry from the client hash table
 */
struct Client *
find_named_client(const char *name)
{
	s_assert(name != NULL);
	if(EmptyString(name))
		return NULL;

	return hash_find_data(HASH_CLIENT, name);
}

/* find_server()
 *
 * finds a server from the client hash table
 */
struct Client *
find_server(struct Client *source_p, const char *name)
{
	struct Client *target_p;

	if(EmptyString(name))
		return NULL;

	if((source_p == NULL || !MyClient(source_p)) && IsDigit(*name) && strlen(name) == 3)
	{
	        return hash_find_data(HASH_ID, name);
	}

	target_p = hash_find_data(HASH_CLIENT, name);

	if(target_p != NULL && (IsServer(target_p) || IsMe(target_p)))
		return target_p;

	/* wasnt found, look for a masked server */
	return hash_find_masked_server(source_p, name);
}

/* find_hostname()
 *
 * finds a hostname dlink list from the hostname hash table.
 * we return the full dlink list, because you can have multiple
 * entries with the same hostname
 */
rb_dlink_list *
find_hostname(const char *hostname)
{
	uint32_t hashv;

	if(EmptyString(hostname))
		return NULL;

	hashv = hash_hostname(hostname);

	return &hostTable[hashv];
}

/* find_channel()
 *
 * finds a channel from the channel hash table
 */
struct Channel *
find_channel(const char *name)
{
	s_assert(name != NULL);
	if(EmptyString(name))
		return NULL;

	return hash_find_data(HASH_CHANNEL, name);
}

/*
 * get_or_create_channel
 * inputs	- client pointer
 *		- channel name
 *		- pointer to int flag whether channel was newly created or not
 * output	- returns channel block or NULL if illegal name
 *		- also modifies *isnew
 *
 *  Get Channel block for chname (and allocate a new channel
 *  block, if it didn't exist before).
 */
struct Channel *
get_or_create_channel(struct Client *client_p, const char *chname, int *isnew)
{
	struct Channel *chptr;
	size_t len;
	const char *s = chname;

	if(EmptyString(s))
		return NULL;

	if((len = strlen(s)) > CHANNELLEN)
	{
		if(IsServer(client_p))
		{
			sendto_realops_flags(UMODE_DEBUG, L_ALL,
					     "*** Long channel name from %s (%zd > %d): %s",
					     client_p->name, len, CHANNELLEN, s);
		}
		s = LOCAL_COPY_N(s, CHANNELLEN);
	}


	if((chptr = find_channel(s)) != NULL)
	{
		if(isnew != NULL)
			*isnew = 0;
		return chptr;
	}

	if(isnew != NULL)
		*isnew = 1;

	chptr = allocate_channel(s);

	rb_dlinkAdd(chptr, &chptr->node, &global_channel_list);

	chptr->channelts = rb_current_time();	/* doesn't hurt to set it here */

	hash_add(HASH_CHANNEL, chptr->chname, chptr);
	return chptr;
}

rb_dlink_list 
hash_get_channel_block(int i)
{
        return channelTable[i];
}
        

rb_dlink_list *
hash_get_tablelist(int type)
{
        rb_dlink_list *table = hash_function[type].table;
        rb_dlink_list *alltables;

	alltables = rb_malloc(sizeof(rb_dlink_list));

        for(int i = 0; i < 1<<hash_function[type].hashbits; i++)
        {
                if(rb_dlink_list_length(&table[i]) == 0)
                        continue;
                rb_dlinkAddAlloc(&table[i], alltables);
        }

        if(rb_dlink_list_length(alltables) == 0)
        {
                rb_free(alltables);
                alltables = NULL;
        }
        return alltables;
}

void 
hash_free_tablelist(rb_dlink_list *table)
{
        rb_dlink_node *ptr, *next;
        RB_DLINK_FOREACH_SAFE(ptr, next, table->head)
        {
                rb_free_rb_dlink_node(ptr);
        }
        rb_free(table);        
}	


/* hash_find_resv()
 *
 * hunts for a resv entry in the resv hash table
 */
struct ConfItem *
hash_find_resv(const char *name)
{
	struct ConfItem *aconf;

	aconf = hash_find_data(HASH_RESV, name);
	if(aconf != NULL)
		aconf->port++;

	return aconf;
}

void
clear_resv_hash(void)
{
	rb_dlink_node *ptr;
	rb_dlink_node *next_ptr;
	int i;

	HASH_WALK_SAFE(i, R_MAX, ptr, next_ptr, resvTable)
	{
		hash_node *hnode = ptr->data;
		struct ConfItem *aconf = hnode->data;

		/* skip temp resvs */
		if(aconf->flags & CONF_FLAGS_TEMPORARY)
			continue;

		del_channel_hash_resv_hnode(hnode);
	}
	HASH_WALK_END;
}

void
add_channel_hash_resv(struct ConfItem *aconf)
{
	rb_dlink_list *list;
	hash_add(HASH_RESV, aconf->host, aconf);
	if(aconf->flags & CONF_FLAGS_TEMPORARY)
		list = &resv_channel_temp_list;
	else
		list = &resv_channel_perm_list;
	rb_dlinkAddAlloc(aconf, list);
}

void
del_channel_hash_resv(struct ConfItem *aconf)
{
	hash_node *hnode;
	
	if((hnode = hash_find(HASH_RESV, aconf->host)) == NULL)
		return;
	del_channel_hash_resv_hnode(hnode);
}

void
del_channel_hash_resv_hnode(hash_node *hnode)
{
	rb_dlink_list *list;
	struct ConfItem *aconf;
	
	if(hnode == NULL)
		return;
		
	aconf = hnode->data;
	if(aconf->flags & CONF_FLAGS_TEMPORARY)
		list = &resv_channel_temp_list;
	else
		list = &resv_channel_perm_list;

	rb_dlinkFindDestroy(aconf, list);
	hash_del_hnode(HASH_RESV, hnode);
}



void
add_to_zconnid_hash(struct Client *client_p)
{
	hash_add_len(HASH_ZCONNID, &client_p->localClient->zconnid, sizeof(client_p->localClient->zconnid), client_p);
}

void
del_from_zconnid_hash(struct Client *client_p)
{
	hash_del_len(HASH_ZCONNID, &client_p->localClient->zconnid, sizeof(client_p->localClient->zconnid), client_p);
}

void
add_to_cli_connid_hash(struct Client *client_p)
{
	hash_add_len(HASH_CONNID, &client_p->localClient->connid, sizeof(client_p->localClient->connid), client_p);
}


void
del_from_cli_connid_hash(struct Client *client_p)
{
	hash_del_len(HASH_CONNID, &client_p->localClient->connid, sizeof(client_p->localClient->connid), client_p);
}

struct Client *
find_cli_connid_hash(uint32_t connid)
{
	struct Client *target_p;
	
	

	target_p = hash_find_data_len(HASH_CONNID, &connid, sizeof(connid));
	if(target_p != NULL)
		return target_p;
	
	target_p = hash_find_data_len(HASH_ZCONNID, &connid, sizeof(connid));
	
	return target_p;
}



static void
output_hash(struct Client *source_p, const char *name, int length, int *counts, unsigned long deepest)
{
	unsigned long total = 0;
	int i;

	sendto_one_numeric(source_p, RPL_STATSDEBUG, "B :%s Hash Statistics", name);

	sendto_one_numeric(source_p, RPL_STATSDEBUG, "B :Size: %d Empty: %d (%.3f%%)",
			   length, counts[0], (float)((counts[0] * 100) / (float)length));

	for(i = 1; i < 11; i++)
	{
		total += (counts[i] * i);
	}

	/* dont want to divide by 0! --fl */
	if(counts[0] != length)
	{
		sendto_one_numeric(source_p, RPL_STATSDEBUG,
				   "B :Average depth: %.3f%%/%.3f%% Highest depth: %lu",
				   (float)(total / (length - counts[0])), (float)(total / length), deepest);
	}
	
	for(i = 1; i < IRCD_MIN(11, deepest+1); i++)
	{
		sendto_one_numeric(source_p, RPL_STATSDEBUG, "B :Nodes with %d entries: %d", i, counts[i]);
	}
}


static void
count_hash(struct Client *source_p, rb_dlink_list * table, int length, const char *name)
{
	int counts[11];
	unsigned long deepest = 0;
	int i;

	memset(counts, 0, sizeof(counts));

	for(i = 0; i < length; i++)
	{
		if(rb_dlink_list_length(&table[i]) >= 10)
			counts[10]++;
		else
			counts[rb_dlink_list_length(&table[i])]++;

		if(rb_dlink_list_length(&table[i]) > deepest)
			deepest = rb_dlink_list_length(&table[i]);
	}

	output_hash(source_p, name, length, counts, deepest);
}

void
hash_stats(struct Client *source_p)
{
	for(int i = 0; i < HASH_LAST; i++)
	{
		count_hash(source_p, hash_function[i].table, 1<<hash_function[i].hashbits, hash_function[i].name);
		sendto_one_numeric(source_p, RPL_STATSDEBUG, "B :--");			
	}
}
