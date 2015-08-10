/*  modules/m_encap.c
 *  Copyright (C) 2003-2012 ircd-ratbox development team
 *  Copyright (C) 2003 Lee Hardy <lee@leeh.co.uk>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1.Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * 2.Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * 3.The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $Id$
 */

#include "stdinc.h"
#include "struct.h"
#include "send.h"
#include "ircd.h"
#include "s_serv.h"
#include "hash.h"
#include "parse.h"
#include "modules.h"
#include "match.h"

static int ms_encap(struct Client *client_p, struct Client *source_p, int parc, const char *parv[]);

struct Message encap_msgtab = {
	.cmd = "ENCAP",
	.handlers[UNREGISTERED_HANDLER] =	{ mm_ignore },
	.handlers[CLIENT_HANDLER] =		{ mm_ignore },
	.handlers[RCLIENT_HANDLER] =		{ .handler = ms_encap, .min_para = 3 },	 
	.handlers[SERVER_HANDLER] =		{ .handler = ms_encap, .min_para = 3 },	 
	.handlers[ENCAP_HANDLER] =		{ mm_ignore },	
	.handlers[OPER_HANDLER] =		{ mm_ignore },
};

mapi_clist_av1 encap_clist[] = { &encap_msgtab, NULL };

DECLARE_MODULE_AV1(encap, NULL, NULL, encap_clist, NULL, NULL, "$Revision$");

/* ms_encap()
 *
 * parv[1] - destination server
 * parv[2] - subcommand
 * parv[3] - parameters
 */
static int
ms_encap(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	char buffer[IRCD_BUFSIZE];
	char *ptr;
	int cur_len = 0;
	int len;
	int i;

	ptr = buffer;

	for(i = 1; i < parc - 1; i++)
	{
		len = strlen(parv[i]) + 1;

		/* ugh, not even at the last parameter, just bail --fl */
		if((size_t) (cur_len + len) >= sizeof(buffer))
			return 0;

		snprintf(ptr, sizeof(buffer) - cur_len, "%s ", parv[i]);
		cur_len += len;
		ptr += len;
	}

	len = strlen(parv[i]);

	/* if its a command without parameters, dont prepend a ':' */
	if(parc == 3)
		snprintf(ptr, sizeof(buffer) - cur_len, "%s", parv[2]);
	else
		snprintf(ptr, sizeof(buffer) - cur_len, ":%s", parv[parc - 1]);

	/* add a trailing \0 if it was too long */
	if((cur_len + len) >= IRCD_BUFSIZE)
		buffer[IRCD_BUFSIZE - 1] = '\0';

	sendto_match_servs(source_p, parv[1], CAP_ENCAP, NOCAPS, "ENCAP %s", buffer);

	/* if it matches us, find a matching handler and call it */
	if(match(parv[1], me.name))
		handle_encap(client_p, source_p, parv[2], parc - 2, parv + 2);

	return 0;
}
