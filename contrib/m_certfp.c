/*   contrib/m_certfp.c
 *   Copyright (C) 2002 Hybrid Development Team
 *   Copyright (C) 2004-2012 ircd-ratbox development team
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 1, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include "stdinc.h"
#include "ratbox_lib.h"
#include "struct.h"
#include "channel.h"
#include "client.h"
#include "ircd.h"
#include "numeric.h"
#include "s_log.h"
#include "s_serv.h"
#include "send.h"
#include "whowas.h"
#include "match.h"
#include "hash.h"
#include "parse.h"
#include "modules.h"
#include "s_newconf.h"

static int me_certfp(struct Client *client_p, struct Client *source_p, int parc, const char *parv[]);

struct Message certfp_msgtab = {
	"CERTFP", 0, 0, 0, 0,
	{mg_unreg, mg_ignore, mg_ignore, mg_ignore, {me_certfp, 2}, mg_ignore}
};

mapi_clist_av1 certfp_clist[] = { &certfp_msgtab, NULL };

DECLARE_MODULE_AV1(certfp, NULL, NULL, certfp_clist, NULL, NULL, "$Revision: 27371 $");


/*
** me_certfp
**      parv[1] = certfp string
*/
static int
me_certfp(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	if (source_p->user == NULL)
		return 0;

	rb_free(source_p->certfp);
	SetSSL(source_p);
	source_p->certfp = NULL;
	if (!EmptyString(parv[1]))
		source_p->certfp = rb_strdup(parv[1]);

	return 0;
}
