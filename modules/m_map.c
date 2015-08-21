/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_map.c: Sends an Undernet compatible map to a user.
 *
 *  Copyright (C) 2002 by the past and present ircd coders, and others.
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
#include <client.h>
#include <parse.h>
#include <modules.h>
#include <numeric.h>
#include <send.h>
#include <s_conf.h>
#include <ircd.h>
#include <match.h>

#define USER_COL       50	/* display | Users: %d at col 50 */

static int m_map(struct Client *client_p, struct Client *source_p, int parc, const char *parv[]);
static int mo_map(struct Client *client_p, struct Client *source_p, int parc, const char *parv[]);




struct Message map_msgtab = {
	.cmd = "MAP",
	.handlers[UNREGISTERED_HANDLER] =	{ mm_unreg },
	.handlers[CLIENT_HANDLER] =		{ .handler = m_map },
	.handlers[RCLIENT_HANDLER] =		{  mm_ignore },
	.handlers[SERVER_HANDLER] =		{  mm_ignore },
	.handlers[ENCAP_HANDLER] =		{  mm_ignore },
	.handlers[OPER_HANDLER] =		{ .handler = mo_map },
};

mapi_clist_av1 map_clist[] = { &map_msgtab, NULL };

DECLARE_MODULE_AV1(map, NULL, NULL, map_clist, NULL, NULL, "$Revision$");

static void dump_map(struct Client *client_p, struct Client *root, char *pbuf);

static char buf[IRCD_BUFSIZE];

/* m_map
**	parv[0] = sender prefix
*/
static int
m_map(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	if((!IsExemptShide(source_p) && ConfigServerHide.flatten_links) ||
	   ConfigFileEntry.map_oper_only)
	{
		m_not_oper(client_p, source_p, parc, parv);
		return 0;
	}
	SetCork(source_p);
	dump_map(source_p, &me, buf);
	ClearCork(source_p);
	sendto_one_numeric(source_p, s_RPL(RPL_MAPEND));
	return 0;
}

/*
** mo_map
**	parv[0] = sender prefix
*/
static int
mo_map(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	SetCork(source_p);
	dump_map(source_p, &me, buf);
	ClearCork(source_p);
	sendto_one_numeric(source_p, s_RPL(RPL_MAPEND));

	return 0;
}

/*
** dump_map
**   dumps server map, called recursively.
*    you must pop the sendq after this is done..with the MAPEND numeric
*/
static void
dump_map(struct Client *client_p, struct Client *root_p, char *pbuf)
{
	int cnt = 0, i = 0, len;
	struct Client *server_p;
	rb_dlink_node *ptr;
	*pbuf = '\0';

	rb_strlcat(pbuf, root_p->name, IRCD_BUFSIZE);
	if(has_id(root_p))
	{
		rb_strlcat(pbuf, "[", IRCD_BUFSIZE);
		rb_strlcat(pbuf, root_p->id, IRCD_BUFSIZE);
		rb_strlcat(pbuf, "]", IRCD_BUFSIZE);
	}
	len = strlen(buf);
	buf[len] = ' ';

	if(len < USER_COL)
	{
		for(i = len + 1; i < USER_COL; i++)
		{
			buf[i] = '-';
		}
	}

	snprintf(buf + USER_COL, IRCD_BUFSIZE - USER_COL,
		 " | Users: %5lu (%4.1f%%)", rb_dlink_list_length(&root_p->serv->users),
		 (float) 100 * (float) rb_dlink_list_length(&root_p->serv->users) /
		 (float) Count.total);

	sendto_one_numeric(client_p, s_RPL(RPL_MAP), buf);

	if(root_p->serv->servers.head != NULL)
	{
		cnt += rb_dlink_list_length(&root_p->serv->servers);

		if(cnt)
		{
			if(pbuf > buf + 3)
			{
				pbuf[-2] = ' ';
				if(pbuf[-3] == '`')
					pbuf[-3] = ' ';
			}
		}
	}
	i = 1;
	RB_DLINK_FOREACH(ptr, root_p->serv->servers.head)
	{
		server_p = ptr->data;
		*pbuf = ' ';
		if(i < cnt)
			*(pbuf + 1) = '|';
		else
			*(pbuf + 1) = '`';

		*(pbuf + 2) = '-';
		*(pbuf + 3) = ' ';
		dump_map(client_p, server_p, pbuf + 4);

		i++;
	}
}
