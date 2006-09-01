/*
 *  ircd-ratbox: an advanced Internet Relay Chat Daemon(ircd).
 *  m_stats.c: Sends the user statistics or config information.
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 *  USA
 *
 *  $Id$
 */

#include "stdinc.h"
#include "struct.h"
#include "class.h"		/* report_classes */
#include "client.h"		/* Client */
#include "channel.h"
#include "match.h"
#include "ircd.h"		/* me */
#include "listener.h"		/* show_ports */
#include "s_gline.h"
#include "hostmask.h"		/* report_mtrie_conf_links */
#include "numeric.h"		/* ERR_xxx */
#include "send.h"		/* sendto_one */
#include "s_conf.h"		/* ConfItem */
#include "s_serv.h"		/* hunt_server */
#include "s_stats.h"		/* tstats */
#include "s_user.h"		/* show_opers */
#include "parse.h"
#include "hook.h"
#include "modules.h"
#include "s_newconf.h"
#include "hash.h"
#include "dns.h"
#include "reject.h"
#include "whowas.h"
#include "scache.h"

static int m_stats (struct Client *, struct Client *, int, const char **);

struct Message stats_msgtab = {
	"STATS", 0, 0, 0, MFLG_SLOW,
	{mg_unreg, {m_stats, 2}, {m_stats, 3}, mg_ignore, mg_ignore, {m_stats, 2}}
};

int doing_stats_hook;
int doing_stats_p_hook;

mapi_clist_av1 stats_clist[] = { &stats_msgtab, NULL };
mapi_hlist_av1 stats_hlist[] = {
	{ "doing_stats",	&doing_stats_hook },
	{ "doing_stats_p",	&doing_stats_p_hook },
	{ NULL, NULL }
};

DECLARE_MODULE_AV1(stats, NULL, NULL, stats_clist, stats_hlist, NULL, "$Revision$");

static const char *Lformat = "%s %u %u %u %u %u :%u %u %s";

static void stats_l_list(struct Client *s, const char *, int, int, dlink_list *, char);
static void stats_l_client(struct Client *source_p, struct Client *target_p,
				char statchar);

static void stats_spy(struct Client *, char, const char *);
static void stats_p_spy(struct Client *);

/* Heres our struct for the stats table */
struct StatsStruct
{
	char letter;
	void (*handler) ();
	int need_oper;
	int need_admin;
};

static void stats_adns_servers (struct Client *);
static void stats_delay(struct Client *);
static void stats_hash(struct Client *);
static void stats_connect(struct Client *);
static void stats_tdeny(struct Client *);
static void stats_deny(struct Client *);
static void stats_exempt(struct Client *);
static void stats_events(struct Client *);
static void stats_glines(struct Client *);
static void stats_pending_glines(struct Client *);
static void stats_hubleaf(struct Client *);
static void stats_auth(struct Client *);
static void stats_tklines(struct Client *);
static void stats_klines(struct Client *);
static void stats_messages(struct Client *);
static void stats_oper(struct Client *);
static void stats_operedup(struct Client *);
static void stats_ports(struct Client *);
static void stats_tresv(struct Client *);
static void stats_resv(struct Client *);
static void stats_usage(struct Client *);
static void stats_tstats(struct Client *);
static void stats_uptime(struct Client *);
static void stats_shared(struct Client *);
static void stats_servers(struct Client *);
static void stats_tgecos(struct Client *);
static void stats_gecos(struct Client *);
static void stats_class(struct Client *);
static void stats_memory(struct Client *);
static void stats_servlinks(struct Client *);
static void stats_ltrace(struct Client *, int, const char **);
static void stats_ziplinks(struct Client *);
static void stats_comm(struct Client *);
/* This table contains the possible stats items, in order:
 * stats letter,  function to call, operonly? adminonly?
 * case only matters in the stats letter column.. -- fl_
 */
static struct StatsStruct stats_cmd_table[] = {
    /* letter     function        need_oper need_admin */
	{'a', stats_adns_servers,	1, 1, },
	{'A', stats_adns_servers,	1, 1, },
	{'b', stats_delay,		1, 1, },
	{'B', stats_hash,		1, 1, },
	{'c', stats_connect,		0, 0, },
	{'C', stats_connect,		0, 0, },
	{'d', stats_tdeny,		1, 0, },
	{'D', stats_deny,		1, 0, },
	{'e', stats_exempt,		1, 0, },
	{'E', stats_events,		1, 1, },
	{'f', stats_comm,		1, 1, },
	{'F', stats_comm,		1, 1, },
	{'g', stats_pending_glines,	1, 0, },
	{'G', stats_glines,		1, 0, },
	{'h', stats_hubleaf,		0, 0, },
	{'H', stats_hubleaf,		0, 0, },
	{'i', stats_auth,		0, 0, },
	{'I', stats_auth,		0, 0, },
	{'k', stats_tklines,		0, 0, },
	{'K', stats_klines,		0, 0, },
	{'l', stats_ltrace,		0, 0, },
	{'L', stats_ltrace,		0, 0, },
	{'m', stats_messages,		0, 0, },
	{'M', stats_messages,		0, 0, },
	{'o', stats_oper,		0, 0, },
	{'O', stats_oper,		0, 0, },
	{'p', stats_operedup,		0, 0, },
	{'P', stats_ports,		0, 0, },
	{'q', stats_tresv,		1, 0, },
	{'Q', stats_resv,		1, 0, },
	{'r', stats_usage,		1, 0, },
	{'R', stats_usage,		1, 0, },
	{'t', stats_tstats,		1, 0, },
	{'T', stats_tstats,		1, 0, },
	{'u', stats_uptime,		0, 0, },
	{'U', stats_shared,		1, 0, },
	{'v', stats_servers,		0, 0, },
	{'V', stats_servers,		0, 0, },
	{'x', stats_tgecos,		1, 0, },
	{'X', stats_gecos,		1, 0, },
	{'y', stats_class,		0, 0, },
	{'Y', stats_class,		0, 0, },
	{'z', stats_memory,		1, 0, },
	{'Z', stats_ziplinks,		1, 0, },
	{'?', stats_servlinks,		0, 0, },
	{(char) 0, (void (*)()) 0, 	0, 0, }
};

/*
 * m_stats by fl_
 *      parv[0] = sender prefix
 *      parv[1] = stat letter/command
 *      parv[2] = (if present) server/mask in stats L, or target
 *
 * This will search the tables for the appropriate stats letter,
 * if found execute it.  
 */
