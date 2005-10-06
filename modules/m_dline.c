/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_dline.c: Bans/unbans a user.
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
#include "struct.h"
#include "client.h"
#include "match.h"
#include "reject.h"
#include "ircd.h"
#include "hostmask.h"
#include "numeric.h"
#include "s_conf.h"
#include "s_newconf.h"
#include "s_log.h"
#include "send.h"
#include "parse.h"
#include "modules.h"
#include "translog.h"

static int mo_dline(struct Client *, struct Client *, int, const char **);
static int mo_undline(struct Client *, struct Client *, int, const char **);

struct Message dline_msgtab = {
	"DLINE", 0, 0, 0, MFLG_SLOW,
	{mg_unreg, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {mo_dline, 2}}
};
struct Message undline_msgtab = {
	"UNDLINE", 0, 0, 0, MFLG_SLOW,
	{mg_unreg, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {mo_undline, 2}}
};

mapi_clist_av1 dline_clist[] = { &dline_msgtab, &undline_msgtab, NULL };
DECLARE_MODULE_AV1(dline, NULL, NULL, dline_clist, NULL, NULL, "$Revision: 19295 $");

static int valid_comment(char *comment);
static int remove_temp_dline(const char *);
static void remove_perm_dline(struct Client *source_p, const char *host);
static void check_dlines(void);

/* mo_dline()
 * 
 *   parv[1] - dline to add
 *   parv[2] - reason
 */
