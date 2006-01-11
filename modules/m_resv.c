/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_resv.c: Reserves(jupes) a nickname or channel.
 *
 *  Copyright (C) 2001-2002 Hybrid Development Team
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
#include "channel.h"
#include "ircd.h"
#include "numeric.h"
#include "s_serv.h"
#include "send.h"
#include "parse.h"
#include "modules.h"
#include "s_conf.h"
#include "s_newconf.h"
#include "hash.h"
#include "s_log.h"
#include "match.h"
#include "translog.h"
#include "operhash.h"

static int mo_resv(struct Client *, struct Client *, int, const char **);
static int me_resv(struct Client *, struct Client *, int, const char **);
static int mo_unresv(struct Client *, struct Client *, int, const char **);
static int me_unresv(struct Client *, struct Client *, int, const char **);

struct Message resv_msgtab = {
	"RESV", 0, 0, 0, MFLG_SLOW | MFLG_UNREG,
	{mg_ignore, mg_not_oper, mg_ignore, mg_ignore, {me_resv, 5}, {mo_resv, 3}}
};
struct Message unresv_msgtab = {
	"UNRESV", 0, 0, 0, MFLG_SLOW | MFLG_UNREG,
	{mg_ignore, mg_not_oper, mg_ignore, mg_ignore, {me_unresv, 2}, {mo_unresv, 2}}
};

mapi_clist_av1 resv_clist[] = {	&resv_msgtab, &unresv_msgtab, NULL };
DECLARE_MODULE_AV1(resv, NULL, NULL, resv_clist, NULL, NULL, "$Revision: 19295 $");

static void parse_resv(struct Client *source_p, const char *name,
			const char *reason, int temp_time);

static void remove_resv(struct Client *source_p, const char *name);

/*
 * mo_resv()
 *      parv[0] = sender prefix
 *      parv[1] = channel/nick to forbid
 *      parv[2] = reason
 */
static int
mo_resv(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	const char *name;
	const char *reason;
	const char *target_server = NULL;
	int temp_time;
	int loc = 1;

	/* RESV [time] <name> [ON <server>] :<reason> */

	if((temp_time = valid_temp_time(parv[loc])) >= 0)
		loc++;
	/* we just set temp_time to -1! */
	else
		temp_time = 0;

	name = parv[loc];
	loc++;

	if((parc >= loc+2) && (irccmp(parv[loc], "ON") == 0))
	{
		if(!IsOperRemoteBan(source_p))
		{
			sendto_one(source_p, POP_QUEUE, form_str(ERR_NOPRIVS),
				me.name, source_p->name, "remoteban");
			return 0;
		}

		target_server = parv[loc+1];
		loc += 2;
	}

	if(parc <= loc || EmptyString(parv[loc]))
	{
		sendto_one(source_p, POP_QUEUE, form_str(ERR_NEEDMOREPARAMS),
			   me.name, source_p->name, "RESV");
		return 0;
	}

	reason = parv[loc];

	/* remote resv.. */
	if(target_server)
	{
		sendto_match_servs(source_p, target_server, CAP_ENCAP, NOCAPS,
				"ENCAP %s RESV %d %s 0 :%s",
				target_server, temp_time, name, reason);

		if(match(target_server, me.name) == 0)
			return 0;
	}
	else if(ircd_dlink_list_length(&cluster_conf_list) > 0)
		cluster_generic(source_p, "RESV",
				(temp_time > 0) ? SHARED_TRESV : SHARED_PRESV,
				"%d %s 0 :%s",
				temp_time, name, reason);

	parse_resv(source_p, name, reason, temp_time);

	return 0;
}

static int
me_resv(struct Client *client_p, struct Client *source_p,
	int parc, const char *parv[])
{
	/* time name 0 :reason */
	if(!IsPerson(source_p))
		return 0;

	parse_resv(source_p, parv[2], parv[4], atoi(parv[1]));
	return 0;
}

