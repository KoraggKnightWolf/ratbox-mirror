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
#include "irc_string.h"
#include "banconf.h"

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

static void handle_remote_unresv(struct Client *source_p, const char *name);
static void remove_resv(struct Client *source_p, const char *name);
static int remove_temp_resv(struct Client *source_p, const char *name);

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
	else if(dlink_list_length(&cluster_conf_list) > 0)
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

	if(!MyClient(source_p) && 
	   !find_shared_conf(source_p->username, source_p->host,
			source_p->user->server, 
			(temp_time > 0) ? SHARED_TRESV : SHARED_PRESV))
		return;

	if(IsChannelName(name))
	{
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

		if(strchr(reason, '"'))
		{
			sendto_one_notice(source_p, POP_QUEUE,
					":Invalid character '\"' in comment");
			return;
		}

		aconf = make_conf();
		aconf->status = CONF_RESV_CHANNEL;
		aconf->port = 0;
		DupString(aconf->name, name);
		DupString(aconf->passwd, reason);
		add_to_resv_hash(aconf->name, aconf);

		if(temp_time > 0)
		{
			aconf->hold = ircd_currenttime + temp_time;

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
			write_confitem(RESV_TYPE, source_p, NULL, aconf->name, 
					aconf->passwd, NULL, NULL, 0);
			banconf_add_write(TRANS_RESV, source_p, aconf->name, NULL,
					aconf->passwd, NULL);
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
		DupString(aconf->name, name);
		DupString(aconf->passwd, reason);
		dlinkAddAlloc(aconf, &resv_conf_list);

		if(temp_time > 0)
		{
			aconf->hold = ircd_currenttime + (temp_time * 60);

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
			write_confitem(RESV_TYPE, source_p, NULL, aconf->name, 
					aconf->passwd, NULL, NULL, 0);
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
	else if(dlink_list_length(&cluster_conf_list) > 0)
		cluster_generic(source_p, "UNRESV", SHARED_UNRESV, 
				"%s", parv[1]);

	if(remove_temp_resv(source_p, parv[1]))
		return 0;

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
				source_p->user->server, SHARED_UNRESV))
		return 0;

	if(remove_temp_resv(source_p, name))
		return 0;

	remove_resv(source_p, name);
	return 0;
}

static int
remove_temp_resv(struct Client *source_p, const char *name)
{
	struct ConfItem *aconf = NULL;

	if(IsChannelName(name))
	{
		if((aconf = hash_find_resv(name)) == NULL)
			return 0;

		/* its permanent, let remove_resv do it properly */
		if(!aconf->hold)
			return 0;

		del_from_resv_hash(name, aconf);
		free_conf(aconf);
	}
	else
	{
		dlink_node *ptr;

		DLINK_FOREACH(ptr, resv_conf_list.head)
		{
			aconf = ptr->data;

			if(irccmp(aconf->name, name))
				aconf = NULL;
			else
				break;
		}

		if(aconf == NULL)
			return 0;

		/* permanent, remove_resv() needs to do it properly */
		if(!aconf->hold)
			return 0;

		/* already have ptr from the loop above.. */
		dlinkDestroy(ptr, &resv_conf_list);
		free_conf(aconf);
	}

	sendto_one_notice(source_p, POP_QUEUE, ":RESV for [%s] is removed", name);
	sendto_realops_flags(UMODE_ALL, L_ALL,
			"%s has removed the RESV for: [%s]", 
			get_oper_name(source_p), name);
	ilog(L_KLINE, "UR %s %s", get_oper_name(source_p), name);

	return 1;
}

/* remove_resv()
 *
 * inputs	- client removing the resv
 * 		- resv to remove
 * outputs	-
 * side effects - resv if found, is removed
 */
static void
remove_resv(struct Client *source_p, const char *name)
{
	FILE *in, *out;
	char buf[BUFSIZE];
	char buff[BUFSIZE];
	char temppath[BUFSIZE];
	const char *filename;
	mode_t oldumask;
	char *p;
	int error_on_write = 0;
	int found_resv = 0;

	ircd_sprintf(temppath, "%s.tmp", ConfigFileEntry.resvfile);
	filename = get_conf_name(RESV_TYPE);

	if((in = fopen(filename, "r")) == NULL)
	{
		sendto_one_notice(source_p, POP_QUEUE, ":Cannot open %s", filename);
		return;
	}

	oldumask = umask(0);

	if((out = fopen(temppath, "w")) == NULL)
	{
		sendto_one_notice(source_p, POP_QUEUE, ":Cannot open %s", temppath);
		fclose(in);
		umask(oldumask);
		return;
	}

	umask(oldumask);

	while (fgets(buf, sizeof(buf), in))
	{
		const char *resv_name;

		if(error_on_write)
		{
			if(temppath != NULL)
				(void) unlink(temppath);

			break;
		}

		strlcpy(buff, buf, sizeof(buff));

		if((p = strchr(buff, '\n')) != NULL)
			*p = '\0';

		if((*buff == '\0') || (*buff == '#'))
		{
			error_on_write = (fputs(buf, out) < 0) ? YES : NO;
			continue;
		}

		if((resv_name = getfield(buff)) == NULL)
		{
			error_on_write = (fputs(buf, out) < 0) ? YES : NO;
			continue;
		}

		if(irccmp(resv_name, name) == 0)
		{
			found_resv++;
		}
		else
		{
			error_on_write = (fputs(buf, out) < 0) ? YES : NO;
		}
	}

	fclose(in);
	fclose(out);

	if(error_on_write)
	{
		sendto_one_notice(source_p, POP_QUEUE, ":Couldn't write temp resv file, aborted");
		return;
	}
	else if(!found_resv)
	{
		sendto_one_notice(source_p, POP_QUEUE, ":No RESV for %s", name);

		if(temppath != NULL)
			(void) unlink(temppath);

		return;
	}

	(void) rename(temppath, filename);
	rehash_bans(0);

	sendto_one_notice(source_p, POP_QUEUE, ":RESV for [%s] is removed", name);
	sendto_realops_flags(UMODE_ALL, L_ALL,
			     "%s has removed the RESV for: [%s]", get_oper_name(source_p), name);
	ilog(L_KLINE, "UR %s %s", get_oper_name(source_p), name);
}