static int
mo_dline(struct Client *client_p, struct Client *source_p,
	 int parc, const char *parv[])
{
	char def[] = "No Reason";
	const char *dlhost;
	char *oper_reason;
	char *reason = def;
	struct irc_sockaddr_storage daddr;
	char cidr_form_host[HOSTLEN + 1];
	struct ConfItem *aconf;
	int bits;
	char dlbuffer[IRCD_BUFSIZE];
	const char *current_date;
	int tdline_time = 0;
	int loc = 1;

	if(!IsOperK(source_p))
	{
		sendto_one(source_p, POP_QUEUE, form_str(ERR_NOPRIVS),
			   me.name, source_p->name, "kline");
		return 0;
	}

	if((tdline_time = valid_temp_time(parv[loc])) >= 0)
		loc++;

	if(parc < loc + 1)
	{
		sendto_one(source_p, POP_QUEUE, form_str(ERR_NEEDMOREPARAMS),
			   me.name, source_p->name, "DLINE");
		return 0;
	}

	dlhost = parv[loc];
	strlcpy(cidr_form_host, dlhost, sizeof(cidr_form_host));

	if(!parse_netmask(dlhost, NULL, &bits))
	{
		sendto_one(source_p, POP_QUEUE, ":%s NOTICE %s :Invalid D-Line",
			   me.name, source_p->name);
		return 0;
	}

	loc++;

	if(parc >= loc + 1)	/* host :reason */
	{
		if(!EmptyString(parv[loc]))
			reason = LOCAL_COPY(parv[loc]);

		if(!valid_comment(reason))
		{
			sendto_one(source_p, POP_QUEUE, 
				   ":%s NOTICE %s :Invalid character '\"' in comment",
				   me.name, source_p->name);
			return 0;
		}
	}

	if(IsOperAdmin(source_p))
	{
		if(bits < 8)
		{
			sendto_one(source_p, POP_QUEUE, 
				   ":%s NOTICE %s :For safety, bitmasks less than 8 require conf access.",
				   me.name, parv[0]);
			return 0;
		}
	}
	else
	{
		if(bits < 16)
		{
			sendto_one(source_p, POP_QUEUE, 
				   ":%s NOTICE %s :Dline bitmasks less than 16 are for admins only.",
				   me.name, parv[0]);
			return 0;
		}
	}

	if(ConfigFileEntry.non_redundant_klines)
	{
		const char *creason;
		int t = AF_INET, ty, b;
		ty = parse_netmask(dlhost, (struct sockaddr *)&daddr, &b);
#ifdef IPV6
		if(ty == HM_IPV6)
			t = AF_INET6;
		else
#endif
			t = AF_INET;
						
		if((aconf = find_dline((struct sockaddr *)&daddr)) != NULL)
		{
			int bx;
			parse_netmask(aconf->host, NULL, &bx);
			if(b >= bx)
			{
				creason = aconf->passwd ? aconf->passwd : "<No Reason>";
				if(IsConfExemptKline(aconf))
					sendto_one(source_p, POP_QUEUE, 
						   ":%s NOTICE %s :[%s] is (E)d-lined by [%s] - %s",
						   me.name, parv[0], dlhost, aconf->host, creason);
				else
					sendto_one(source_p, POP_QUEUE, 
						   ":%s NOTICE %s :[%s] already D-lined by [%s] - %s",
						   me.name, parv[0], dlhost, aconf->host, creason);
				return 0;
			}
		}
	}

	ircd_set_time();
	current_date = smalldate();

	aconf = make_conf();
	aconf->status = CONF_DLINE;
	DupString(aconf->host, dlhost);

	/* Look for an oper reason */
	if((oper_reason = strchr(reason, '|')) != NULL)
	{
		*oper_reason = '\0';
		oper_reason++;

		if(!EmptyString(oper_reason))
			DupString(aconf->spasswd, oper_reason);
	}

	if(tdline_time > 0)
	{
		ircd_snprintf(dlbuffer, sizeof(dlbuffer), 
			 "Temporary D-line %d min. - %s (%s)",
			 (int) (tdline_time / 60), reason, current_date);
		DupString(aconf->passwd, dlbuffer);
		aconf->hold = ircd_currenttime + tdline_time;
		add_temp_dline(aconf);

		if(EmptyString(oper_reason))
		{
			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "%s added temporary %d min. D-Line for [%s] [%s]",
					     get_oper_name(source_p), tdline_time / 60,
					     aconf->host, reason);
			ilog(L_KLINE, "D %s %d %s %s",
				get_oper_name(source_p), tdline_time / 60,
				aconf->host, reason);
		}
		else
		{
			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "%s added temporary %d min. D-Line for [%s] [%s|%s]",
					     get_oper_name(source_p), tdline_time / 60,
					     aconf->host, reason, oper_reason);
			ilog(L_KLINE, "D %s %d %s %s|%s",
				get_oper_name(source_p), tdline_time / 60,
				aconf->host, reason, oper_reason);
		}

		sendto_one(source_p, POP_QUEUE, ":%s NOTICE %s :Added temporary %d min. D-Line for [%s]",
			   me.name, source_p->name, tdline_time / 60, aconf->host);
	}
	else
	{
		ircd_snprintf(dlbuffer, sizeof(dlbuffer), "%s (%s)", reason, current_date);
		DupString(aconf->passwd, dlbuffer);
		add_dline(aconf);

		if(EmptyString(oper_reason))
		{
			sendto_realops_flags(UMODE_ALL, L_ALL,
					"%s added D-Line for [%s] [%s]",
					get_oper_name(source_p), aconf->host, reason);
			ilog(L_KLINE, "D %s 0 %s %s",
				get_oper_name(source_p), aconf->host, reason);
		}
		else
		{
			sendto_realops_flags(UMODE_ALL, L_ALL,
					"%s added D-Line for [%s] [%s|%s]",
					get_oper_name(source_p), aconf->host, 
					reason, oper_reason);
			ilog(L_KLINE, "D %s 0 %s %s|%s",
				get_oper_name(source_p), aconf->host, 
				reason, oper_reason);
		}

		sendto_one(source_p, POP_QUEUE,
			   ":%s NOTICE %s :Added D-Line [%s] to %s", me.name,
			   source_p->name, aconf->host, ConfigFileEntry.dlinefile);

		translog_add_ban(TRANS_DLINE, source_p, aconf->host, NULL,
				reason, oper_reason);
	}

	check_dlines();
	return 0;
}

/* mo_undline()
 *
 *      parv[1] = dline to remove
 */
