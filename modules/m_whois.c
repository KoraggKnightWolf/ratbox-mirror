/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_whois.c: Shows who a user is.
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
#include "hash.h"
#include "channel.h"
#include "ircd.h"
#include "numeric.h"
#include "s_conf.h"
#include "s_serv.h"
#include "send.h"
#include "match.h"
#include "s_conf.h"
#include "s_log.h"
#include "parse.h"
#include "hook.h"
#include "modules.h"
#include "s_newconf.h"

static void do_whois(struct Client *client_p, struct Client *source_p, int parc, const char *parv[]);
static void single_whois(struct Client *source_p, struct Client *target_p, int operspy);

static int m_whois(struct Client *, struct Client *, int, const char **);
static int ms_whois(struct Client *, struct Client *, int, const char **);

struct Message whois_msgtab = {
	"WHOIS", 0, 0, 0, MFLG_SLOW,
	{mg_unreg, {m_whois, 2}, {ms_whois, 2}, mg_ignore, mg_ignore, {m_whois, 2}}
};

int doing_whois_hook;
int doing_whois_global_hook;

mapi_clist_av1 whois_clist[] = { &whois_msgtab, NULL };
mapi_hlist_av1 whois_hlist[] = {
	{ "doing_whois",	&doing_whois_hook },
	{ "doing_whois_global",	&doing_whois_global_hook },
	{ NULL, NULL }
};

DECLARE_MODULE_AV1(whois, NULL, NULL, whois_clist, whois_hlist, NULL, "$Revision: 19295 $");

/*
 * m_whois
 *      parv[0] = sender prefix
 *      parv[1] = nickname masklist
 */
static int
m_whois(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	static time_t last_used = 0;

	if(parc > 2)
	{
		if(EmptyString(parv[2]))
		{
			sendto_one(source_p, POP_QUEUE, form_str(ERR_NONICKNAMEGIVEN),
					me.name, source_p->name);
			return 0;
		}

		if(!IsOper(source_p))
		{
			/* seeing as this is going across servers, we should limit it */
			if((last_used + ConfigFileEntry.pace_wait_simple) > ircd_currenttime)
			{
				sendto_one(source_p, HOLD_QUEUE, form_str(RPL_LOAD2HI),
					   me.name, source_p->name, "WHOIS");
				sendto_one_numeric(source_p, POP_QUEUE, RPL_ENDOFWHOIS, 
						   form_str(RPL_ENDOFWHOIS), parv[1]);
				return 0;
			}
			else
				last_used = ircd_currenttime;
		}

		if(hunt_server(client_p, source_p, ":%s WHOIS %s :%s", 1, parc, parv) !=
		   HUNTED_ISME)
			return 0;

		parv[1] = parv[2];

	}
	do_whois(client_p, source_p, parc, parv);

	return 0;
}

/*
 * ms_whois
 *      parv[0] = sender prefix
 *      parv[1] = server to reply
 *      parv[2] = nickname to whois
 */
static int
ms_whois(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Client *target_p;

	/* note: early versions of ratbox allowed users to issue a remote
	 * whois with a blank parv[2], so we cannot treat it as a protocol
	 * violation. --anfl
	 */
	if(parc < 3 || EmptyString(parv[2]))
	{
		sendto_one(source_p, POP_QUEUE, form_str(ERR_NONICKNAMEGIVEN),
				me.name, source_p->name);
		return 0;
	}  
	

	/* check if parv[1] exists */
	if((target_p = find_client(parv[1])) == NULL)
	{
		sendto_one_numeric(source_p, POP_QUEUE, ERR_NOSUCHSERVER,
				   form_str(ERR_NOSUCHSERVER), 
				   IsDigit(parv[1][0]) ? "*" : parv[1]);
		return 0;
	}

	/* if parv[1] isnt my client, or me, someone else is supposed
	 * to be handling the request.. so send it to them 
	 */
	if(!MyClient(target_p) && !IsMe(target_p))
	{
		sendto_one(target_p, POP_QUEUE, ":%s WHOIS %s :%s", 
			   get_id(source_p, target_p), 
			   get_id(target_p, target_p), parv[2]);
		return 0;
	}

	/* ok, the target is either us, or a client on our server, so perform the whois
	 * but first, parv[1] == server to perform the whois on, parv[2] == person
	 * to whois, so make parv[1] = parv[2] so do_whois is ok -- fl_
	 */
	parv[1] = parv[2];
	do_whois(client_p, source_p, parc, parv);

	return 0;
}

/* do_whois
 *
 * inputs	- pointer to 
 * output	- 
 * side effects -
 */