static int
m_stats(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	static time_t last_used = 0;
	int i;
	char statchar;

	statchar = parv[1][0];

	if(MyClient(source_p) && !IsOper(source_p))
	{
		/* Check the user is actually allowed to do /stats, and isnt flooding */
		if((last_used + ConfigFileEntry.pace_wait) > ircd_currenttime)
		{
			/* safe enough to give this on a local connect only */
			sendto_one(source_p, HOLD_QUEUE, form_str(RPL_LOAD2HI),
				   me.name, source_p->name, "STATS");
			sendto_one_numeric(source_p, POP_QUEUE, RPL_ENDOFSTATS, 
					   form_str(RPL_ENDOFSTATS), statchar);
			return 0;
		}
		else
			last_used = ircd_currenttime;
	}

	if(hunt_server (client_p, source_p, ":%s STATS %s :%s", 2, parc, parv) != HUNTED_ISME)
		return 0;

	if((statchar != 'L') && (statchar != 'l'))
		stats_spy(source_p, statchar, NULL);

	for (i = 0; stats_cmd_table[i].handler; i++)
	{
		if(stats_cmd_table[i].letter == statchar)
		{
			/* The stats table says what privs are needed, so check --fl_ */
			/* Called for remote clients and for local opers, so check need_admin
			 * and need_oper
			 */
			if((stats_cmd_table[i].need_admin && !IsOperAdmin (source_p)) ||
			   (stats_cmd_table[i].need_oper && !IsOper (source_p)))
			{
				sendto_one_numeric(source_p, POP_QUEUE, ERR_NOPRIVILEGES,
						   form_str (ERR_NOPRIVILEGES));
				break;
			}

			/* Blah, stats L needs the parameters, none of the others do.. */
			if(statchar == 'L' || statchar == 'l')
				stats_cmd_table[i].handler (source_p, parc, parv);
			else
				stats_cmd_table[i].handler (source_p);

		}
	}

	/* Send the end of stats notice, and the stats_spy */
	sendto_one_numeric(source_p, POP_QUEUE, RPL_ENDOFSTATS, 
			   form_str(RPL_ENDOFSTATS), statchar);

	return 0;
}

static void
stats_adns_servers (struct Client *source_p)
{
	(void)source_p;
/*	report_adns_servers (source_p); */
}

static void
stats_delay(struct Client *source_p)
{
	struct nd_entry *nd;
	dlink_node *ptr;
	int i;

	HASH_WALK(i, U_MAX, ptr, ndTable)
	{
		nd = ptr->data;
		sendto_one_notice(source_p, POP_QUEUE, "Delaying: %s for %ld",
				nd->name, (long) nd->expire);
	}
	HASH_WALK_END
}

static void
stats_hash(struct Client *source_p)
{
	hash_stats(source_p);
}

static void
stats_connect(struct Client *source_p)
{
	static char buf[5];
	struct server_conf *server_p;
	char *s;
	dlink_node *ptr;

	if((ConfigFileEntry.stats_c_oper_only || 
	    (ConfigServerHide.flatten_links && !IsExemptShide(source_p))) &&
	    !IsOper(source_p))
	{
		sendto_one_numeric(source_p, POP_QUEUE, ERR_NOPRIVILEGES,
				   form_str(ERR_NOPRIVILEGES));
		return;
	}

	DLINK_FOREACH(ptr, server_conf_list.head)
	{
		server_p = ptr->data;

		if(ServerConfIllegal(server_p))
			continue;

		buf[0] = '\0';
		s = buf;

		if(IsOper(source_p))
		{
			if(ServerConfAutoconn(server_p))
				*s++ = 'A';
			if(ServerConfTb(server_p))
				*s++ = 'T';
			if(ServerConfCompressed(server_p))
				*s++ = 'Z';
		}

		if(!buf[0])
			*s++ = '*';

		*s = '\0';

		sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSCLINE, 
				form_str(RPL_STATSCLINE),
				"*@127.0.0.1", buf, server_p->name,
				server_p->port, server_p->class_name);
	}
}

/* stats_tdeny()
 *
 * input	- client to report to
 * output	- none
 * side effects - client is given temp dline list.
 */
static void
stats_tdeny (struct Client *source_p)
{
	report_tdlines(source_p);
}

/* stats_deny()
 *
 * input	- client to report to
 * output	- none
 * side effects - client is given dline list.
 */
static void
stats_deny (struct Client *source_p)
{
	report_dlines(source_p);
}


/* stats_exempt()
 *
 * input	- client to report to
 * output	- none
 * side effects - client is given list of exempt blocks
 */
static void
stats_exempt(struct Client *source_p)
{
	if(ConfigFileEntry.stats_e_disabled)
	{
		sendto_one_numeric(source_p, POP_QUEUE, ERR_NOPRIVILEGES,
				   form_str (ERR_NOPRIVILEGES));
		return;
	}

	report_elines(source_p);
}

static void
stats_events_cb(char *str, void *ptr)
{
	sendto_one_numeric(ptr, HOLD_QUEUE, RPL_STATSDEBUG, "E :%s", str);
}

static void
stats_events (struct Client *source_p)
{
	ircd_dump_events(stats_events_cb, source_p);
	send_pop_queue(source_p);
}

/* stats_pending_glines()
 *
 * input	- client pointer
 * output	- none
 * side effects - client is shown list of pending glines
 */
static void
stats_pending_glines (struct Client *source_p)
{
	if(ConfigFileEntry.glines)
	{
		dlink_node *pending_node;
		struct gline_pending *glp_ptr;
		char timebuffer[MAX_DATE_STRING];
		struct tm *tmptr;

		DLINK_FOREACH (pending_node, pending_glines.head)
		{
			glp_ptr = pending_node->data;

			tmptr = localtime (&glp_ptr->time_request1);
			strftime (timebuffer, MAX_DATE_STRING, "%Y/%m/%d %H:%M:%S", tmptr);

			sendto_one_notice(source_p, POP_QUEUE,
				    ":1) %s!%s@%s on %s requested gline at %s for %s@%s [%s]",
				    glp_ptr->oper_nick1,
				    glp_ptr->oper_user1, glp_ptr->oper_host1,
				    glp_ptr->oper_server1, timebuffer,
				    glp_ptr->user, glp_ptr->host, glp_ptr->reason1);

			if(glp_ptr->oper_nick2[0])
			{
				tmptr = localtime (&glp_ptr->time_request2);
				strftime (timebuffer, MAX_DATE_STRING, "%Y/%m/%d %H:%M:%S", tmptr);
				sendto_one_notice(source_p, POP_QUEUE,
					    ":2) %s!%s@%s on %s requested gline at %s for %s@%s [%s]",
					    glp_ptr->oper_nick2,
					    glp_ptr->oper_user2, glp_ptr->oper_host2,
					    glp_ptr->oper_server2, timebuffer,
					    glp_ptr->user, glp_ptr->host, glp_ptr->reason2);
			}
		}

		if(ircd_dlink_list_length (&pending_glines) > 0)
			sendto_one_notice(source_p, POP_QUEUE, ":End of Pending G-lines");
	}
	else
		sendto_one_notice(source_p, POP_QUEUE, ":This server does not support G-Lines");

}

