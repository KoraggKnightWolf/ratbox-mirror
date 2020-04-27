/* contrib/m_oaccept.c
 * Copyright (C) 1996-2002 Hybrid Development Team
 * Copyright (C) 2004-2012 ircd-ratbox Development Team
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
 */

#include "stdinc.h"
#include "ratbox_lib.h"
#include "struct.h"
#include "channel.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "match.h"
#include "ircd.h"
#include "hostmask.h"
#include "numeric.h"
#include "s_conf.h"
#include "s_newconf.h"
#include "s_log.h"
#include "send.h"
#include "hash.h"
#include "s_serv.h"
#include "parse.h"
#include "modules.h"


static int mo_oaccept(struct Client *client_p, struct Client *source_p,
			int parc, const char *parv[]);
static void add_accept(struct Client *, struct Client *);

struct Message oaccept_msgtab = {
	"OACCEPT", 0, 0, 0, MFLG_SLOW,
	{mg_unreg, mg_not_oper, {mo_oaccept, 2}, mg_ignore, mg_ignore, {mo_oaccept, 2}}
};

mapi_clist_av1 oaccept_clist[] = { &oaccept_msgtab, NULL };

DECLARE_MODULE_AV1(oaccept, NULL, NULL, oaccept_clist, NULL, NULL, "$Revision: 27381 $");

/*
 * mo_oaccept
 *      parv[0] = user to modify accept list
 */
static int
mo_oaccept(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Client *target_p;

	if(!IsOperAdmin(source_p))
	{
		sendto_one(source_p, form_str(ERR_NOPRIVS), me.name, source_p->name, "oaccept");
		return 0;
	}

	if((hunt_server(client_p, source_p, ":%s OACCEPT %s", 1, parc, parv)) != HUNTED_ISME)
		return 0;

	if((target_p = find_client(parv[1])) == NULL)
	{
		sendto_one_numeric(source_p, ERR_NOSUCHNICK, form_str(ERR_NOSUCHNICK), parv[1]);
		return 0;
	}

	if(!IsClient(target_p))
		return 0;

	if(accept_message(source_p, target_p))
	{
		sendto_one(source_p, form_str(ERR_ACCEPTEXIST),
			me.name, source_p->name, target_p->name);
		return 0;
	}

	/* don't check accept length, we will override this anyway */

	/* add accept to target and notify them */
	add_accept(target_p, source_p);
	sendto_one_notice(target_p, ":Operator %s forced you to /ACCEPT them (overriding umode +g)", source_p->name);

	return 0;
}

static void
add_accept(struct Client *source_p, struct Client *target_p)
{
	rb_dlinkAddAlloc(target_p, &source_p->localClient->allow_list);
	rb_dlinkAddAlloc(source_p, &target_p->on_allow_list);
}