static void
notify_resv(struct Client *source_p, const char *name, const char *reason, int temp_time)
{
	if(temp_time)
	{
		sendto_realops_flags(UMODE_ALL, L_ALL,
			     "%s added temporary %d min. RESV for [%s] [%s]",
			     get_oper_name(source_p), temp_time / 60,
			     name, reason);
		ilog(L_KLINE, "R %s %d %s %s",
			get_oper_name(source_p), temp_time / 60,
			name, reason);
		sendto_one_notice(source_p, POP_QUEUE, ":Added temporary %d min. RESV [%s]",
				temp_time / 60, name);
	}
	else
	{
		sendto_realops_flags(UMODE_ALL, L_ALL,
				"%s added RESV for [%s] [%s]",
				get_oper_name(source_p), name, reason);
		ilog(L_KLINE, "R %s 0 %s %s",
			get_oper_name(source_p), name, reason);
		sendto_one_notice(source_p, POP_QUEUE, ":Added RESV for [%s] [%s]",
				  name, reason);
	}
}


/* parse_resv()
 *
 * inputs       - source_p if error messages wanted
 * 		- thing to resv
 * 		- reason for resv
 * outputs	-
 * side effects - will parse the resv and create it if valid
 */
static void
parse_resv(struct Client *source_p, const char *name, 
	   const char *reason, int temp_time)
{
	struct ConfItem *aconf;
	const char *oper = get_oper_name(source_p);

	if(!MyClient(source_p) && 
	   !find_shared_conf(source_p->username, source_p->host,
			source_p->servptr->name, 
			(temp_time > 0) ? SHARED_TRESV : SHARED_PRESV))
		return;

	if(IsChannelName(name))
	{
		const char *p;

		if(hash_find_resv(name))
		{
			sendto_one_notice(source_p, POP_QUEUE,
					":A RESV has already been placed on channel: %s",
					name);
			return;
		}

		if(strlen(name) > CHANNELLEN)
		{
			sendto_one_notice(source_p, POP_QUEUE, ":Invalid RESV length: %s",
					  name);
			return;
		}

		for(p = name; *p; p++)
		{
			if(!IsChanChar(*p))
			{
				sendto_one_notice(source_p, POP_QUEUE, 
						":Invalid character '%c' in resv",
						*p);
				return;
			}
		}

		if(strchr(reason, '"'))
		{
			sendto_one_notice(source_p, POP_QUEUE,
					":Invalid character '\"' in comment");
			return;
		}

		aconf = make_conf();
		aconf->status = CONF_RESV_CHANNEL;
		aconf->port = 0;
		aconf->host = ircd_strdup(name);
		aconf->passwd = ircd_strdup(reason);
		aconf->info.oper = operhash_add(oper);
		add_to_hash(HASH_RESV, aconf->host, aconf);

		notify_resv(source_p, aconf->host, aconf->passwd, temp_time);

		if(temp_time > 0)
		{
			aconf->flags |= CONF_FLAGS_TEMPORARY;
			aconf->hold = ircd_currenttime + temp_time;
		}
		else
		{
			translog_add_ban(TRANS_RESV, source_p, aconf->host, NULL,
					aconf->passwd, NULL, 0);
			aconf->hold = ircd_currenttime;
		}
	}
	else if(clean_resv_nick(name))
	{
		if(strlen(name) > NICKLEN*2)
		{
			sendto_one_notice(source_p, POP_QUEUE, ":Invalid RESV length: %s",
					   name);
			return;
		}

		if(strchr(reason, '"'))
		{
			sendto_one_notice(source_p, POP_QUEUE,
					":Invalid character '\"' in comment");
			return;
		}

		if(!valid_wild_card_simple(name))
		{
			sendto_one_notice(source_p, POP_QUEUE,
					   ":Please include at least %d non-wildcard "
					   "characters with the resv",
					   ConfigFileEntry.min_nonwildcard_simple);
			return;
		}

		if(find_nick_resv(name))
		{
			sendto_one_notice(source_p, POP_QUEUE,
					   ":A RESV has already been placed on nick: %s",
					   name);
			return;
		}

		aconf = make_conf();
		aconf->status = CONF_RESV_NICK;
		aconf->port = 0;
		aconf->host = ircd_strdup(name);
		aconf->passwd = ircd_strdup(reason);
		aconf->info.oper = operhash_add(oper);
		ircd_dlinkAddAlloc(aconf, &resv_conf_list);

		notify_resv(source_p, aconf->host, aconf->passwd, temp_time);

		if(temp_time > 0)
		{
			aconf->flags |= CONF_FLAGS_TEMPORARY;
			aconf->hold = ircd_currenttime + temp_time;
		}
		else
		{
			translog_add_ban(TRANS_RESV, source_p, aconf->host, NULL,
					aconf->passwd, NULL, 0);
			aconf->hold = ircd_currenttime;
		}
			
	}
	else
		sendto_one_notice(source_p, POP_QUEUE,
				  ":You have specified an invalid resv: [%s]",
				  name);
}