/* stats_glines()
 *
 * input	- client pointer
 * output	- none
 * side effects - client is shown list of glines
 */
static void
stats_glines (struct Client *source_p)
{
	if(ConfigFileEntry.glines)
	{
		dlink_node *gline_node;
		struct ConfItem *kill_ptr;

		DLINK_FOREACH_PREV (gline_node, glines.tail)
		{
			kill_ptr = gline_node->data;

			sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSKLINE, 
					   form_str(RPL_STATSKLINE), 'G',
					    kill_ptr->host ? kill_ptr->host : "*",
					    kill_ptr->user ? kill_ptr->user : "*",
					    kill_ptr->passwd ? kill_ptr->passwd : "No Reason",
					    kill_ptr->spasswd ? "|" : "",
					    kill_ptr->spasswd ? kill_ptr->spasswd : "");
		}
	}
	else
		sendto_one_notice(source_p, POP_QUEUE, ":This server does not support G-Lines");
}


static void
stats_hubleaf(struct Client *source_p)
{
	struct remote_conf *hub_p;
	dlink_node *ptr;

	if((ConfigFileEntry.stats_h_oper_only || 
	    (ConfigServerHide.flatten_links && !IsExemptShide(source_p))) &&
	    !IsOper(source_p))
	{
		sendto_one_numeric(source_p, POP_QUEUE, ERR_NOPRIVILEGES,
				   form_str (ERR_NOPRIVILEGES));
		return;
	}

	DLINK_FOREACH(ptr, hubleaf_conf_list.head)
	{
		hub_p = ptr->data;

		if(hub_p->flags & CONF_HUB)
			sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSHLINE,
					form_str(RPL_STATSHLINE),
					hub_p->host, hub_p->server);
		else
			sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSLLINE,
					form_str(RPL_STATSLLINE),
					hub_p->host, hub_p->server);
	}
}


static void
stats_auth (struct Client *source_p)
{
        char *name, *host, *pass, *user, *classname;
	struct AddressRec *arec;
	struct ConfItem *aconf;
	int i, port;

	/* Oper only, if unopered, return ERR_NOPRIVS */
	if((ConfigFileEntry.stats_i_oper_only == 2) && !IsOper (source_p))
		sendto_one_numeric(source_p, POP_QUEUE, ERR_NOPRIVILEGES,
				   form_str (ERR_NOPRIVILEGES));

	/* If unopered, Only return matching auth blocks */
	else if((ConfigFileEntry.stats_i_oper_only == 1) && !IsOper (source_p))
	{
		if(MyConnect (source_p))
			aconf = find_auth(source_p->host, source_p->sockhost,
					(struct sockaddr *)&source_p->localClient->ip,
					source_p->localClient->ip.ss_family,
					source_p->username);
		else
			aconf = find_auth(source_p->host, NULL, NULL,
					0, source_p->username);

		if(aconf == NULL)
			return;

		get_printable_conf(aconf, &name, &host, &pass, &user, &port, &classname);

		sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSILINE, form_str(RPL_STATSILINE),
				   name, show_iline_prefix(source_p, aconf, user),
				   host, port, classname);
	}

	/* Theyre opered, or allowed to see all auth blocks */
	else {
		HOSTHASH_WALK(i, arec)
		{
			if((arec->type & ~CONF_SKIPUSER) == CONF_CLIENT)
			{
				aconf = arec->aconf;
				if(!MyOper(source_p) && IsConfDoSpoofIp(aconf))
					continue;
				get_printable_conf(aconf, &name, &host, &pass, &user, &port, &classname);

				sendto_one_numeric(source_p, HOLD_QUEUE, RPL_STATSILINE, 
						   form_str(RPL_STATSILINE), name, 
						   show_iline_prefix(source_p, aconf, user),
						   show_ip_conf(aconf, source_p) ? host : "255.255.255.255",
						   port, classname);
			}
		}
		HOSTHASH_WALK_END;
		send_pop_queue(source_p);                                                                                                                                                                                     				
	}
}


static void
stats_tklines(struct Client *source_p)
{
	/* Oper only, if unopered, return ERR_NOPRIVS */
	if((ConfigFileEntry.stats_k_oper_only == 2) && !IsOper (source_p))
		sendto_one_numeric(source_p, POP_QUEUE, ERR_NOPRIVILEGES,
				   form_str (ERR_NOPRIVILEGES));

	/* If unopered, Only return matching klines */
	else if((ConfigFileEntry.stats_k_oper_only == 1) && !IsOper (source_p))
	{
		struct ConfItem *aconf;
		char *host, *pass, *user, *oper_reason;

		if(MyConnect (source_p))
			aconf = find_conf_by_address (source_p->host, source_p->sockhost,
						      (struct sockaddr *)&source_p->localClient->ip,
						      CONF_KILL,
						      source_p->localClient->ip.ss_family,
						      source_p->username);
		else
			aconf = find_conf_by_address (source_p->host, NULL, NULL, CONF_KILL,
						      0, source_p->username);

		if(aconf == NULL)
			return;

		/* dont report a permanent kline as a tkline */
		if((aconf->flags & CONF_FLAGS_TEMPORARY) == 0)
			return;

		get_printable_kline(source_p, aconf, &host, &pass, &user, &oper_reason);

		sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSKLINE, 
				   form_str(RPL_STATSKLINE), 'k',
				   user, pass, oper_reason ? "|" : "",
				   oper_reason ? oper_reason : "");
	}
	/* Theyre opered, or allowed to see all klines */
	else
	{
		struct ConfItem *aconf;
		dlink_node *ptr;
		int i;
		char *user, *host, *pass, *oper_reason;

		for(i = 0; i < LAST_TEMP_TYPE; i++)
		{
			DLINK_FOREACH(ptr, temp_klines[i].head)
			{
				aconf = ptr->data;

				get_printable_kline(source_p, aconf, &host, &pass, 
							&user, &oper_reason);

				sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSKLINE,
						   form_str(RPL_STATSKLINE),
						   'k', host, user, pass,
						   oper_reason ? "|" : "",
						   oper_reason ? oper_reason : "");
			}
		}
	}
}

