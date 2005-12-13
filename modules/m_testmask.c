/*
 *  m_testmask.c: Shows the number of matching local and global clients
 *                for a user@host mask, helpful when setting GLINE's
 *
 *  Copyright (C) 2003 by W. Campbell
 *  Coypright (C) 2004 ircd-ratbox development team
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
 *
 */

/* List of ircd includes from ../include/ */
#include "stdinc.h"
#include "struct.h"
#include "client.h"
#include "ircd.h"
#include "match.h"
#include "numeric.h"
#include "send.h"
#include "parse.h"
#include "modules.h"

static int mo_testmask(struct Client *client_p, struct Client *source_p,
			int parc, const char *parv[]);

static int mo_testmaskgecos(struct Client *client_p, struct Client *source_p,
				int parc, const char *parv[]);
 
struct Message testmaskgecos_msgtab = {
	"TESTMASKGECOS", 0, 0, 0, MFLG_SLOW,
	{mg_unreg, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {mo_testmaskgecos, 2}}
};

struct Message testmask_msgtab = {
	"TESTMASK", 0, 0, 0, MFLG_SLOW,
	{mg_unreg, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {mo_testmask, 2}}
};

mapi_clist_av1 testmask_clist[] = { &testmask_msgtab, &testmaskgecos_msgtab, NULL };
DECLARE_MODULE_AV1(testmask, NULL, NULL, testmask_clist, NULL, NULL, "$Revision: 19256 $");

static int
mo_testmask(struct Client *client_p, struct Client *source_p,
			int parc, const char *parv[])
{
	struct Client *target_p;
	int lcount = 0;
	int gcount = 0;
	char *name;
	char *username;
	char *hostname;
	dlink_node *ptr;

	name = LOCAL_COPY(parv[1]);
	collapse(name);

	/* username is required */
	if((hostname = strchr(name, '@')) == NULL)
	{
		sendto_one(source_p, POP_QUEUE, ":%s NOTICE %s :Invalid parameters",
				me.name, source_p->name);
		return 0;
	}

	*hostname++ = '\0';

	/* nickname is optional */
	if((username = strchr(name, '!')) == NULL)
	{
		username = name;
		name = NULL;
	}
	else
		*username++ = '\0';

	if(EmptyString(username) || EmptyString(hostname))
	{
		sendto_one(source_p, POP_QUEUE, ":%s NOTICE %s :Invalid parameters",
				me.name, source_p->name);
		return 0;
	}

	DLINK_FOREACH(ptr, global_client_list.head)
	{
		target_p = ptr->data;

		if(!IsPerson(target_p))
			continue;

		if(match(username, target_p->username) &&
		   match(hostname, target_p->host))
		{
			if(name && !match(name, target_p->name))
				continue;

			if(MyClient(target_p))
				lcount++;
			else
				gcount++;
		}
	}

	sendto_one(source_p, POP_QUEUE, form_str(RPL_TESTMASK),
                               me.name, source_p->name,
                               name ? name : "*",
                               username, hostname, lcount, gcount);
	return 0;
}

static int
mo_testmaskgecos(struct Client *client_p, struct Client *source_p,
			int parc, const char *parv[])
{
	struct Client *target_p;
	int lcount = 0;
	int gcount = 0;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, global_client_list.head)
	{
		target_p = ptr->data;

		if(!IsPerson(target_p))
			continue;

		if(match_esc(parv[1], target_p->info))
		{
			if(MyClient(target_p))
				lcount++;
			else
				gcount++;
		}
	}

	sendto_one(source_p, POP_QUEUE, form_str(RPL_TESTMASKGECOS),
			me.name, source_p->name, 
			lcount, gcount, parv[1]);

	return 0;
}