/*
 * mo_unresv()
 *     parv[0] = sender prefix
 *     parv[1] = channel/nick to unforbid
 */
static int
mo_unresv(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	if((parc == 4) && (irccmp(parv[2], "ON") == 0))
	{
		if(!IsOperRemoteBan(source_p))
		{
			sendto_one(source_p, POP_QUEUE, form_str(ERR_NOPRIVS),
				me.name, source_p->name, "remoteban");
			return 0;
		}

		sendto_match_servs(source_p, parv[3], CAP_ENCAP, NOCAPS,
				"ENCAP %s UNRESV %s",
				parv[3], parv[1]);

		if(match(parv[3], me.name) == 0)
			return 0;
	}
	else if(ircd_dlink_list_length(&cluster_conf_list) > 0)
		cluster_generic(source_p, "UNRESV", SHARED_UNRESV, 
				"%s", parv[1]);

	remove_resv(source_p, parv[1]);
	return 0;
}

static int
me_unresv(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	const char *name;

	/* name */
	if(!IsPerson(source_p))
		return 0;

	name = parv[1];

	if(!find_shared_conf(source_p->username, source_p->host,
				source_p->servptr->name, SHARED_UNRESV))
		return 0;

	remove_resv(source_p, name);
	return 0;
}

static void
remove_resv(struct Client *source_p, const char *name)
{
	struct ConfItem *aconf = NULL;

	if(IsChannelName(name))
	{
		if(((aconf = hash_find_resv(name)) == NULL) ||
		   IsConfPermanent(aconf))
		{
			sendto_one_notice(source_p, POP_QUEUE,
					":No RESV for %s", name);
			return;
		}

		/* schedule it to transaction log */
		if((aconf->flags & CONF_FLAGS_TEMPORARY) == 0)
			translog_del_ban(TRANS_RESV, name, NULL);

		del_from_hash(HASH_RESV, name, aconf);
		free_conf(aconf);
	}
	else
	{
		dlink_node *ptr;

		DLINK_FOREACH(ptr, resv_conf_list.head)
		{
			aconf = ptr->data;

			if(irccmp(aconf->host, name))
				aconf = NULL;
			else
				break;
		}

		if(aconf == NULL || IsConfPermanent(aconf))
		{
			sendto_one_notice(source_p, POP_QUEUE,
					":No RESV for %s", name);
			return;
		}

		/* schedule it to transaction log */
		if((aconf->flags & CONF_FLAGS_TEMPORARY) == 0)
			translog_del_ban(TRANS_RESV, name, NULL);

		/* already have ptr from the loop above.. */
		ircd_dlinkDestroy(ptr, &resv_conf_list);
		free_conf(aconf);
	}

	sendto_one_notice(source_p, POP_QUEUE, ":RESV for [%s] is removed", name);
	sendto_realops_flags(UMODE_ALL, L_ALL,
			"%s has removed the RESV for: [%s]", 
			get_oper_name(source_p), name);
	ilog(L_KLINE, "UR %s %s", get_oper_name(source_p), name);
}