static void
stats_klines(struct Client *source_p)
{
	struct ConfItem *aconf;
	char *host, *pass, *user, *oper_reason;
	struct AddressRec *arec;
	int i;
	
	/* Oper only, if unopered, return ERR_NOPRIVS */
	if((ConfigFileEntry.stats_k_oper_only == 2) && !IsOper (source_p))
		sendto_one_numeric(source_p, POP_QUEUE, ERR_NOPRIVILEGES,
				   form_str (ERR_NOPRIVILEGES));

	/* If unopered, Only return matching klines */
	else if((ConfigFileEntry.stats_k_oper_only == 1) && !IsOper (source_p))
	{

		/* search for a kline */
		if(MyConnect (source_p))
			aconf = find_conf_by_address (source_p->host, source_p->sockhost,
						      (struct sockaddr *)&source_p->localClient->ip,
						      CONF_KILL,
						      source_p->localClient->ip.ss_family,
						      source_p->username);
		else
			aconf = find_conf_by_address (source_p->host, NULL, NULL, CONF_KILL,
						      0, source_p->username);

		if(aconf == NULL)
			return;

		/* dont report a tkline as a kline */
		if(aconf->flags & CONF_FLAGS_TEMPORARY)
			return;

		get_printable_kline(source_p, aconf, &host, &pass, &user, &oper_reason);

		sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSKLINE, form_str(RPL_STATSKLINE),
				   'K', host, user, pass, oper_reason ? "|" : "",
				   oper_reason ? oper_reason : "");
	}
	/* Theyre opered, or allowed to see all klines */
	else {
		HOSTHASH_WALK(i, arec)
		{
			if((arec->type & ~CONF_SKIPUSER) == CONF_KILL)
			{
				aconf = arec->aconf;
				
				if(aconf->flags & CONF_FLAGS_TEMPORARY) /* skip temps */
					continue;

				get_printable_kline(source_p, aconf, &host, &pass, &user, &oper_reason);

				sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSKLINE, form_str(RPL_STATSKLINE),
						   'K', host, user, pass, oper_reason ? "|" : "",
						   oper_reason ? oper_reason : "");

			}
		}
		HOSTHASH_WALK_END;
		send_pop_queue(source_p);                                                                                                                                                                                     				
	}
}

static void
stats_messages(struct Client *source_p)
{
	int i;
	struct MessageHash *ptr;

	for (i = 0; i < MAX_MSG_HASH; i++)
	{
		for (ptr = msg_hash_table[i]; ptr; ptr = ptr->next)
		{
			s_assert(ptr->msg != NULL);
			s_assert(ptr->cmd != NULL);

			sendto_one_numeric(source_p, HOLD_QUEUE, RPL_STATSCOMMANDS, 
					   form_str(RPL_STATSCOMMANDS),
					   ptr->cmd, ptr->msg->count, 
					   ptr->msg->bytes, ptr->msg->rcount);
		}
	}
	send_pop_queue(source_p);
}

static void
stats_oper(struct Client *source_p)
{
	struct oper_conf *oper_p;
	dlink_node *ptr;

	if(!IsOper(source_p) && ConfigFileEntry.stats_o_oper_only)
	{
		sendto_one_numeric(source_p, POP_QUEUE, ERR_NOPRIVILEGES,
				   form_str (ERR_NOPRIVILEGES));
		return;
	}

	DLINK_FOREACH(ptr, oper_conf_list.head)
	{
		oper_p = ptr->data;
		
		sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSOLINE, 
				form_str(RPL_STATSOLINE),
				oper_p->username, oper_p->host, oper_p->name,
				IsOper(source_p) ? get_oper_privs(oper_p->flags) : "0", "-1");
	}
}


/* stats_operedup()
 *
 * input	- client pointer
 * output	- none
 * side effects - client is shown a list of active opers
 */
static void
stats_operedup (struct Client *source_p)
{
	struct Client *target_p;
	dlink_node *oper_ptr;
	unsigned int count = 0;

	DLINK_FOREACH (oper_ptr, oper_list.head)
	{
		target_p = oper_ptr->data;

		if(IsOperInvis(target_p) && !IsOper(source_p))
			continue;

		count++;

		if(MyClient(source_p) && IsOper(source_p))
		{
			sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
					   "p :[%c][%s] %s (%s@%s) Idle: %d",
					   IsAdmin (target_p) ? 'A' : 'O',
					   get_oper_privs(target_p->operflags),
					   target_p->name, target_p->username, target_p->host,
					   (int) (ircd_currenttime - target_p->localClient->last));
		}
		else
		{
			sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
					   "p :[%c] %s (%s@%s) Idle: %d",
					   IsAdmin (target_p) ? 'A' : 'O',
					   target_p->name, target_p->username, target_p->host,
					   (int) (ircd_currenttime - target_p->localClient->last));
		}
	}

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "p :%u OPER(s)", count);

	stats_p_spy(source_p);
}

static void
stats_ports (struct Client *source_p)
{
	if(!IsOper (source_p) && ConfigFileEntry.stats_P_oper_only)
		sendto_one_numeric(source_p, POP_QUEUE, ERR_NOPRIVILEGES,
				   form_str (ERR_NOPRIVILEGES));
	else
		show_ports (source_p);
}

static void
stats_tresv(struct Client *source_p)
{
	struct ConfItem *aconf;
	dlink_node *ptr;
	int i;

	DLINK_FOREACH(ptr, resv_conf_list.head)
	{
		aconf = ptr->data;
		if(aconf->flags & CONF_FLAGS_TEMPORARY)
			sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSQLINE, 
					form_str(RPL_STATSQLINE),
					'q', aconf->port, aconf->host, aconf->passwd);
	}

	HASH_WALK(i, R_MAX, ptr, resvTable)
	{
		aconf = ptr->data;
		if(aconf->flags & CONF_FLAGS_TEMPORARY)
			sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSQLINE, 
					form_str(RPL_STATSQLINE),
					'q', aconf->port, aconf->host, aconf->passwd);
	}
	HASH_WALK_END
}


static void
stats_resv(struct Client *source_p)
{
	struct ConfItem *aconf;
	dlink_node *ptr;
	int i;

	DLINK_FOREACH(ptr, resv_conf_list.head)
	{
		aconf = ptr->data;
		if((aconf->flags & CONF_FLAGS_TEMPORARY) == 0)
			sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSQLINE, 
					form_str(RPL_STATSQLINE),
					'Q', aconf->port, aconf->host, aconf->passwd);
	}

	HASH_WALK(i, R_MAX, ptr, resvTable)
	{
		aconf = ptr->data;
		if((aconf->flags & CONF_FLAGS_TEMPORARY) == 0)
			sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSQLINE, 
					form_str(RPL_STATSQLINE),
					'Q', aconf->port, aconf->host, aconf->passwd);
	}
	HASH_WALK_END
}

