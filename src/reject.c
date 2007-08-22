/*
 *  ircd-ratbox: A slightly useful ircd
 *  reject.c: reject users with prejudice
 *
 *  Copyright (C) 2003 Aaron Sethman <androsyn@ratbox.org>
 *  Copyright (C) 2003-2005 ircd-ratbox development team
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
#include "patricia.h"
#include "client.h"
#include "s_conf.h"
#include "reject.h"
#include "s_stats.h"
#include "ircd.h"
#include "send.h"
#include "numeric.h"
#include "parse.h"
#include "hostmask.h"

static patricia_tree_t *reject_tree;
patricia_tree_t *dline_tree;
static patricia_tree_t *eline_tree;
dlink_list delay_exit;
static dlink_list reject_list;
static dlink_list throttle_list;
static patricia_tree_t *throttle_tree;
static void throttle_expires(void *unused);


struct reject_data
{
	dlink_node rnode;
	time_t time;
	unsigned int count;
};

struct delay_data
{
	dlink_node node;
	rb_fde_t *F;
};


static patricia_node_t *
add_ipline(struct ConfItem *aconf, patricia_tree_t *tree, struct sockaddr *addr, int cidr)
{
	patricia_node_t *pnode;
	pnode = make_and_lookup_ip(tree, addr, cidr);
	if(pnode == NULL)
		return NULL;
	aconf->pnode = pnode;
	pnode->data = aconf;
	return (pnode);
}

int 
add_dline(struct ConfItem *aconf)
{
	struct irc_sockaddr_storage st;
	int bitlen;
	if(parse_netmask(aconf->host, (struct sockaddr *)&st, &bitlen) == HM_HOST)
		return 0;

	if(add_ipline(aconf, dline_tree, (struct sockaddr *)&st, bitlen) != NULL)
		return 1;
	return 0;
}

int
add_eline(struct ConfItem *aconf)
{
	struct irc_sockaddr_storage st;
	int bitlen;
	if(parse_netmask(aconf->host, (struct sockaddr *)&st, &bitlen) == HM_HOST)
		return 0;

	if(add_ipline(aconf, eline_tree, (struct sockaddr *)&st, bitlen) != NULL)
		return 1;
	return 0;
}


static void
reject_exit(void *unused)
{
	dlink_node *ptr, *ptr_next;
	struct delay_data *ddata;
	static const char *errbuf = "ERROR :Closing Link: (*** Banned (cache))\r\n";
	
	DLINK_FOREACH_SAFE(ptr, ptr_next, delay_exit.head)
	{
		ddata = ptr->data;

		rb_write(ddata->F, errbuf, strlen(errbuf));		
		rb_close(ddata->F);
		rb_free(ddata);
	}

	delay_exit.head = delay_exit.tail = NULL;
	delay_exit.length = 0;
}

static void
reject_expires(void *unused)
{
	dlink_node *ptr, *next;
	patricia_node_t *pnode;
	struct reject_data *rdata;
	
	DLINK_FOREACH_SAFE(ptr, next, reject_list.head)
	{
		pnode = ptr->data;
		rdata = pnode->data;		

		if(rdata->time + ConfigFileEntry.reject_duration > rb_current_time())
			continue;

		rb_dlinkDelete(ptr, &reject_list);
		rb_free(rdata);
		patricia_remove(reject_tree, pnode);
	}
}

void
init_reject(void)
{
	reject_tree = New_Patricia(PATRICIA_BITS);
	dline_tree = New_Patricia(PATRICIA_BITS);
	eline_tree = New_Patricia(PATRICIA_BITS);
	throttle_tree = New_Patricia(PATRICIA_BITS);
	rb_event_add("reject_exit", reject_exit, NULL, DELAYED_EXIT_TIME);
	rb_event_add("reject_expires", reject_expires, NULL, 60);
	rb_event_add("throttle_expires", throttle_expires, NULL, 10);
}


void
add_reject(struct Client *client_p)
{
	patricia_node_t *pnode;
	struct reject_data *rdata;

	/* Reject is disabled */
	if(ConfigFileEntry.reject_after_count == 0 || ConfigFileEntry.reject_duration == 0)
		return;

	if((pnode = match_ip(reject_tree, (struct sockaddr *)&client_p->localClient->ip)) != NULL)
	{
		rdata = pnode->data;
		rdata->time = rb_current_time();
		rdata->count++;
	}
	else
	{
		int bitlen = 32;
#ifdef IPV6
		if(GET_SS_FAMILY(&client_p->localClient->ip) == AF_INET6)
			bitlen = 128;
#endif
		pnode = make_and_lookup_ip(reject_tree, (struct sockaddr *)&client_p->localClient->ip, bitlen);
		pnode->data = rdata = rb_malloc(sizeof(struct reject_data));
		rb_dlinkAddTail(pnode, &rdata->rnode, &reject_list);
		rdata->time = rb_current_time();
		rdata->count = 1;
	}
}

