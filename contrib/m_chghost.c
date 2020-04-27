/*
 * Copyright (c) 2005 William Pitcock <nenolod -at- nenolod.net>
 * and Jilles Tjoelker <jilles -at- stack.nl>
 * All rights reserved.
 *
 * Redistribution in both source and binary forms are permitted
 * provided that the above copyright notice remains unchanged.
 *
 * m_chghost.c: A module for handling spoofing dynamically.
 */

#include "stdinc.h"
#include "send.h"
#include "channel.h"
#include "defaults.h"
#include "ircd.h"
#include "struct.h"
#include "numeric.h"
#include "s_conf.h"
#include "s_newconf.h"
#include "s_serv.h"
#include "s_user.h"
#include "client.h"
#include "hash.h"
#include "parse.h"
#include "modules.h"
#include "match.h"
#include "whowas.h"
#include "monitor.h"

static int ms_chghost(struct Client *, struct Client *, int, const char **);
static int me_chghost(struct Client *, struct Client *, int, const char **);
static int mo_chghost(struct Client *, struct Client *, int, const char **);

struct Message chghost_msgtab = {
	"CHGHOST", 0, 0, 0, MFLG_SLOW,
	{mg_ignore, mg_not_oper, {ms_chghost, 3}, {ms_chghost, 3}, {me_chghost, 3}, {mo_chghost, 3}}
};

mapi_clist_av1 chghost_clist[] = { &chghost_msgtab, NULL };

DECLARE_MODULE_AV1(chghost, NULL, NULL, chghost_clist, NULL, NULL, "Provides commands used to change and retrieve client hostnames");

/* clean_host()
 *
 * input	- host to check
 * output	- FALSE if erroneous, else TRUE
 * side effects -
 */
static unsigned short
clean_host(const char *host)
{
	int len = 0;
	const char *last_slash = 0;

	if (*host == '\0' || *host == ':')
		return FALSE;

	for(; *host; host++)
	{
		len++;

		if(!IsHostChar(*host))
			return FALSE;
		if(*host == '/')
			last_slash = host;
	}

	if(len > HOSTLEN)
		return FALSE;

	if(last_slash && IsDigit(last_slash[1]))
		return FALSE;

	return TRUE;
}

static unsigned short
do_chghost(struct Client *source_p, struct Client *target_p,
		const char *newhost, int is_encap)
{
	if (!clean_host(newhost))
	{
		sendto_realops_flags(UMODE_ALL, L_ALL, "%s attempted to change hostname for %s to %s (invalid)",
				IsServer(source_p) ? source_p->name : get_oper_name(source_p),
				target_p->name, newhost);
		/* sending this remotely may disclose important
		 * routing information -- jilles */
		if (is_encap ? MyClient(target_p) : !ConfigServerHide.flatten_links)
			sendto_one_notice(target_p, ":*** Notice -- %s attempted to change your hostname to %s (invalid)",
					source_p->name, newhost);
		return FALSE;
	}

	rb_strlcpy(target_p->host, newhost, sizeof(target_p->host));

	if (MyClient(target_p))
		sendto_one_notice(target_p, ":%s is now your hidden host (set by %s)", target_p->host, source_p->name);

	if (MyClient(source_p))
		sendto_one_notice(source_p, ":Changed hostname for %s to %s", target_p->name, target_p->host);
	if (!IsServer(source_p) && !IsService(source_p))
		sendto_realops_flags(UMODE_DEBUG, L_ALL, "%s changed hostname for %s to %s", get_oper_name(source_p), target_p->name, target_p->host);
	return TRUE;
}

/*
 * ms_chghost
 * parv[1] = target
 * parv[2] = host
 */
static int
ms_chghost(struct Client *client_p, struct Client *source_p,
	int parc, const char *parv[])
{
	struct Client *target_p;

	if (!(target_p = find_person(parv[1])))
		return 0;

	if (do_chghost(source_p, target_p, parv[2], 0))
	{
		sendto_server(client_p, NULL,
			CAP_TS6, NOCAPS, ":%s CHGHOST %s %s",
			use_id(source_p), use_id(target_p), parv[2]);
		sendto_server(client_p, NULL,
			CAP_TS6, CAP_ENCAP, ":%s ENCAP * CHGHOST %s :%s",
			use_id(source_p), use_id(target_p), parv[2]);
	}

	return 0;
}

/*
 * me_chghost
 * parv[1] = target
 * parv[2] = host
 */
static int
me_chghost(struct Client *client_p, struct Client *source_p,
	int parc, const char *parv[])
{
	struct Client *target_p;

	if (!(target_p = find_person(parv[1])))
		return 0;

	do_chghost(source_p, target_p, parv[2], 1);
	return 0;
}

/*
 * mo_chghost
 * parv[1] = target
 * parv[2] = host
 */
/* Disable this because of the abuse potential -- jilles
 * No, make it toggleable via ./configure. --nenolod
 */
static int
mo_chghost(struct Client *client_p, struct Client *source_p,
	int parc, const char *parv[])
{
	struct Client *target_p;

	if(!IsOperAdmin(source_p))
	{
		sendto_one(source_p, form_str(ERR_NOPRIVS),
			   me.name, source_p->name, "admin");
		return 0;
	}

	if (!(target_p = find_named_person(parv[1])))
	{
		sendto_one_numeric(source_p, ERR_NOSUCHNICK,
				form_str(ERR_NOSUCHNICK), parv[1]);
		return 0;
	}

	if (!clean_host(parv[2]))
	{
		sendto_one_notice(source_p, ":Hostname %s is invalid", parv[2]);
		return 0;
	}

	do_chghost(source_p, target_p, parv[2], 0);

	sendto_server(NULL, NULL,
		CAP_TS6, NOCAPS, ":%s CHGHOST %s %s",
		use_id(source_p), use_id(target_p), parv[2]);
	sendto_server(NULL, NULL,
		CAP_TS6, CAP_ENCAP, ":%s ENCAP * CHGHOST %s :%s",
		use_id(source_p), use_id(target_p), parv[2]);
	return 0;
}