static void
stats_usage (struct Client *source_p)
{
#ifndef _WIN32
	struct rusage rus;
	time_t secs;
	time_t rup;
#ifdef  hz
# define hzz hz
#else
# ifdef HZ
#  define hzz HZ
# else
	int hzz = 1;
# endif
#endif

	if(getrusage(RUSAGE_SELF, &rus) == -1)
	{
		sendto_one_notice(source_p, POP_QUEUE, ":Getruseage error: %s.",
				  strerror(errno));
		return;
	}
	secs = rus.ru_utime.tv_sec + rus.ru_stime.tv_sec;
	if(0 == secs)
		secs = 1;

	rup = (ircd_currenttime - startup_time) * hzz;
	if(0 == rup)
		rup = 1;
  
	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "R :CPU Secs %d:%d User %d:%d System %d:%d",
			   (int) (secs / 60), (int) (secs % 60),
			   (int) (rus.ru_utime.tv_sec / 60),
			   (int) (rus.ru_utime.tv_sec % 60),
			   (int) (rus.ru_stime.tv_sec / 60), 
			   (int) (rus.ru_stime.tv_sec % 60));
	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "R :RSS %ld ShMem %ld Data %ld Stack %ld",
			   rus.ru_maxrss, (rus.ru_ixrss / rup), 
			   (rus.ru_idrss / rup), (rus.ru_isrss / rup));
	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "R :Swaps %d Reclaims %d Faults %d",
			   (int) rus.ru_nswap, (int) rus.ru_minflt, (int) rus.ru_majflt);
	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "R :Block in %d out %d",
			   (int) rus.ru_inblock, (int) rus.ru_oublock);
	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "R :Msg Rcv %d Send %d",
			   (int) rus.ru_msgrcv, (int) rus.ru_msgsnd);
	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "R :Signals %d Context Vol. %d Invol %d",
			   (int) rus.ru_nsignals, (int) rus.ru_nvcsw, 
			   (int) rus.ru_nivcsw);
#endif
}

static void
stats_tstats(struct Client *source_p)
{
	struct Client *target_p;
	struct ServerStatistics sp;
	dlink_node *ptr;

	memcpy(&sp, &ServerStats, sizeof(struct ServerStatistics));

	DLINK_FOREACH(ptr, serv_list.head)
	{
		target_p = ptr->data;

		sp.is_sbs += target_p->localClient->sendB;
		sp.is_sbr += target_p->localClient->receiveB;
		sp.is_sks += target_p->localClient->sendK;
		sp.is_skr += target_p->localClient->receiveK;
		sp.is_sti += ircd_currenttime - target_p->localClient->firsttime;
		sp.is_sv++;
		if(sp.is_sbs > 1023)
		{
			sp.is_sks += (sp.is_sbs >> 10);
			sp.is_sbs &= 0x3ff;
		}
		if(sp.is_sbr > 1023)
		{
			sp.is_skr += (sp.is_sbr >> 10);
			sp.is_sbr &= 0x3ff;
		}
	}

	DLINK_FOREACH(ptr, lclient_list.head)
	{
		target_p = ptr->data;

		sp.is_cbs += target_p->localClient->sendB;
		sp.is_cbr += target_p->localClient->receiveB;
		sp.is_cks += target_p->localClient->sendK;
		sp.is_ckr += target_p->localClient->receiveK;
		sp.is_cti += ircd_currenttime - target_p->localClient->firsttime;
		sp.is_cl++;
		if(sp.is_cbs > 1023)
		{
			sp.is_cks += (sp.is_cbs >> 10);
			sp.is_cbs &= 0x3ff;
		}
		if(sp.is_cbr > 1023)
		{
			sp.is_ckr += (sp.is_cbr >> 10);
			sp.is_cbr &= 0x3ff;
		}

	}

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
				"T :accepts %u refused %u", 
				sp.is_ac, sp.is_ref);
	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
				"T :rejected %u delaying %lu", 
				sp.is_rej, ircd_dlink_list_length(&delay_exit));
	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
				"T :nicks being delayed %lu", get_nd_count());
	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
				"T :unknown commands %u prefixes %u",
				sp.is_unco, sp.is_unpf);
	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
				"T :nick collisions %u unknown closes %u",
				sp.is_kill, sp.is_ni);
	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
				"T :wrong direction %u empty %u", 
				sp.is_wrdi, sp.is_empt);
	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
				"T :numerics seen %u", 
				sp.is_num);
	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
				"T :auth successes %u fails %u",
				sp.is_asuc, sp.is_abad);
	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG, "T :Client Server");
	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
				"T :connected %u %u",
				sp.is_cl, sp.is_sv);
	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
				"T :bytes sent %d.%uK %d.%uK",
				(int) sp.is_cks, sp.is_cbs, 
				(int) sp.is_sks, sp.is_sbs);
	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
				"T :bytes recv %d.%uK %d.%uK",
				(int) sp.is_ckr, sp.is_cbr, 
				(int) sp.is_skr, sp.is_sbr);
	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
				"T :time connected %d %d",
				(int) sp.is_cti, (int) sp.is_sti);
}

static void
stats_uptime (struct Client *source_p)
{
	time_t now;

	now = ircd_currenttime - startup_time;
	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSUPTIME, 
			   form_str (RPL_STATSUPTIME),
			   now / 86400, (now / 3600) % 24, 
			   (now / 60) % 60, now % 60);
	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSCONN,
			   form_str (RPL_STATSCONN),
			   MaxConnectionCount, MaxClientCount, 
			   Count.totalrestartcount);
}

struct shared_flags
{
	int flag;
	char letter;
};
static struct shared_flags shared_flagtable[] =
{
	{ SHARED_PKLINE,	'K' },
	{ SHARED_TKLINE,	'k' },
	{ SHARED_UNKLINE,	'U' },
	{ SHARED_PXLINE,	'X' },
	{ SHARED_TXLINE,	'x' },
	{ SHARED_UNXLINE,	'Y' },
	{ SHARED_PRESV,		'Q' },
	{ SHARED_TRESV,		'q' },
	{ SHARED_UNRESV,	'R' },
	{ SHARED_LOCOPS,	'L' },
	{ 0,			'\0'}
};


static void
stats_shared (struct Client *source_p)
{
	struct remote_conf *shared_p;
	dlink_node *ptr;
	char buf[15];
	char *p;
	int i;

	DLINK_FOREACH(ptr, shared_conf_list.head)
	{
		shared_p = ptr->data;

		p = buf;

		*p++ = 'c';

		for(i = 0; shared_flagtable[i].flag != 0; i++)
		{
			if(shared_p->flags & shared_flagtable[i].flag)
				*p++ = shared_flagtable[i].letter;
		}

		*p = '\0';

		sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSULINE, 
					form_str(RPL_STATSULINE),
					shared_p->server, shared_p->username,
					shared_p->host, buf);
	}

	DLINK_FOREACH(ptr, cluster_conf_list.head)
	{
		shared_p = ptr->data;

		p = buf;

		*p++ = 'C';

		for(i = 0; shared_flagtable[i].flag != 0; i++)
		{
			if(shared_p->flags & shared_flagtable[i].flag)
				*p++ = shared_flagtable[i].letter;
		}

		*p = '\0';

		sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSULINE, 
					form_str(RPL_STATSULINE),
					shared_p->server, "*", "*", buf);
	}
}

