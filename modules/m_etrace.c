/*
 *  ircd-ratbox: an advanced Internet Relay Chat Daemon(ircd).
 *  m_etrace.c: Gives local opers a trace output with added info.
 *
 *  Copyright (C) 2002-2003 Lee Hardy <lee@leeh.co.uk>
 *  Copyright (C) 2002-2005 ircd-ratbox development team
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *
 *  1.Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  2.Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  3.The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 *  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 *  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 *  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 *  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 *  IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 *  $Id$
 */

#include "stdinc.h"
#include "tools.h"
#include "struct.h"
#include "class.h"
#include "client.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "send.h"
#include "parse.h"
#include "modules.h"

static int mo_etrace(struct Client *, struct Client *, int, const char **);
static int me_etrace(struct Client *, struct Client *, int, const char **);

struct Message etrace_msgtab = {
	"ETRACE", 0, 0, 0, MFLG_SLOW,
	{mg_ignore, mg_not_oper, mg_ignore, mg_ignore, {me_etrace, 0}, {mo_etrace, 0}}
};

mapi_clist_av1 etrace_clist[] = { &etrace_msgtab, NULL };
DECLARE_MODULE_AV1(etrace, NULL, NULL, etrace_clist, NULL, NULL, "$Revision$");

static void do_etrace(struct Client *source_p, int ipv4, int ipv6);
static void do_etrace_full(struct Client *source_p);
static void do_single_etrace(struct Client *source_p, struct Client *target_p);

/*
 * m_etrace
 *      parv[0] = sender prefix
 *      parv[1] = options [or target]
 *	parv[2] = [target]
 */
static int
mo_etrace(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	if(parc > 1 && !EmptyString(parv[1]))
	{
		if(!irccmp(parv[1], "-full"))
			do_etrace_full(source_p);
#ifdef IPV6
		else if(!irccmp(parv[1], "-v6"))
			do_etrace(source_p, 0, 1);
		else if(!irccmp(parv[1], "-v4"))
			do_etrace(source_p, 1, 0);
#endif
		else
		{
			struct Client *target_p = find_named_person(parv[1]);

			if(target_p)
			{
				if(!MyClient(target_p))
					sendto_one(target_p, POP_QUEUE, ":%s ENCAP %s ETRACE %s",
						get_id(source_p, target_p),
						target_p->user->server,
						get_id(target_p, target_p));
				else
					do_single_etrace(source_p, target_p);
			}
			else
				sendto_one_numeric(source_p, POP_QUEUE, ERR_NOSUCHNICK,
						form_str(ERR_NOSUCHNICK), parv[1]);
		}
	}
	else
		do_etrace(source_p, 1, 1);

	return 0;
}

static int
me_etrace(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Client *target_p;

	if(!IsOper(source_p) || parc < 2 || EmptyString(parv[1]))
		return 0;

	/* we cant etrace remote clients.. we shouldnt even get sent them */
	if((target_p = find_person(parv[1])) && MyClient(target_p))
		do_single_etrace(source_p, target_p);

        sendto_one_numeric(source_p, POP_QUEUE, RPL_ENDOFTRACE, form_str(RPL_ENDOFTRACE), 
				target_p ? target_p->name : parv[1]);

	return 0;
}

static void
do_etrace(struct Client *source_p, int ipv4, int ipv6)
{
	struct Client *target_p;
	dlink_node *ptr;

	/* report all direct connections */
	DLINK_FOREACH(ptr, lclient_list.head)
	{
		target_p = ptr->data;

#ifdef IPV6
		if((!ipv4 && target_p->localClient->ip.ss_family == AF_INET) ||
		   (!ipv6 && target_p->localClient->ip.ss_family == AF_INET6))
			continue;
#endif

		sendto_one(source_p, POP_QUEUE, form_str(RPL_ETRACE),
			   me.name, source_p->name, 
			   IsOper(target_p) ? "Oper" : "User", 
			   get_client_class(target_p),
			   target_p->name, target_p->username, target_p->host,
#ifdef HIDE_SPOOF_IPS
			   IsIPSpoof(target_p) ? "255.255.255.255" :
#endif
			    target_p->sockhost, target_p->info);
	}

	sendto_one_numeric(source_p, POP_QUEUE, RPL_ENDOFTRACE, form_str(RPL_ENDOFTRACE), me.name);
}

static void
do_etrace_full(struct Client *source_p)
{
	dlink_node *ptr;

	DLINK_FOREACH(ptr, lclient_list.head)
	{
		do_single_etrace(source_p, ptr->data);
	}

	sendto_one_numeric(source_p, POP_QUEUE, RPL_ENDOFTRACE, form_str(RPL_ENDOFTRACE), me.name);
}

/*
 * do_single_etrace  - searches local clients and displays those matching
 *                     a pattern
 * input             - source client, target client
 * output	     - etrace results
 * side effects	     - etrace results are displayed
 */
static void
do_single_etrace(struct Client *source_p, struct Client *target_p)
{
#ifdef HIDE_SPOOF_IPS
	/* note, we hide fullcaps for spoofed users, as mirc can often
	 * advertise its internal ip address in the field --fl
	 */
	if(IsIPSpoof(target_p))
		sendto_one(source_p, POP_QUEUE, form_str(RPL_ETRACEFULL),
				me.name, source_p->name, 
				IsOper(target_p) ? "Oper" : "User",
				get_client_class(target_p),
				target_p->name, target_p->username, target_p->host, 
				"255.255.255.255", "<hidden> <hidden>", target_p->info);
	else
#endif
		sendto_one(source_p, POP_QUEUE, form_str(RPL_ETRACEFULL),
				me.name, source_p->name, 
				IsOper(target_p) ? "Oper" : "User",
				get_client_class(target_p),
				target_p->name, target_p->username, 
				target_p->host, target_p->sockhost,
				target_p->localClient->fullcaps, target_p->info);
}

