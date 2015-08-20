/*
 *  ircd-ratbox: A slightly useful ircd.
 *  whowas.c: WHOWAS user cache.
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

#include "stdinc.h"
#include "struct.h"
#include "whowas.h"
#include "hash.h"
#include "match.h"
#include "ircd.h"
#include "numeric.h"
#include "s_serv.h"
#include "s_user.h"
#include "send.h"
#include "s_conf.h"
#include "client.h"
#include "send.h"
#include "s_log.h"

static rb_dlink_list *whowas_list;
static rb_dlink_list *whowas_hash;
static unsigned int whowas_list_length = NICKNAMEHISTORYLENGTH;

/*
 * Whowas hash table size
 *
 */
#define WW_MAX_BITS 16
#define WW_MAX (1<<WW_MAX_BITS)
#define hash_whowas_name(x) fnv_hash_upper((const unsigned char *)x, WW_MAX_BITS, 0)


void
whowas_get_list(const char *nick, rb_dlink_list *list)
{
	uint32_t hashv;
	rb_dlink_node *ptr;

	if(list == NULL)
		return;
			
	hashv = hash_whowas_name(nick);

	RB_DLINK_FOREACH(ptr, whowas_hash[hashv].head)
	{
		whowas_t *who = ptr->data;
		if(!irccmp(nick, who->name))
		{
			rb_dlinkAddTailAlloc(who, list);		
		}
	}
	return;
}

void
whowas_free_list(rb_dlink_list *list)
{
	rb_dlink_node *ptr, *next;
	if(list == NULL)
		return;
		
	RB_DLINK_FOREACH_SAFE(ptr, next, list->head)
	{
		rb_dlinkDestroy(ptr, list);
	}
	return;
}


void
whowas_add_history(struct Client *client_p, int online)
{
        whowas_t *who;
	s_assert(NULL != client_p);

	if(client_p == NULL)
		return;
                
        if(rb_dlink_list_length(whowas_list) >= whowas_list_length)
        {
                if(whowas_list->tail != NULL && whowas_list->tail->data != NULL)
                {
                        whowas_t *twho = whowas_list->tail->data;
                        if(twho->online != NULL)
                                rb_dlinkDelete(&twho->cnode, &twho->online->whowas_clist);
                        rb_dlinkDelete(&twho->node, &whowas_hash[twho->hashv]);
                        rb_dlinkDelete(&twho->whowas_node, whowas_list);
                        rb_free(twho);
                }
        }

        who = rb_malloc(sizeof(whowas_t));
	who->hashv = hash_whowas_name(client_p->name);
	who->logoff = rb_current_time();

	rb_strlcpy(who->name, client_p->name, sizeof(who->name));
	rb_strlcpy(who->username, client_p->username, sizeof(who->username));
	rb_strlcpy(who->hostname, client_p->host, sizeof(who->hostname));
	rb_strlcpy(who->realname, client_p->info, sizeof(who->realname));

	if(MyClient(client_p))
	{
		rb_strlcpy(who->sockhost, client_p->sockhost, sizeof(who->sockhost));
		who->spoof = IsIPSpoof(client_p);

	}
	else
	{
		who->spoof = false;
		if(EmptyString(client_p->sockhost) || !strcmp(client_p->sockhost, "0"))
			who->sockhost[0] = '\0';
		else
			rb_strlcpy(who->sockhost, client_p->sockhost, sizeof(who->sockhost));
	}

	/* this is safe do to the servername cache */
	who->servername = client_p->servptr->name;

	if(online)
	{
		who->online = client_p;
		rb_dlinkAdd(who, &who->cnode, &client_p->whowas_clist); 
	}
	else
		who->online = NULL;

	rb_dlinkAdd(who, &who->node, &whowas_hash[who->hashv]);
	rb_dlinkAdd(who, &who->whowas_node, whowas_list);
}

void
whowas_off_history(struct Client *client_p)
{
	rb_dlink_node *ptr, *next;

	RB_DLINK_FOREACH_SAFE(ptr, next, client_p->whowas_clist.head)
	{
		whowas_t *who = ptr->data;
		who->online = NULL;
		rb_dlinkDelete(&who->cnode, &client_p->whowas_clist);
	}
}

struct Client *
whowas_get_history(const char *nick, time_t timelimit)
{
	uint32_t hashv;
	rb_dlink_node *ptr;

	timelimit = rb_current_time() - timelimit;
	hashv = hash_whowas_name(nick);

 	RB_DLINK_FOREACH(ptr, whowas_hash[hashv].head)
	{
		whowas_t *who = ptr->data;
		if(irccmp(nick, who->name))
			continue;
		if(who->logoff < timelimit)
			continue;
		return who->online;
	
	}
	return NULL;
}

void
whowas_init()
{
        whowas_list = rb_malloc(sizeof(rb_dlink_list));
	whowas_hash = rb_malloc(sizeof(rb_dlink_list) * WW_MAX);

	if(whowas_list_length == 0)
	{
	        whowas_list_length = NICKNAMEHISTORYLENGTH;
	}
}

void 
whowas_set_size(int len)
{
        int i;

        if((whowas_list_length > len) && (rb_dlink_list_length(whowas_list) > len))
        {
                for(i = rb_dlink_list_length(whowas_list) - len; i > 0; --i)
                {
                        if(whowas_list->tail != NULL && whowas_list->tail->data != NULL)
                        {
                                whowas_t *who = whowas_list->tail->data;
                                if(who->online != NULL)
                                        rb_dlinkDelete(&who->cnode, &who->online->whowas_clist);
                                rb_dlinkDelete(&who->node, &whowas_hash[who->hashv]);
                                rb_dlinkDelete(&who->whowas_node, whowas_list);
                                rb_free(who);
                        }
                }
                
        }
        whowas_list_length = len;

}

void
whowas_memory_usage(size_t *count, size_t *memused)
{
	*count = rb_dlink_list_length(whowas_list);
	*memused = *count * sizeof(whowas_t);
}