/* stats_servers()
 *
 * input	- client pointer
 * output	- none
 * side effects - client is shown lists of who connected servers
 */
static void
stats_servers (struct Client *source_p)
{
	struct Client *target_p;
	dlink_node *ptr;
	time_t seconds;
	int days, hours, minutes;
	int j = 0;

	if(ConfigServerHide.flatten_links && !IsOper(source_p) &&
	   !IsExemptShide(source_p))
	{
		sendto_one_numeric(source_p, POP_QUEUE, ERR_NOPRIVILEGES,
				   form_str (ERR_NOPRIVILEGES));
		return;
	}

	DLINK_FOREACH (ptr, serv_list.head)
	{
		target_p = ptr->data;

		j++;
		seconds = ircd_currenttime - target_p->localClient->firsttime;

		days = (int) (seconds / 86400);
		seconds %= 86400;
		hours = (int) (seconds / 3600);
		seconds %= 3600;
		minutes = (int) (seconds / 60);
		seconds %= 60;

		sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
				   "V :%s (%s!*@*) Idle: %d SendQ: %d "
				   "Connected: %d day%s, %d:%02d:%02d",
				   target_p->name,
				   (target_p->serv->by[0] ? target_p->serv->by : "Remote."),
				   (int) (ircd_currenttime - target_p->localClient->lasttime),
				   (int) ircd_linebuf_len (&target_p->localClient->buf_sendq),
				   days, (days == 1) ? "" : "s", hours, minutes, 
				   (int) seconds);
	}

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "V :%d Server(s)", j);
}

static void
stats_tgecos(struct Client *source_p)
{
	struct ConfItem *aconf;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, xline_conf_list.head)
	{
		aconf = ptr->data;

		if(aconf->flags & CONF_FLAGS_TEMPORARY)
			sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSXLINE,
					form_str(RPL_STATSXLINE),
					'x', aconf->port, aconf->host,
					aconf->passwd);
	}
}

static void
stats_gecos(struct Client *source_p)
{
	struct ConfItem *aconf;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, xline_conf_list.head)
	{
		aconf = ptr->data;

		if((aconf->flags & CONF_FLAGS_TEMPORARY) == 0)
			sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSXLINE,
					form_str(RPL_STATSXLINE),
					'X', aconf->port, aconf->host, 
					aconf->passwd);
	}
}

static void
stats_class(struct Client *source_p)
{
	struct Class *cltmp;
	dlink_node *ptr;

	if(ConfigFileEntry.stats_y_oper_only && !IsOper(source_p)) 
	{
		sendto_one_numeric(source_p, POP_QUEUE, ERR_NOPRIVILEGES,
				   form_str (ERR_NOPRIVILEGES));
		return;
	}

	DLINK_FOREACH(ptr, class_list.head)
	{
		cltmp = ptr->data;

		sendto_one_numeric(source_p, HOLD_QUEUE, RPL_STATSYLINE, 
				form_str(RPL_STATSYLINE),
				ClassName(cltmp), PingFreq(cltmp), 
				ConFreq(cltmp), MaxUsers(cltmp), 
				MaxSendq(cltmp), 
				MaxLocal(cltmp), MaxIdent(cltmp),
				MaxGlobal(cltmp), MaxIdent(cltmp),
				CurrUsers(cltmp));
	}

	/* also output the default class */
	sendto_one_numeric(source_p, HOLD_QUEUE, RPL_STATSYLINE, form_str(RPL_STATSYLINE),
			ClassName(default_class), PingFreq(default_class), 
			ConFreq(default_class), MaxUsers(default_class), 
			MaxSendq(default_class),
			MaxLocal(default_class), MaxIdent(default_class),
			MaxGlobal(default_class), MaxIdent(default_class),
			CurrUsers(default_class));
	send_pop_queue(source_p);
}