static int
mo_undline(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	const char *cidr;

	if(!IsOperUnkline(source_p))
	{
		sendto_one(source_p, POP_QUEUE, form_str(ERR_NOPRIVS),
			   me.name, source_p->name, "unkline");
		return 0;
	}

	cidr = parv[1];

	if(parse_netmask(cidr, NULL, NULL) == HM_HOST)
	{
		sendto_one(source_p, POP_QUEUE, ":%s NOTICE %s :Invalid D-Line",
			   me.name, source_p->name);
		return 0;
	}

	if(remove_temp_dline(cidr))
	{
		sendto_one(source_p, POP_QUEUE, 
			   ":%s NOTICE %s :Un-dlined [%s] from temporary D-lines",
			   me.name, parv[0], cidr);
		sendto_realops_flags(UMODE_ALL, L_ALL,
				     "%s has removed the temporary D-Line for: [%s]",
				     get_oper_name(source_p), cidr);
		ilog(L_KLINE, "UD %s %s", get_oper_name(source_p), cidr);
		return 0;
	}

	remove_perm_dline(source_p, cidr);
	return 0;
}

/*
 * valid_comment
 * inputs	- pointer to client
 *              - pointer to comment
 * output       - 0 if no valid comment, 1 if valid
 * side effects - NONE
 */
static int
valid_comment(char *comment)
{
	if(strchr(comment, '"'))
		return 0;

	if(strlen(comment) > REASONLEN)
		comment[REASONLEN] = '\0';

	return 1;
}

/* remove_temp_dline()
 *
 * inputs       - hostname to undline
 * outputs      -
 * side effects - tries to undline anything that matches
 */
static int
remove_temp_dline(const char *host)
{
	struct ConfItem *aconf;
	dlink_node *ptr;
	int i;

	for (i = 0; i < LAST_TEMP_TYPE; i++)
	{
		DLINK_FOREACH(ptr, temp_dlines[i].head)
		{
			aconf = ptr->data;

			if(irccmp(aconf->host, host))
				continue;

			ircd_dlinkDestroy(ptr, &temp_dlines[i]);
			delete_one_address_conf(aconf->host, aconf);
			return YES;
		}
	}

	return NO;
}

void
remove_perm_dline(struct Client *source_p, const char *host)
{
	struct AddressRec *arec;
	struct ConfItem *aconf;
	int i;

	HOSTHASH_WALK(i, arec)
	{
		if((arec->type & ~CONF_SKIPUSER) == CONF_DLINE)
		{
			aconf = arec->aconf;

			if(aconf->flags & CONF_FLAGS_TEMPORARY)
				continue;

			if(irccmp(aconf->host, host))
				continue;

			delete_one_address_conf(host, aconf);
			translog_del_ban(TRANS_DLINE, host, NULL);


			sendto_one(source_p, POP_QUEUE, 
					":%s NOTICE %s :D-Line for [%s] is removed", 
					me.name, source_p->name, host);
			sendto_realops_flags(UMODE_ALL, L_ALL,
					"%s has removed the D-Line for: [%s]", 
					get_oper_name(source_p), host);
			ilog(L_KLINE, "UD %s %s", get_oper_name(source_p), host);

			return;
		}
	}
	HOSTHASH_WALK_END

	sendto_one(source_p, POP_QUEUE, ":%s NOTICE %s :No D-Line for %s",
		   me.name, source_p->name, host);
}

/* check_dlines()
 *
 * inputs       -
 * outputs      -
 * side effects - all clients will be checked for dlines
 */
static void
check_dlines(void)
{
	struct Client *client_p;
	struct ConfItem *aconf;
	dlink_node *ptr;
	dlink_node *next_ptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, lclient_list.head)
	{
		client_p = ptr->data;

		if(IsMe(client_p))
			continue;

		if((aconf = find_dline((struct sockaddr *)&client_p->localClient->ip)) != NULL)
		{
			if(aconf->status & CONF_EXEMPTDLINE)
				continue;

			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "DLINE active for %s",
					     get_client_name(client_p, HIDE_IP));

			notify_banned_client(client_p, aconf, D_LINED);
			continue;
		}
	}

	/* dlines need to be checked against unknowns too */
	DLINK_FOREACH_SAFE(ptr, next_ptr, unknown_list.head)
	{
		client_p = ptr->data;

		if((aconf = find_dline((struct sockaddr *)&client_p->localClient->ip)) != NULL)
		{
			if(aconf->status & CONF_EXEMPTDLINE)
				continue;

			notify_banned_client(client_p, aconf, D_LINED);
		}
	}
}

