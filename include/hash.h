/*
 *  ircd-ratbox: A slightly useful ircd.
 *  hash.h: A header for the ircd hashtable code.
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

#ifndef INCLUDED_hash_h
#define INCLUDED_hash_h


struct _hash_node;
typedef struct _hash_node hash_node;

#include <cache.h>
#include <s_newconf.h>

#define HASH_WALK(i, max, ptr, table) for (i = 0; i < max; i++) { RB_DLINK_FOREACH(ptr, table[i].head)
#define HASH_WALK_SAFE(i, max, ptr, nptr, table) for (i = 0; i < max; i++) { RB_DLINK_FOREACH_SAFE(ptr, nptr, table[i].head)
#define HASH_WALK_END }



typedef enum
{
	HASH_CLIENT,
	HASH_ID,
	HASH_CHANNEL,
	HASH_HOSTNAME,
	HASH_RESV,
	HASH_OPER,
	HASH_SCACHE,
	HASH_HELP,
	HASH_OHELP,
	HASH_ND,
	HASH_CONNID,
	HASH_ZCONNID,
	HASH_WHOWAS,
	HASH_MONITOR,
	HASH_COMMAND,
	HASH_LAST
} hash_type;

struct _hash_node
{
	rb_dlink_node node;
	void *key;
	size_t keylen;
	void *data;
	uint32_t hashv;
};

typedef void hash_destroy_cb(void *data);
typedef void hash_walk_cb(void *a, void *);


void init_hash(void);

hash_node *hash_add(hash_type, const char *, void *);
void hash_del(hash_type, const char *, void *);
hash_node *hash_add_len(hash_type type, const void *hashindex, size_t indexlen, void *pointer);
void hash_del_len(hash_type type, const void *hashindex, size_t indexlen, void *pointer);

void hash_walkall(hash_type type, hash_walk_cb *walk_cb, void *walk_data);


rb_dlink_list *hash_find_list_len(hash_type type, const void *hashindex, size_t size);
rb_dlink_list *hash_find_list(hash_type type, const char *hashindex);
void hash_free_list(rb_dlink_list *list);

hash_node *hash_find(hash_type, const char *hashindex);
hash_node *hash_find_len(hash_type, const void *hashindex, size_t len);


void *hash_find_data(hash_type type, const char *hashindex);
void *hash_find_data_len(hash_type type, const void *hashindex, size_t len);

void hash_del_hnode(hash_type type, hash_node *node);

void hash_stats(struct Client *);
void hash_get_memusage(hash_type type, size_t *memusage, size_t *entries);

rb_dlink_list hash_get_channel_block(int i);

rb_dlink_list *hash_get_tablelist(int type);  
void hash_free_tablelist(rb_dlink_list *tables);
void hash_destroyall(hash_type type, hash_destroy_cb *destroy_cb);


#endif /* INCLUDED_hash_h */