static void
stats_memory (struct Client *source_p)
{
	struct Client *target_p;
	struct Channel *chptr;
	struct Ban *actualBan;
	dlink_node *dlink;
	dlink_node *ptr;
	int channel_count = 0;
	int local_client_conf_count = 0;	/* local client conf links */
	int users_counted = 0;	/* user structs */

	int channel_users = 0;
	int channel_invites = 0;
	int channel_bans = 0;
	int channel_except = 0;
	int channel_invex = 0;

	size_t wwu = 0;		/* whowas users */
	int class_count = 0;	/* classes */
	int conf_count = 0;	/* conf lines */
	int users_invited_count = 0;	/* users invited */
	int user_channels = 0;	/* users in channels */
	int aways_counted = 0;
	size_t number_servers_cached;	/* number of servers cached by scache */

	size_t channel_memory = 0;
	size_t channel_ban_memory = 0;
	size_t channel_except_memory = 0;
	size_t channel_invex_memory = 0;

	size_t away_memory = 0;	/* memory used by aways */
	size_t wwm = 0;		/* whowas array memory used */
	size_t conf_memory = 0;	/* memory used by conf lines */
	size_t mem_servers_cached;	/* memory used by scache */

	size_t ircd_linebuf_count = 0;
	size_t ircd_linebuf_memory_used = 0;

	size_t total_channel_memory = 0;
	size_t totww = 0;

	size_t local_client_count = 0;
	size_t local_client_memory_used = 0;

	size_t remote_client_count = 0;
	size_t remote_client_memory_used = 0;

	size_t total_memory = 0;

	count_whowas_memory(&wwu, &wwm);

	DLINK_FOREACH(ptr, global_client_list.head)
	{
		target_p = ptr->data;
		if(MyConnect(target_p))
		{
			local_client_conf_count++;
		}

		if(target_p->user)
		{
			users_counted++;
			if(MyConnect(target_p))
				users_invited_count += ircd_dlink_list_length(&target_p->localClient->invited);
			user_channels += ircd_dlink_list_length(&target_p->user->channel);
			if(target_p->user->away)
			{
				aways_counted++;
				away_memory += (strlen(target_p->user->away) + 1);
			}
		}
	}

	/* Count up all channels, ban lists, except lists, Invex lists */
	DLINK_FOREACH(ptr, global_channel_list.head)
	{
		chptr = ptr->data;
		channel_count++;
		channel_memory += (strlen(chptr->chname) + sizeof(struct Channel));

		channel_users += ircd_dlink_list_length(&chptr->members);
		channel_invites += ircd_dlink_list_length(&chptr->invites);

		DLINK_FOREACH(dlink, chptr->banlist.head)
		{
			actualBan = dlink->data;
			channel_bans++;

			channel_ban_memory += sizeof(dlink_node) + sizeof(struct Ban);
		}

		DLINK_FOREACH(dlink, chptr->exceptlist.head)
		{
			actualBan = dlink->data;
			channel_except++;

			channel_except_memory += (sizeof(dlink_node) + sizeof(struct Ban));
		}

		DLINK_FOREACH(dlink, chptr->invexlist.head)
		{
			actualBan = dlink->data;
			channel_invex++;

			channel_invex_memory += (sizeof(dlink_node) + sizeof(struct Ban));
		}
	}

	/* count up all classes */

	class_count = ircd_dlink_list_length(&class_list) + 1;

	ircd_count_ircd_linebuf_memory(&ircd_linebuf_count, &ircd_linebuf_memory_used);

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "z :Users %u(%lu) Invites %u(%lu)",
			   users_counted,
			   (unsigned long) users_counted * sizeof(struct User),
			   users_invited_count, 
			   (unsigned long) users_invited_count * sizeof(dlink_node));

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "z :User channels %u(%lu) Aways %u(%d)",
			   user_channels,
			   (unsigned long) user_channels * sizeof(dlink_node),
			   aways_counted, (int) away_memory);

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "z :Attached confs %u(%lu)",
			   local_client_conf_count,
			   (unsigned long) local_client_conf_count * sizeof(dlink_node));

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "z :Conflines %u(%d)", conf_count, (int) conf_memory);

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "z :Classes %u(%lu)",
			   class_count, 
			   (unsigned long) class_count * sizeof(struct Class));

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "z :Channels %u(%d)",
			   channel_count, (int) channel_memory);

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "z :Bans %u(%d)",
			   channel_bans, (int) channel_ban_memory);

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "z :Exceptions %u(%d)",
			   channel_except, (int) channel_except_memory);

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "z :Invex %u(%d)",
			   channel_invex, (int) channel_invex_memory);

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "z :Channel members %u(%lu) invite %u(%lu)",
			   channel_users,
			   (unsigned long) channel_users * sizeof(dlink_node),
			   channel_invites, 
			   (unsigned long) channel_invites * sizeof(dlink_node));

	total_channel_memory = channel_memory +
		channel_ban_memory +
		channel_users * sizeof(dlink_node) + channel_invites * sizeof(dlink_node);

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "z :Whowas users %ld(%ld)",
			   (long)wwu, (long) (wwu * sizeof(struct User)));

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "z :Whowas array %u(%d)",
			   NICKNAMEHISTORYLENGTH, (int) wwm);

	totww = wwu * sizeof(struct User) + wwm;

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "z :Hash: client %u(%ld) chan %u(%ld)",
			   U_MAX, (long)(U_MAX * sizeof(dlink_list)), 
			   CH_MAX, (long)(CH_MAX * sizeof(dlink_list)));

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "z :linebuf %ld(%ld)",
			   (long)ircd_linebuf_count, (long)ircd_linebuf_memory_used);

	count_scache(&number_servers_cached, &mem_servers_cached);

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "z :scache %ld(%ld)",
			   (long)number_servers_cached, (long)mem_servers_cached);

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "z :hostname hash %d(%ld)",
			   HOST_MAX, (long)HOST_MAX * sizeof(dlink_list));

	total_memory = totww + total_channel_memory + conf_memory +
		class_count * sizeof(struct Class);

	total_memory += mem_servers_cached;
	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "z :Total: whowas %d channel %d conf %d", 
			   (int) totww, (int) total_channel_memory, 
			   (int) conf_memory);

	count_local_client_memory(&local_client_count, &local_client_memory_used);
	total_memory += local_client_memory_used;

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "z :Local client Memory in use: %ld(%ld)",
			   (long)local_client_count, (long)local_client_memory_used);


	count_remote_client_memory(&remote_client_count, &remote_client_memory_used);
	total_memory += remote_client_memory_used;

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "z :Remote client Memory in use: %ld(%ld)",
			   (long)remote_client_count,
			   (long)remote_client_memory_used);

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "z :TOTAL: %d Available:  Current max RSS: %lu",
			   (int) total_memory, get_maxrss());
}

static void
stats_ziplinks (struct Client *source_p)
{
	dlink_node *ptr;
	struct Client *target_p;
	int sent_data = 0;

	DLINK_FOREACH (ptr, serv_list.head)
	{
		target_p = ptr->data;
		if(IsCapable (target_p, CAP_ZIP))
		{
			/* we use memcpy(3) and a local copy of the structure to
			 * work around a register use bug on GCC on the SPARC.
			 * -jmallett, 04/27/2002
			 */
			struct ZipStats zipstats;
			memcpy (&zipstats, &target_p->localClient->slink->zipstats,
				sizeof (struct ZipStats)); 
			sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
					    "Z :ZipLinks stats for %s send[%.2f%% compression "
					    "(%lu kB data/%lu kB wire)] recv[%.2f%% compression "
					    "(%lu kB data/%lu kB wire)]",
					    target_p->name,
					    zipstats.out_ratio, (unsigned long)zipstats.outK, 
					    (unsigned long)zipstats.outK_wire, zipstats.in_ratio, 
					    (unsigned long)zipstats.inK, (unsigned long)zipstats.inK_wire);
			sent_data++;
		}
	}

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "Z :%u ziplink(s)", sent_data);
}

static void
stats_servlinks (struct Client *source_p)
{
	static char Sformat[] = ":%s %d %s %s %u %u %u %u %u :%u %u %s";
	long uptime, sendK, receiveK;
	struct Client *target_p;
	dlink_node *ptr;
	int j = 0;

	if(ConfigServerHide.flatten_links && !IsOper (source_p) &&
	   !IsExemptShide(source_p))
	{
		sendto_one_numeric(source_p, POP_QUEUE, ERR_NOPRIVILEGES,
				   form_str (ERR_NOPRIVILEGES));
		return;
	}

	sendK = receiveK = 0;

	DLINK_FOREACH (ptr, serv_list.head)
	{
		target_p = ptr->data;

		j++;
		sendK += target_p->localClient->sendK;
		receiveK += target_p->localClient->receiveK;

		sendto_one(source_p, POP_QUEUE, Sformat,
			get_id(&me, source_p), RPL_STATSLINKINFO, get_id(source_p, source_p),
			target_p->name,
			(int) ircd_linebuf_len (&target_p->localClient->buf_sendq),
			(int) target_p->localClient->sendM,
			(int) target_p->localClient->sendK,
			(int) target_p->localClient->receiveM,
			(int) target_p->localClient->receiveK,
			ircd_currenttime - target_p->localClient->firsttime,
			(ircd_currenttime > target_p->localClient->lasttime) ? 
			 (ircd_currenttime - target_p->localClient->lasttime) : 0,
			IsOper (source_p) ? show_capabilities (target_p) : "TS");
	}

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "? :%u total server(s)", j);

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "? :Sent total : %7.2f %s",
			   _GMKv (sendK), _GMKs (sendK));
	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "? :Recv total : %7.2f %s",
			   _GMKv (receiveK), _GMKs (receiveK));

	uptime = (ircd_currenttime - startup_time);

	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "? :Server send: %7.2f %s (%4.1f K/s)",
			   _GMKv (me.localClient->sendK), 
			   _GMKs (me.localClient->sendK),
			   (float) ((float) me.localClient->sendK / (float) uptime));
	sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSDEBUG,
			   "? :Server recv: %7.2f %s (%4.1f K/s)",
			   _GMKv (me.localClient->receiveK),
			   _GMKs (me.localClient->receiveK),
			   (float) ((float) me.localClient->receiveK / (float) uptime));
}

