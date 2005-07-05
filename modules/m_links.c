/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_links.c: Shows what servers are currently connected.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002-2005 ircd-ratbox development team
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 *
 *  $Id$
 */

#include "stdinc.h"
#include "ircd_lib.h"
#include "struct.h"
#include "client.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "s_serv.h"
#include "send.h"
#include "s_conf.h"
#include "parse.h"
#include "hook.h"
#include "modules.h"
#include "cache.h"


static int m_links(struct Client *, struct Client *, int, const char **);
static int mo_links(struct Client *, struct Client *, int, const char **);
static int modinit(void);
static void moddeinit(void);
static int status;
struct Message links_msgtab = {
	"LINKS", 0, 0, 0, MFLG_SLOW,
	{mg_unreg, {m_links, 0}, {mo_links, 0}, mg_ignore, mg_ignore, {mo_links, 0}}
};

int doing_links_hook;

mapi_clist_av1 links_clist[] = { &links_msgtab, NULL };
mapi_hlist_av1 links_hlist[] = {
	{ "doing_links",	&doing_links_hook },
	{ NULL, NULL }
};

DECLARE_MODULE_AV1(links, modinit, moddeinit, links_clist, links_hlist, NULL, "$Revision: 19256 $");

static void send_links_cache(struct Client *source_p);
static dlink_list links_cache_list;

static int
modinit(void)
{
	if(ConfigServerHide.links_disabled == 1)
		return 0;
	
	if(ConfigServerHide.links_delay > 0)
	{
		cache_links(NULL);
		eventAdd("cache_links", cache_links, NULL,
			    ConfigServerHide.links_delay);
		status = 1;
        }
        return 0;
}

static void
moddeinit(void)
{
	dlink_node *ptr, *next_ptr;
        DLINK_FOREACH_SAFE(ptr, next_ptr, links_cache_list.head)
        {
		MyFree(ptr->data);
		free_dlink_node(ptr);
        }
        eventDelete(cache_links, NULL);
        return;
}

static void
check_delay(void)
{
	/* in case things got twiddled on a rehash on us */
	if(status == 0 && ConfigServerHide.links_disabled == 0)
	{
		cache_links(NULL);
		eventAdd("cache_links", cache_links, NULL, ConfigServerHide.links_delay);
		status = 1; 
	}
}

/*
 * m_links - LINKS message handler
 *      parv[0] = sender prefix
 *      parv[1] = servername mask
 * or
 *      parv[0] = sender prefix
 *      parv[1] = server to query 
 *      parv[2] = servername mask
 */
static int
m_links(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	check_delay();
	if(ConfigServerHide.flatten_links && !IsExemptShide(source_p))
		send_links_cache(source_p);
	else
		mo_links(client_p, source_p, parc, parv);

	return 0;
}

static int
mo_links(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	const char *mask = "";
	struct Client *target_p;
	char clean_mask[2 * HOSTLEN + 4];
	hook_data hd;

	dlink_node *ptr;

	if(parc > 2)
	{
		if(hunt_server(client_p, source_p, ":%s LINKS %s :%s", 1, parc, parv)
		   != HUNTED_ISME)
			return 0;

		mask = parv[2];
	}
	else if(parc == 2)
		mask = parv[1];

	if(*mask)		/* only necessary if there is a mask */
		mask = collapse(clean_string
				(clean_mask, (const unsigned char *) mask, 2 * HOSTLEN));

	hd.client = source_p;
	hd.arg1 = mask;
	hd.arg2 = NULL;

	call_hook(doing_links_hook, &hd);

	DLINK_FOREACH(ptr, global_serv_list.head)
	{
		target_p = ptr->data;

		if(*mask && !match(mask, target_p->name))
			continue;

		/* We just send the reply, as if theyre here theres either no SHIDE,
		 * or theyre an oper..  
		 */
		sendto_one_numeric(source_p, HOLD_QUEUE, RPL_LINKS, form_str(RPL_LINKS),
				   target_p->name, target_p->serv->up,
				   target_p->hopcount,
				   target_p->info[0] ? target_p->info : "(Unknown Location)");
	}

	sendto_one_numeric(source_p, POP_QUEUE, RPL_ENDOFLINKS, form_str(RPL_ENDOFLINKS),
			   EmptyString(mask) ? "*" : mask);

	return 0;
}

void
cache_links(void *unused)
{
	struct Client *target_p;
	dlink_node *ptr;
	dlink_node *next_ptr;
	char *links_line;

	DLINK_FOREACH_SAFE(ptr, next_ptr, links_cache_list.head)
	{
		MyFree(ptr->data);
		free_dlink_node(ptr);
	}

	links_cache_list.head = links_cache_list.tail = NULL;
	links_cache_list.length = 0;

	DLINK_FOREACH(ptr, global_serv_list.head)
	{
		target_p = ptr->data;

		/* skip ourselves (done in /links) and hidden servers */
		if(IsMe(target_p) ||
		   (IsHidden(target_p) && !ConfigServerHide.disable_hidden))
			continue;

		/* if the below is ever modified, change LINKSLINELEN */
		links_line = MyMalloc(LINKSLINELEN);
		ircsnprintf(links_line, LINKSLINELEN, "%s %s :1 %s",
			   target_p->name, me.name, 
			   target_p->info[0] ? target_p->info : 
			    "(Unknown Location)");

		dlinkAddTailAlloc(links_line, &links_cache_list);
	}
}

/* send_links_cache()
 *
 * inputs	- client to send to
 * outputs	- the cached links, us, and RPL_ENDOFLINKS
 * side effects	-
 */
static void
send_links_cache(struct Client *source_p)
{
	dlink_node *ptr;

	DLINK_FOREACH(ptr, links_cache_list.head)
	{
		sendto_one(source_p, HOLD_QUEUE, ":%s 364 %s %s",
			   me.name, source_p->name, (const char *)ptr->data);
	}

	sendto_one_numeric(source_p, HOLD_QUEUE, RPL_LINKS, form_str(RPL_LINKS), 
			   me.name, me.name, 0, me.info);

	sendto_one_numeric(source_p, POP_QUEUE, RPL_ENDOFLINKS, form_str(RPL_ENDOFLINKS), "*");
}

