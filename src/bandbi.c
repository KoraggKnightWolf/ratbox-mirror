/* src/bandbi.c
 * An interface to the ban db.
 *
 * Copyright (C) 2006 Lee Hardy <lee -at- leeh.co.uk>
 * Copyright (C) 2006 ircd-ratbox development team
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
#include "ircd_lib.h"
#include "struct.h"
#include "client.h"
#include "s_conf.h"
#include "s_log.h"
#include "match.h"
#include "bandbi.h"
#include "parse.h"
#include "channel.h"
#include "operhash.h"
#include "hostmask.h"
#include "hash.h"
#include "s_newconf.h"
#include "reject.h"

static char bandb_add_letter[LAST_BANDB_TYPE] =
{
	'K', 'D', 'X', 'R'
};

dlink_list bandb_pending;

static ircd_helper *bandb_helper;
static void fork_bandb(void);

static void bandb_parse(ircd_helper *);
static void bandb_restart_cb(ircd_helper *);
static char *bandb_path;

void
init_bandb(void)
{
	fork_bandb();
}


static void
fork_bandb(void)
{
	char fullpath[PATH_MAX+1];
#ifdef _WIN32
	const char *suffix = ".exe";
#else
	const char *suffix = "";
#endif


	if(bandb_path == NULL)
	{
		ircd_snprintf(fullpath, sizeof(fullpath), "%s/bandb%s", BINPATH, suffix);

		if(access(fullpath, X_OK) == -1)
		{
			ircd_snprintf(fullpath, sizeof(fullpath), "%s/bin/bandb%s", ConfigFileEntry.dpath, suffix);

			if(access(fullpath, X_OK) == -1)
			{
				ilog(L_MAIN, "Unable to execute bandb in %s or %s/bin",
					BINPATH, ConfigFileEntry.dpath);
				return;
			}
		}
		bandb_path = ircd_strdup(fullpath);
	}


	bandb_helper = ircd_helper_start("bandb", bandb_path, bandb_parse, bandb_restart_cb);

	if(bandb_helper == NULL)
	{
		ilog(L_MAIN, "ircd_helper_start failed: %s", strerror(errno));
		return;
	}

	ircd_helper_run(bandb_helper);
	return;
}

void
bandb_add(bandb_type type, struct Client *source_p, const char *mask1,
		const char *mask2, const char *reason, const char *oper_reason)
{
	static char buf[BUFSIZE];

	ircd_snprintf(buf, sizeof(buf), "%c %s ",
			bandb_add_letter[type], mask1);

	if(!EmptyString(mask2))
		ircd_snprintf_append(buf, sizeof(buf), "%s ", mask2);

	ircd_snprintf_append(buf, sizeof(buf), "%s %lu :%s", 
				get_oper_name(source_p), ircd_currenttime, reason);

	if(!EmptyString(oper_reason))
		ircd_snprintf_append(buf, sizeof(buf), "|%s", oper_reason);

	ircd_helper_write(bandb_helper, "%s", buf);
}

static char bandb_del_letter[LAST_BANDB_TYPE] =
{
	'k', 'd', 'x', 'r'
};

void
bandb_del(bandb_type type, const char *mask1, const char *mask2)
{
	static char buf[BUFSIZE];

	buf[0] = '\0';

	ircd_snprintf_append(buf, sizeof(buf), "%c %s",
				bandb_del_letter[type], mask1);

	if(!EmptyString(mask2))
		ircd_snprintf_append(buf, sizeof(buf), " %s", mask2);

	ircd_helper_write(bandb_helper, "%s", buf);
}

static void
bandb_handle_ban(char *parv[], int parc)
{
	struct ConfItem *aconf;
	char *p;
	int para = 1;

	aconf = make_conf();
	aconf->port = 0;

	if(parv[0][0] == 'K')
		aconf->user = ircd_strdup(parv[para++]);

	aconf->host = ircd_strdup(parv[para++]);
	aconf->info.oper = operhash_add(parv[para++]);

	switch(parv[0][0])
	{
		case 'K':
			aconf->status = CONF_KILL;
			break;

		case 'D':
			aconf->status = CONF_DLINE;
			break;

		case 'X':
			aconf->status = CONF_XLINE;
			break;

		case 'R':
			if(IsChannelName(aconf->host))
				aconf->status = CONF_RESV_CHANNEL;
			else
				aconf->status = CONF_RESV_NICK;

			break;
	}

	if((p = strchr(parv[para], '|')))
	{
		*p++ = '\0';
		aconf->spasswd = ircd_strdup(p);
	}

	aconf->passwd = ircd_strdup(parv[para]);

	ircd_dlinkAddAlloc(aconf, &bandb_pending);
}

static int
bandb_check_kline(struct ConfItem *aconf)
{
	struct irc_sockaddr_storage daddr;
	struct ConfItem *kconf = NULL;
	int aftype;
	const char *p;

	aftype = parse_netmask(aconf->host, (struct sockaddr *) &daddr, NULL);

	if(aftype != HM_HOST)
	{
#ifdef IPV6
		if(aftype == HM_IPV6)
			aftype = AF_INET6;
		else
#endif
			aftype = AF_INET;

		kconf = find_conf_by_address(aconf->host, NULL, (struct sockaddr *) &daddr, CONF_KILL, aftype, aconf->user);
	}
	else
		kconf = find_conf_by_address(aconf->host, NULL, NULL, CONF_KILL, 0, aconf->user);

	if(kconf && ((kconf->flags & CONF_FLAGS_TEMPORARY) == 0))
		return 0;

	for(p = aconf->user; *p; p++)
	{
		if(!IsUserChar(*p) && !IsKWildChar(*p))
			return 0;
	}

	for(p = aconf->host; *p; p++)
	{
		if(!IsHostChar(*p) && !IsKWildChar(*p))
			return 0;
	}

	return 1;
}

static int
bandb_check_dline(struct ConfItem *aconf)
{
	struct irc_sockaddr_storage daddr;
/* 	struct ConfItem *dconf; */
	int bits;

	if(!parse_netmask(aconf->host, &daddr, &bits))
		return 0;

	return 1;
}