static void
stats_ltrace(struct Client *source_p, int parc, const char *parv[])
{
	int doall = 0;
	int wilds = 0;
	const char *name;
	char statchar = parv[1][0];

	/* this is def targeted at us somehow.. */
	if(parc > 2 && !EmptyString(parv[2]))
	{
		/* directed at us generically? */
		if(match(parv[2], me.name) ||
		   (!MyClient(source_p) && !irccmp(parv[2], me.id)))
		{
			name = me.name;
			doall = 1;
		}
		else
		{
			name = parv[2];
			wilds = (strpbrk(name, "*?") != NULL);
		}

		/* must be directed at a specific person thats not us */
		if(!doall && !wilds)
		{
			struct Client *target_p;

			if(MyClient(source_p))
				target_p = find_named_person(name);
			else
				target_p = find_person(name);

			if(target_p != NULL)
			{
				stats_spy(source_p, statchar, target_p->name);
				stats_l_client(source_p, target_p, statchar);
			}
			else
				sendto_one_numeric(source_p, POP_QUEUE, ERR_NOSUCHSERVER,
						form_str(ERR_NOSUCHSERVER),
						name);

			return;
		}
	}
	else
	{
		name = me.name;
		doall = 1;
	}

	stats_spy(source_p, statchar, name);

	if(doall)
	{
		/* local opers get everyone */
		if(MyOper(source_p))
		{
			stats_l_list(source_p, name, doall, wilds, &unknown_list, statchar);
			stats_l_list(source_p, name, doall, wilds, &lclient_list, statchar);
		}
		else
		{
			/* they still need themselves if theyre local.. */
			if(MyClient(source_p))
				stats_l_client(source_p, source_p, statchar);

			stats_l_list(source_p, name, doall, wilds, &oper_list, statchar);
		}

		stats_l_list(source_p, name, doall, wilds, &serv_list, statchar);

		return;
	}

	/* ok, at this point theyre looking for a specific client whos on
	 * our server.. but it contains a wildcard.  --fl
	 */
	stats_l_list(source_p, name, doall, wilds, &lclient_list, statchar);

	return;
}


static void
stats_l_list(struct Client *source_p, const char *name, int doall, int wilds,
	     dlink_list * list, char statchar)
{
	dlink_node *ptr;
	struct Client *target_p;

	/* send information about connections which match.  note, we
	 * dont need tests for IsInvisible(), because non-opers will
	 * never get here for normal clients --fl
	 */
	DLINK_FOREACH(ptr, list->head)
	{
		target_p = ptr->data;

		if(!doall && wilds && !match(name, target_p->name))
			continue;

		stats_l_client(source_p, target_p, statchar);
	}
}

void
stats_l_client(struct Client *source_p, struct Client *target_p,
		char statchar)
{
	if(IsAnyServer(target_p))
	{
		sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSLINKINFO, Lformat,
				target_p->name,
				(int) ircd_linebuf_len(&target_p->localClient->buf_sendq),
				(int) target_p->localClient->sendM,
				(int) target_p->localClient->sendK,
				(int) target_p->localClient->receiveM,
				(int) target_p->localClient->receiveK,
				ircd_currenttime - target_p->localClient->firsttime,
				(ircd_currenttime > target_p->localClient->lasttime) ? 
				 (ircd_currenttime - target_p->localClient->lasttime) : 0,
				IsOper(source_p) ? show_capabilities(target_p) : "-");
	}

	else if(!show_ip(source_p, target_p))
	{
		sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSLINKINFO, Lformat,
				    get_client_name(target_p, MASK_IP),
				    (int) ircd_linebuf_len(&target_p->localClient->buf_sendq),
				    (int) target_p->localClient->sendM,
				    (int) target_p->localClient->sendK,
				    (int) target_p->localClient->receiveM,
				    (int) target_p->localClient->receiveK,
				    ircd_currenttime - target_p->localClient->firsttime,
				    (ircd_currenttime > target_p->localClient->lasttime) ? 
				     (ircd_currenttime - target_p->localClient->lasttime) : 0,
				    "-");
	}

	else
	{
		sendto_one_numeric(source_p, POP_QUEUE, RPL_STATSLINKINFO, Lformat,
				   IsUpper(statchar) ?
				   get_client_name(target_p, SHOW_IP) :
				   get_client_name(target_p, HIDE_IP),
				   (int) ircd_linebuf_len(&target_p->localClient->buf_sendq),
				   (int) target_p->localClient->sendM,
				   (int) target_p->localClient->sendK,
				   (int) target_p->localClient->receiveM,
				   (int) target_p->localClient->receiveK,
				   ircd_currenttime - target_p->localClient->firsttime,
				   (ircd_currenttime > target_p->localClient->lasttime) ? 
				    (ircd_currenttime - target_p->localClient->lasttime) : 0,
				   "-");
	}
}

static void
ircd_dump_fd_callback(int fd, const char *desc, void *data)
{
	struct Client *source_p = data;
	sendto_one_numeric(source_p, HOLD_QUEUE, RPL_STATSDEBUG, "F :fd %-3d desc '%s'", fd, desc);
}

static void
stats_comm(struct Client *source_p)
{
	ircd_dump_fd(ircd_dump_fd_callback, source_p);
	send_pop_queue(source_p);
}
/*
 * stats_spy
 *
 * inputs	- pointer to client doing the /stats
 *		- char letter they are doing /stats on
 * output	- none
 * side effects -
 * This little helper function reports to opers if configured.
 * personally, I don't see why opers need to see stats requests
 * at all. They are just "noise" to an oper, and users can't do
 * any damage with stats requests now anyway. So, why show them?
 * -Dianora
 */
static void
stats_spy(struct Client *source_p, char statchar, const char *name)
{
	hook_data_int data;

	data.client = source_p;
	data.arg1 = name;
	data.arg2 = (int) statchar;

	call_hook(doing_stats_hook, &data);
}

/* stats_p_spy()
 *
 * input	- pointer to client doing stats
 * ouput	-
 * side effects - call hook doing_stats_p
 */
static void
stats_p_spy (struct Client *source_p)
{
	hook_data data;

	data.client = source_p;
	data.arg1 = data.arg2 = NULL;

	call_hook(doing_stats_p_hook, &data);
}

