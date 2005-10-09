/* ircd-ratbox: an advanced Internet Relay Chat Daemon(ircd).
 * translog.c - Code for dealing with the ban transaction log
 *
 * Copyright (C) 2005 Lee Hardy <lee -at- leeh.co.uk>
 * Copyright (C) 2005 ircd-ratbox development team
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
#include "tools.h"
#include "match.h"
#include "client.h"
#include "channel.h"
#include "banconf.h"
#include "translog.h"
#include "s_log.h"
#include "s_conf.h"
#include "s_newconf.h"
#include "hash.h"
#include "hostmask.h"

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

/* transaction_append()
 *
 * inputs	- string to add to transaction log
 * outputs	-
 * side effects	- string is added to transaction log
 */
static void
transaction_append(const char *data)
{
	FILE *translog;
	char *store;

	if(transaction_queue.head || (translog = fopen(TRANSPATH, "a")) == NULL)
	{
		DupString(store, data);
		ircd_dlinkAddAlloc(store, &transaction_queue);
		return;
	}

	if(fputs(data, translog) < 0)
	{
		DupString(store, data);
		ircd_dlinkAddAlloc(store, &transaction_queue);
		return;
	}

	fclose(translog);
}

/* translog_add_ban()
 *
 * inputs	- type of ban, source, masks to add, reasons
 * outputs	-
 * side effects	- ban is converted into ircd format and appended to
 * 		  transaction log
 */
void
translog_add_ban(translog_type type, struct Client *source_p, const char *mask, 
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

	transaction_append(buf);
}

/* translog_del_ban()
 *
 * inputs	- type of ban, masks to remove
 * outputs	-
 * side effects	- ban is scheduled for removal via transaction log
 */
void
translog_del_ban(translog_type type, const char *mask, const char *mask2)
{
	char buf[BUFSIZE*2];

	if(mask2)
		snprintf(buf, sizeof(buf), "%c %s %s\n",
			translog_del_letter[type], mask, mask2);
	else
		snprintf(buf, sizeof(buf), "%c %s\n",
			translog_del_letter[type], mask);

	transaction_append(buf);
}

static void
translog_unkline(char *line)
{
	struct AddressRec *arec;
	struct ConfItem *aconf;
	int i;
	char *user, *host;

	if((host = strchr(line, ' ')) == NULL)
		return;

	user = line;
	*host++ = '\0';

	HOSTHASH_WALK(i, arec)
	{
		aconf = arec->aconf;

		if((arec->type & ~CONF_SKIPUSER) != CONF_KILL)
			continue;

		if(aconf->flags & CONF_FLAGS_TEMPORARY)
			continue;

		if((aconf->user && irccmp(user, aconf->user)) ||
		   irccmp(host, aconf->host))
			continue;

		delete_one_address_conf(aconf->host, aconf);
	}
	HOSTHASH_WALK_END
}

static void
translog_undline(char *line)
{
	struct AddressRec *arec;
	struct ConfItem *aconf;
	int i;

	HOSTHASH_WALK(i, arec)
	{
		aconf = arec->aconf;

		if((arec->type & ~CONF_SKIPUSER) != CONF_DLINE)
			continue;

		if(aconf->flags & CONF_FLAGS_TEMPORARY)
			continue;

		if(irccmp(aconf->host, line))
			continue;

		delete_one_address_conf(aconf->host, aconf);
	}
	HOSTHASH_WALK_END
}

static void
translog_unresv(char *line)
{
	struct ConfItem *aconf;

	if(IsChannelName(line))
	{
		if((aconf = hash_find_resv(line)) == NULL)
			return;

		if(aconf->flags & CONF_FLAGS_TEMPORARY)
			return;

		del_from_hash(HASH_RESV, line, aconf);
		free_conf(aconf);
	}
	else
	{
		dlink_node *ptr;

		DLINK_FOREACH(ptr, resv_conf_list.head)
		{
			aconf = ptr->data;

			if(aconf->flags & CONF_FLAGS_TEMPORARY)
				continue;

			if(irccmp(aconf->name, line))
				continue;

			ircd_dlinkDestroy(ptr, &resv_conf_list);
			free_conf(aconf);
			return;
		}
	}
}

static void
translog_unxline(char *line)
{
	struct ConfItem *aconf;
	dlink_node *ptr;

	DLINK_FOREACH(ptr, xline_conf_list.head)
	{
		aconf = ptr->data;

		if(aconf->flags & CONF_FLAGS_TEMPORARY)
			continue;

		if(irccmp(aconf->name, line))
			continue;

		free_conf(aconf);
		ircd_dlinkDestroy(ptr, &xline_conf_list);
		return;
	}
}

void
translog_parse(void)
{
	FILE *tlog;
	char line[BUFSIZE*2];
	char *p;

	if((tlog = fopen(TRANSPATH, "r")) == NULL)
		return;

	while(fgets(line, sizeof(line), tlog))
	{
		if((p = strchr(line, '\n')))
			*p = '\0';

		switch(line[0])
		{
		case 'K':
			banconf_parse_kline(&line[2], 0);
			break;

		case 'D':
			banconf_parse_dline(&line[2], 0);
			break;

		case 'R':
			banconf_parse_resv(&line[2], 0);
			break;

		case 'X':
			banconf_parse_xline(&line[2], 0);
			break;

		case 'k':
			translog_unkline(&line[2]);
			break;

		case 'd':
			translog_undline(&line[2]);
			break;

		case 'r':
			translog_unresv(&line[2]);
			break;

		case 'x':
			translog_unxline(&line[2]);
			break;
		}
	}

	fclose(tlog);

	/* its been parsed.. nuke it */
	unlink(TRANSPATH);
}

