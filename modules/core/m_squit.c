/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_squit.c: Makes a server quit.
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

#include <stdinc.h>
#include <struct.h>
#include <client.h>
#include <match.h>
#include <ircd.h>
#include <numeric.h>
#include <s_serv.h>
#include <s_log.h>
#include <send.h>
#include <parse.h>
#include <modules.h>
#include <hash.h>
#include <s_newconf.h>
#include <hook.h>

static int ms_squit(struct Client *, struct Client *, int, const char **);
static int mo_squit(struct Client *, struct Client *, int, const char **);

struct Message squit_msgtab = {
	.cmd = "SQUIT", 

	.handlers[UNREGISTERED_HANDLER] =       { mm_unreg },
	.handlers[CLIENT_HANDLER] =             { mm_not_oper },
	.handlers[RCLIENT_HANDLER] =            { .handler = ms_squit },
	.handlers[SERVER_HANDLER] =             { .handler = ms_squit },
	.handlers[ENCAP_HANDLER] =              { mm_ignore },
	.handlers[OPER_HANDLER] =               { .handler = mo_squit, .min_para = 2 },
};

mapi_clist_av1 squit_clist[] = { &squit_msgtab, NULL };

DECLARE_MODULE_AV1(squit, NULL, NULL, squit_clist, NULL, NULL, "$Revision$");

struct squit_parms
{
	const char *server_name;
	struct Client *target_p;
};

static struct squit_parms *find_squit(struct Client *client_p,
				      struct Client *source_p, const char *server);


/*
 * mo_squit - SQUIT message handler
 *      parv[0] = sender prefix
 *      parv[1] = server name
 *      parv[2] = comment
 */
static int
mo_squit(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct squit_parms *found_squit;
	const char *comment = (parc > 2 && parv[2]) ? parv[2] : client_p->name;

	if((found_squit = find_squit(client_p, source_p, parv[1])))
	{
	
		if(IsFake(found_squit->target_p))
		{
			sendto_one(source_p, "%s NOTICE %s :Cannot SQUIT a service", me.name, source_p->name);
			return 0;
		}
		if(MyConnect(found_squit->target_p))
		{
			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "Received SQUIT %s from %s (%s)",
					     found_squit->target_p->name,
					     get_client_name(source_p, HIDE_IP), comment);
			ilog(L_SERVER, "Received SQUIT %s from %s (%s)",
			     found_squit->target_p->name, log_client_name(source_p, HIDE_IP),
			     comment);
		}
		else if(!IsOperRemote(source_p))
		{
			sendto_one_numeric(source_p, s_RPL(ERR_NOPRIVS), "remote");
			return 0;
		}

		exit_client(client_p, found_squit->target_p, source_p, comment);
		return 0;
	}
	else
	{
		sendto_one_numeric(source_p, ERR_NOSUCHSERVER, form_str(ERR_NOSUCHSERVER), parv[1]);
	}

	return 0;
}

/*
 * ms_squit - SQUIT message handler
 *      parv[0] = sender prefix
 *      parv[1] = server name
 *      parv[2] = comment
 */
static int
ms_squit(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Client *target_p;
	const char *comment = (parc > 2 && parv[2]) ? parv[2] : client_p->name;

	if(parc < 2)
		target_p = client_p;
	else
	{
		if((target_p = find_server(NULL, parv[1])) == NULL)
			return 0;

		if(IsMe(target_p))
			target_p = client_p;
		if(!IsServer(target_p))
			return 0;
	}
	
	if(IsFake(target_p))
	{
                if (!IsFakePersist(target_p))
                        call_hook(h_service_squit, target_p);
                        
                sendto_wallops_flags(UMODE_WALLOP, &me,
                                                        "Remote SQUIT %s from %s (%s)",
                                                        target_p->name, source_p->name, comment);
                                                        
                sendto_server(NULL, NULL, NOCAPS, NOCAPS,
                                                ":%s WALLOPS :Remote SQUIT %s from %s (%s)",
                                                me.name, target_p->name, source_p->name, comment);
                                                
                ilog(L_SERVER, "SQUIT From %s : %s (%s)", parv[0],
                                target_p->name, comment);
                                
                sendto_server(client_p, NULL, NOCAPS, NOCAPS,
                                                "SQUIT %s :%s",
                                                target_p->name, me.name);
                                                
                if(IsFakePersist(target_p))     
                {
                        sendto_server(NULL, NULL, NOCAPS, NOCAPS,
                                                        ":%s SERVER %s 2 :%s",
                                                        me.name, target_p->name,
                                                        target_p->info);
                        call_hook(h_service_squit, target_p);
                }
                else
                        destroy_fake_server(target_p, 1);
                    
                return 0;
        } 
	

	/* Server is closing its link */
	if(target_p == client_p)
	{
		sendto_realops_flags(UMODE_ALL, L_ALL, "Server %s closing link (%s)",
				     target_p->name, comment);
	}
	/*
	 **  Notify all opers, if my local link is remotely squitted
	 */
	else if(MyConnect(target_p))
	{
		sendto_wallops_flags(UMODE_WALLOP, &me,
				     "Remote SQUIT %s from %s (%s)",
				     target_p->name, source_p->name, comment);

		sendto_server(NULL, NULL, CAP_TS6, NOCAPS,
			      ":%s WALLOPS :Remote SQUIT %s from %s (%s)",
			      me.id, target_p->name, source_p->name, comment);

		sendto_server(NULL, NULL, NOCAPS, CAP_TS6,
			      ":%s WALLOPS :Remote SQUIT %s from %s (%s)",
			      me.name, target_p->name, source_p->name, comment);

		ilog(L_SERVER, "SQUIT From %s : %s (%s)", parv[0], target_p->name, comment);

	}
	exit_client(client_p, target_p, source_p, comment);
	return 0;
}


/*
 * find_squit
 * inputs	- local server connection
 *		-
 *		-
 * output	- pointer to struct containing found squit or none if not found
 * side effects	-
 */
static struct squit_parms *
find_squit(struct Client *client_p, struct Client *source_p, const char *server)
{
	static struct squit_parms found_squit;
	struct Client *target_p = NULL;
	struct Client *p;
	rb_dlink_node *ptr;

	/* must ALWAYS be reset */
	found_squit.target_p = NULL;
	found_squit.server_name = NULL;


	/*
	 ** The following allows wild cards in SQUIT. Only useful
	 ** when the command is issued by an oper.
	 */

	RB_DLINK_FOREACH(ptr, global_serv_list.head)
	{
		p = ptr->data;
		if(IsServer(p) || IsMe(p))
		{
			if(match(server, p->name))
			{
				target_p = p;
				break;
			}
		}
	}

	if(target_p == NULL)
		return NULL;

	found_squit.target_p = target_p;
	found_squit.server_name = server;

	if(IsMe(target_p))
	{
		if(IsClient(client_p))
		{
			if(MyClient(client_p))
				sendto_one_notice(source_p, ":You are trying to squit me.");
			return NULL;
		}
		else
		{
			found_squit.target_p = client_p;
			found_squit.server_name = client_p->name;
		}
	}

	if(found_squit.target_p != NULL)
		return &found_squit;
	else
		return (NULL);
}