int
check_reject(rb_fde_t *F, struct sockaddr *addr)
{
	patricia_node_t *pnode;
	struct reject_data *rdata;
	struct delay_data *ddata;
	/* Reject is disabled */
	if(ConfigFileEntry.reject_after_count == 0 || ConfigFileEntry.reject_duration == 0)
		return 0;
		
	pnode = match_ip(reject_tree, addr);
	if(pnode != NULL)
	{
		rdata = pnode->data;

		rdata->time = rb_current_time();
		if(rdata->count > (unsigned long)ConfigFileEntry.reject_after_count)
		{
			ddata = rb_malloc(sizeof(struct delay_data));
			ServerStats.is_rej++;
			rb_setselect(F, IRCD_SELECT_WRITE | IRCD_SELECT_READ, NULL, NULL);
			ddata->F = F;
			rb_dlinkAdd(ddata, &ddata->node, &delay_exit);
			return 1;
		}
	}	
	/* Caller does what it wants */	
	return 0;
}

void 
flush_reject(void)
{
	dlink_node *ptr, *next;
	patricia_node_t *pnode;
	struct reject_data *rdata;
	
	DLINK_FOREACH_SAFE(ptr, next, reject_list.head)
	{
		pnode = ptr->data;
		rdata = pnode->data;
		rb_dlinkDelete(ptr, &reject_list);
		rb_free(rdata);
		patricia_remove(reject_tree, pnode);
	}
}

int 
remove_reject(const char *ip)
{
	patricia_node_t *pnode;
	
	/* Reject is disabled */
	if(ConfigFileEntry.reject_after_count == 0 || ConfigFileEntry.reject_duration == 0)
		return -1;

	if((pnode = match_string(reject_tree, ip)) != NULL)
	{
		struct reject_data *rdata = pnode->data;
		rb_dlinkDelete(&rdata->rnode, &reject_list);
		rb_free(rdata);
		patricia_remove(reject_tree, pnode);
		return 1;
	}
	return 0;
}

static void
delete_ipline(struct ConfItem *aconf, patricia_tree_t *t)
{
	patricia_remove(t, aconf->pnode);
	if(!aconf->clients)
	{
		free_conf(aconf);
	}
}

static struct ConfItem *
find_ipline(patricia_tree_t *t, struct sockaddr *addr)
{
	patricia_node_t *pnode;
	pnode = match_ip(t, addr);
	if(pnode != NULL)
		return (struct ConfItem *) pnode->data;
	return NULL;
}

static struct ConfItem *
find_ipline_exact(patricia_tree_t *t, struct sockaddr *addr, unsigned int bitlen)
{
	patricia_node_t *pnode;
	pnode = match_ip_exact(t, addr, bitlen);
	if(pnode != NULL)
		return (struct ConfItem *) pnode->data;
	return NULL;
}


struct ConfItem *
find_dline(struct sockaddr *addr)
{
	struct ConfItem *aconf;
	aconf = find_ipline(eline_tree, addr);
	if(aconf != NULL)
	{
		return aconf;
	}
	return (find_ipline(dline_tree, addr));
}

