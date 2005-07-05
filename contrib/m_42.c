/*
 *   Copyright (C) infinity-infinity God <God@Heaven>
 *
 *   Bob was here 
 *   $Id$
 */

#include "stdinc.h"
#include "ircd_lib.h"
#include "struct.h"
#include "parse.h"
#include "modules.h"
#include "client.h"
#include "ircd.h"
#include "send.h"

static int mclient_42(struct Client *client_p, struct Client *source_p, int parc, const char *parv[]);
static int mclient_kilroy(struct Client *client_p, struct Client *source_p, int parc, const char *parv[]);

struct Message hgtg_msgtab = {
  "42", 0, 0, 0, MFLG_SLOW,
  { mg_ignore, {mclient_42, 0}, mg_ignore, mg_ignore, mg_ignore, {mclient_42, 0}
  }
};

struct Message kilroy_msgtab = {
  "KILROY", 0, 0, 0, MFLG_SLOW,
  { mg_ignore, {mclient_kilroy, 0}, mg_ignore, mg_ignore, mg_ignore, {mclient_kilroy, 0}
  }
};
 

mapi_clist_av1 hgtg_clist[] = { &hgtg_msgtab, &kilroy_msgtab, NULL };


DECLARE_MODULE_AV1(42, NULL, NULL, hgtg_clist, NULL, NULL, "Revision 0.42");


static int
mclient_42(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	sendto_one(source_p, POP_QUEUE, ":%s NOTICE %s :The Answer to Life, the Universe, and Everything.",
		   me.name, source_p->name);
	return 0;
}

static int
mclient_kilroy(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	sendto_one(source_p, POP_QUEUE, ":%s NOTICE %s :Kilroy was here",
		   me.name, source_p->name);
	return 0;
}


