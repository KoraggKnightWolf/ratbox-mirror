/* ircd-ratbox: an advanced Internet Relay Chat Daemon(ircd).
 * banconf.c - Code for reading/writing bans.
 *
 * Copyright (C) 2005 Lee Hardy <lee -at- leeh.co.uk>
 * Copyright (C) 2005 ircd-ratbox development team
 *
 * $Id$
 */
#include "stdinc.h"
#include "struct.h"
#include "tools.h"
#include "irc_string.h"
#include "client.h"
#include "s_conf.h"
#include "s_log.h"
#include "banconf.h"

static dlink_list transaction_queue;

static char translog_add_letter[] =
{
	'K',	/* TRANS_KLINE */
	'D',	/* TRANS_DLINE */
	'X',	/* TRANS_XLINE */
	'R'	/* TRANS_RESV */
};

static char translog_del_letter[] =
{
	'k',	/* TRANS_KLINE */
	'd',	/* TRANS_DLINE */
	'x',	/* TRANS_XLINE */
	'r'	/* TRANS_RESV */
};

static void
transaction_append(const char *data)
{
	FILE *translog;
	char *store;

	if(transaction_queue.head || (translog = fopen(TRANSPATH, "a")) == NULL)
	{
		DupString(store, data);
		dlinkAddAlloc(store, &transaction_queue);
		return;
	}

	if(fputs(data, translog) < 0)
	{
		DupString(store, data);
		dlinkAddAlloc(store, &transaction_queue);
		return;
	}

	fclose(translog);
}

void
banconf_add_write(banconf_type type, struct Client *source_p, const char *mask, 
		const char *mask2, const char *reason, const char *oper_reason)
{
	char buf[BUFSIZE*2];
	FILE *translog;

	if(reason == NULL)
		reason = "";
	if(oper_reason == NULL)
		oper_reason = "";

	if(mask2)
		snprintf(buf, sizeof(buf), "%c \"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%ld\"\n",
				translog_add_letter[type], mask, mask2, reason, 
				oper_reason, smalldate(),
				get_oper_name(source_p), ircd_currenttime);
	else
		snprintf(buf, sizeof(buf), "%c \"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%ld\"\n",
				translog_add_letter[type], mask, reason, 
				oper_reason, smalldate(),
				get_oper_name(source_p), ircd_currenttime);
}

void
banconf_del_write(banconf_type type, const char *mask, const char *mask2)
{
	char buf[BUFSIZE*2];

	if(mask2)
		snprintf(buf, sizeof(buf), "%c %s %s",
			translog_del_letter[type], mask, mask2);
	else
		snprintf(buf, sizeof(buf), "%c %s",
			translog_del_letter[type], mask);

	transaction_append(buf);
}