static void
do_whois(struct Client * UNUSED(client_p), struct Client *source_p, int UNUSED(parc), const char *parv[])
{
	struct Client *target_p;
	char *nick;
	char *p = NULL;
	int operspy = 0;

	nick = LOCAL_COPY(parv[1]);
	if((p = strchr(parv[1], ',')))
		*p = '\0';

	if(IsOperSpy(source_p) && *nick == '!')
	{
		operspy = 1;
		nick++;
	}

	if(MyClient(source_p))
		target_p = find_named_person(nick);
	else
		target_p = find_person(nick);

	if(target_p != NULL)
	{
		if(operspy)
		{
			char buffer[BUFSIZE];

			ircd_snprintf(buffer, sizeof(buffer), "%s!%s@%s %s",
				target_p->name, target_p->username,
				target_p->host, target_p->user->server);
			report_operspy(source_p, "WHOIS", buffer);
		}

		single_whois(source_p, target_p, operspy);
	}
	else
		sendto_one_numeric(source_p, POP_QUEUE, ERR_NOSUCHNICK,
				   form_str(ERR_NOSUCHNICK), 
				   IsDigit(*nick) ? "*" : parv[1]);

	sendto_one_numeric(source_p, POP_QUEUE, RPL_ENDOFWHOIS, 
			   form_str(RPL_ENDOFWHOIS), parv[1]);
	return;
}

/*
 * single_whois()
 *
 * Inputs	- source_p client to report to
 *		- target_p client to report on
 * Output	- if found return 1
 * Side Effects	- do a single whois on given client
 * 		  writing results to source_p
 */
static void
single_whois(struct Client *source_p, struct Client *target_p, int operspy)
{
	char buf[BUFSIZE];
	dlink_node *ptr;
	struct Client *a2client_p;
	struct membership *msptr;
	struct Channel *chptr;
	int cur_len = 0;
	int mlen;
	char *t;
	int tlen;
	hook_data_client hdata;
	const char *name;
	char quest[] = "?";
	int visible;

	if(target_p->name)
		name = quest;
	else
		name = target_p->name;

	if(target_p->user == NULL)
	{
		s_assert(0);
		return;
	}

	a2client_p = target_p->servptr;

	sendto_one_numeric(source_p, HOLD_QUEUE, RPL_WHOISUSER, form_str(RPL_WHOISUSER),
			   target_p->name, target_p->username, 
			   target_p->host, target_p->info);

	cur_len = mlen = ircd_sprintf(buf, form_str(RPL_WHOISCHANNELS), 
				    get_id(&me, source_p), get_id(source_p, source_p), 
				    target_p->name);

	t = buf + mlen;

	DLINK_FOREACH(ptr, target_p->user->channel.head)
	{
		msptr = ptr->data;
		chptr = msptr->chptr;

		visible = ShowChannel(source_p, chptr);

		if(visible || operspy)
		{
			if((cur_len + strlen(chptr->chname) + 3) > (BUFSIZE - 5))
			{
				sendto_one_buffer(source_p, HOLD_QUEUE, buf);
				cur_len = mlen;
				t = buf + mlen;
			}

			tlen = ircd_sprintf(t, "%s%s%s ",
					visible ? "" : "!",
					find_channel_status(msptr, 1),
					chptr->chname);
			t += tlen;
			cur_len += tlen;
		}
	}

	if(cur_len > mlen)
		sendto_one_buffer(source_p, HOLD_QUEUE, buf);

	sendto_one_numeric(source_p, HOLD_QUEUE, RPL_WHOISSERVER, form_str(RPL_WHOISSERVER),
			   target_p->name, target_p->user->server,
			   a2client_p ? a2client_p->info : "*Not On This Net*");

	if(target_p->user->away)
		sendto_one_numeric(source_p, HOLD_QUEUE, RPL_AWAY, form_str(RPL_AWAY),
				   target_p->name, target_p->user->away);

	if(IsOper(target_p))
	{
		sendto_one_numeric(source_p, HOLD_QUEUE, RPL_WHOISOPERATOR, form_str(RPL_WHOISOPERATOR),
				   target_p->name,
				   IsAdmin(target_p) ? GlobalSetOptions.adminstring :
				    GlobalSetOptions.operstring);
	}

	if(MyClient(target_p))
	{
		if(ConfigFileEntry.use_whois_actually && show_ip(source_p, target_p))
			sendto_one_numeric(source_p, HOLD_QUEUE, RPL_WHOISACTUALLY,
					   form_str(RPL_WHOISACTUALLY),
					   target_p->name, target_p->sockhost);

		sendto_one_numeric(source_p, HOLD_QUEUE, RPL_WHOISIDLE, form_str(RPL_WHOISIDLE),
				   target_p->name, 
				   ircd_currenttime - target_p->localClient->last, 
				   target_p->localClient->firsttime);
	}
	else
	{
		if(ConfigFileEntry.use_whois_actually && show_ip(source_p, target_p) &&
		   !EmptyString(target_p->sockhost) && strcmp(target_p->sockhost, "0"))
		{
			sendto_one_numeric(source_p, HOLD_QUEUE, RPL_WHOISACTUALLY,
					   form_str(RPL_WHOISACTUALLY),
					   target_p->name, target_p->sockhost);
			
		}
	
	}

	send_pop_queue(source_p);
	hdata.client = source_p;
	hdata.target = target_p;

	/* doing_whois_hook must only be called for local clients,
	 * doing_whois_global_hook must only be called for local targets
	 */
	/* it is important that these are called *before* RPL_ENDOFWHOIS is
	 * sent, services compatibility code depends on it. --anfl
	 */
	if(MyClient(source_p))
		call_hook(doing_whois_hook, &hdata);
	else
		call_hook(doing_whois_global_hook, &hdata);

	return;
}