static int
bandb_check_xline(struct ConfItem *aconf)
{
	struct ConfItem *xconf;
	/* XXX perhaps convert spaces to \s? -- jilles */

	xconf = find_xline_mask(aconf->host);
	if (xconf != NULL && !(xconf->flags & CONF_FLAGS_TEMPORARY))
		return 0;

	return 1;
}

static int
bandb_check_resv_channel(struct ConfItem *aconf)
{
	const char *p;

	if(hash_find_resv(aconf->host) || strlen(aconf->host) > CHANNELLEN)
		return 0;

	for(p = aconf->host; *p; p++)
	{
		if(!IsChanChar(*p))
			return 0;
	}

	return 1;
}

static int
bandb_check_resv_nick(struct ConfItem *aconf)
{
	if(!clean_resv_nick(aconf->host))
		return 0;

	if(find_nick_resv(aconf->host))
		return 0;

	return 1;
}

static void
bandb_handle_clear(void)
{
	dlink_node *ptr, *next_ptr;

	DLINK_FOREACH_SAFE(ptr, next_ptr, bandb_pending.head)
	{
		free_conf(ptr->data);
		ircd_dlinkDestroy(ptr, &bandb_pending);
	}
}

static void
bandb_handle_finish(void)
{
	struct ConfItem *aconf;
	dlink_node *ptr, *next_ptr;

	clear_out_address_conf_bans();
	clear_s_newconf_bans();

	DLINK_FOREACH_SAFE(ptr, next_ptr, bandb_pending.head)
	{
		aconf = ptr->data;

		ircd_dlinkDestroy(ptr, &bandb_pending);

		switch(aconf->status)
		{
			case CONF_KILL:
				if(bandb_check_kline(aconf))
					add_conf_by_address(aconf->host, CONF_KILL, aconf->user, aconf);
				else
					free_conf(aconf);

				break;

			case CONF_DLINE:
				if(bandb_check_dline(aconf))
					add_dline(aconf);
				else
					free_conf(aconf);

				break;

			case CONF_XLINE:
				if(bandb_check_xline(aconf))
					ircd_dlinkAddAlloc(aconf, &xline_conf_list);
				else
					free_conf(aconf);

				break;

			case CONF_RESV_CHANNEL:
				if(bandb_check_resv_channel(aconf))
					add_to_hash(HASH_RESV, aconf->host, aconf);
				else
					free_conf(aconf);

				break;

			case CONF_RESV_NICK:
				if(bandb_check_resv_nick(aconf))
					ircd_dlinkAddAlloc(aconf, &resv_conf_list);
				else
					free_conf(aconf);

				break;
		}
	}

	check_banned_lines();
}

static void
bandb_parse(ircd_helper *helper)
{
	static char buf[READBUF_SIZE];
	char *parv[MAXPARA+1];
	int len, parc;

	while((len = ircd_helper_read(helper, buf, sizeof(buf))))
	{
		parc = ircd_string_to_array(buf, parv, MAXPARA);

		if(parc < 1)
			continue;

		switch(parv[0][0])
		{
			case 'K':
			case 'D':
			case 'X':
			case 'R':
				bandb_handle_ban(parv, parc);
				break;

			case 'C':
				bandb_handle_clear();
			case 'F':
				bandb_handle_finish();
				break;
		}
	}
}

void
bandb_rehash_bans(void)
{
	if(bandb_helper != NULL)
		ircd_helper_write(bandb_helper, "L");
}

static void bandb_restart_cb(ircd_helper *helper)
{
	return;
}