struct ConfItem *
find_dline_exact(struct sockaddr *addr, unsigned int bitlen)
{
	return find_ipline_exact(dline_tree, addr, bitlen);
}

void
remove_dline(struct ConfItem *aconf)
{
	delete_ipline(aconf, dline_tree);
}

void
report_dlines(struct Client *source_p)
{
	patricia_node_t *pnode;
	struct ConfItem *aconf;
	char *host, *pass, *user, *oper_reason;
	PATRICIA_WALK(dline_tree->head, pnode)
	{
		aconf = pnode->data;
		if(aconf->flags & CONF_FLAGS_TEMPORARY)
			PATRICIA_WALK_BREAK;
		get_printable_kline(source_p, aconf, &host, &pass, &user, &oper_reason);
		sendto_one_numeric(source_p, HOLD_QUEUE, RPL_STATSDLINE,
                            		     form_str (RPL_STATSDLINE),
                                             'D', host, pass,
                                             oper_reason ? "|" : "",
                                             oper_reason ? oper_reason : "");
	}
	PATRICIA_WALK_END;
}

void
report_tdlines(struct Client *source_p)
{
	patricia_node_t *pnode;
	struct ConfItem *aconf;
	char *host, *pass, *user, *oper_reason;
	PATRICIA_WALK(dline_tree->head, pnode)
	{
		aconf = pnode->data;
		if(!(aconf->flags & CONF_FLAGS_TEMPORARY))
			PATRICIA_WALK_BREAK;
		get_printable_kline(source_p, aconf, &host, &pass, &user, &oper_reason);
		sendto_one_numeric(source_p, HOLD_QUEUE, RPL_STATSDLINE,
                            		     form_str (RPL_STATSDLINE),
                                             'd', host, pass,
                                             oper_reason ? "|" : "",
                                             oper_reason ? oper_reason : "");
	}
	PATRICIA_WALK_END;
}

void
report_elines(struct Client *source_p)
{
	patricia_node_t *pnode;
	struct ConfItem *aconf;
	int port;
	char *name, *host, *pass, *user, *classname;
	PATRICIA_WALK(eline_tree->head, pnode)
	{
		aconf = pnode->data;
		get_printable_conf(aconf, &name, &host, &pass, &user, &port, &classname);
		sendto_one_numeric(source_p, HOLD_QUEUE, RPL_STATSDLINE,
                            		     form_str (RPL_STATSDLINE),
                                             'e', host, pass,
                                             "", "");
	}
	PATRICIA_WALK_END;
}


typedef struct _throttle
{
	dlink_node node;
	time_t last;
	int count;
} throttle_t;


int
throttle_add(struct sockaddr *addr)
{
	throttle_t *t;
	patricia_node_t *pnode;

	if((pnode = match_ip(throttle_tree, addr)) != NULL)
	{
		t = pnode->data;

		if(t->count > ConfigFileEntry.throttle_count)
			return 1;			

		/* Stop penalizing them after they've been throttled */
		t->last = rb_current_time();
		t->count++;

	} else {
		int bitlen = 32;
#ifdef IPV6
		if(GET_SS_FAMILY(addr) == AF_INET6)
			bitlen = 128;
#endif
		t = rb_malloc(sizeof(throttle_t));	
		t->last = rb_current_time();
		t->count = 1;
		pnode = make_and_lookup_ip(throttle_tree, addr, bitlen);
		pnode->data = t;
		rb_dlinkAdd(pnode, &t->node, &throttle_list); 
	}	
	return 0;
}

static void
throttle_expires(void *unused)
{
	dlink_node *ptr, *next;
	patricia_node_t *pnode;
	throttle_t *t;
	
	DLINK_FOREACH_SAFE(ptr, next, throttle_list.head)
	{
		pnode = ptr->data;
		t = pnode->data;		

		if(t->last + ConfigFileEntry.throttle_duration > rb_current_time())
			continue;

		rb_dlinkDelete(ptr, &throttle_list);
		rb_free(t);
		patricia_remove(throttle_tree, pnode);
	}
}



