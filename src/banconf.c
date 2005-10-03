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
#include "channel.h"
#include "s_conf.h"
#include "s_log.h"
#include "send.h"
#include "banconf.h"
#include "hostmask.h"
#include "reject.h"
#include "s_newconf.h"
#include "hash.h"

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

static char *
banconf_parse_field(char *nline)
{
	static char *line = NULL;
	char *begin, *end;

	if(nline)
		line = nline;

	if(EmptyString(line))
		return NULL;


	/* skip beginning " */
	if(*line == '"')
		line++;
	else
		return NULL;

	begin = line;

	while(1)
	{
		end = strchr(line, ',');

		if(end == NULL)
		{
			end = line + strlen(line);
			line = NULL;

			if(*end == '"')
			{
				*end = '\0';
				return begin;
			}
			
			return NULL;
		}
		else if(*(end - 1) == '"')
		{
			line = end + 1;
			*(end - 1) = '\0';
			return begin;
		}

		/* we require a ", to end the field -- this is ugly,
		 * but bans are never escaped on writing..
		 */
		line = end + 1;
	}

	return NULL;
}

static int
banconf_set_field(char *line, char **field)
{
	static const char *empty = "";
	char *tmp;

	tmp = banconf_parse_field(line);

	if(tmp == NULL)
		return 0;

	if(*tmp == '\0')
		*field = NULL;
	else
		DupString(*field, tmp);

	return 1;
}

static int
banconf_parse_line(char *line, char **mask, char **mask2, char **reason,
			char **oper_reason)
{
	if(!banconf_set_field(line, mask))
		return 0;

	if(mask2)
	{
		if(!banconf_set_field(NULL, mask2))
			return 0;
	}

	if(!banconf_set_field(NULL, reason))
		return 0;

	if(oper_reason)
	{
		if(!banconf_set_field(NULL, oper_reason))
			return 0;
	}

	return 1;
}

static void
banconf_parse_kline(char *line)
{
	struct ConfItem *aconf;

	aconf = make_conf();
	aconf->status = CONF_KILL;

	if(banconf_parse_line(line, &aconf->host, &aconf->user,
				&aconf->passwd, &aconf->spasswd))
		add_conf_by_address(aconf->host, aconf->status,
					aconf->user, aconf);
	else
		free_conf(aconf);
}

static void
banconf_parse_dline(char *line)
{
	struct ConfItem *aconf;

	aconf = make_conf();
	aconf->status = CONF_DLINE;

	if(banconf_parse_line(line, &aconf->host, NULL,
				&aconf->passwd, &aconf->spasswd))
	{
		if(!add_dline(aconf))
		{
			ilog(L_MAIN, "Invalid Dline %s ignored", aconf->host);
			free_conf(aconf);
		}
	}
	else
		free_conf(aconf);
}

static void
banconf_parse_xline(char *line)
{
	struct ConfItem *aconf;

	aconf = make_conf();
	aconf->status = CONF_XLINE;

	if(banconf_parse_line(line, &aconf->name, NULL, 
				&aconf->passwd, NULL))
		dlinkAddAlloc(aconf, &xline_conf_list);
	else
		free_conf(aconf);
}

static void
banconf_parse_resv(char *line)
{
	struct ConfItem *aconf;

	aconf = make_conf();

	if(banconf_parse_line(line, &aconf->name, NULL,
				&aconf->passwd, NULL))
	{
		if(IsChannelName(aconf->name))
		{
			if(hash_find_resv(aconf->name))
				return;

			aconf->status = CONF_RESV_CHANNEL;
			add_to_resv_hash(aconf->name, aconf);
		}
		else if(clean_resv_nick(aconf->name))
		{
			if(find_nick_resv(aconf->name))
				return;

			aconf->status = CONF_RESV_NICK;
			dlinkAddAlloc(aconf, &resv_conf_list);
		}
		else
			free_conf(aconf);
	}
	else
		free_conf(aconf);
}

static struct banconf_file
{
	const char **filename;
	void (*func) (char *);
} banconf_files[] = {
	{ &ConfigFileEntry.klinefile,	banconf_parse_kline	},
	{ &ConfigFileEntry.dlinefile,	banconf_parse_dline	},
	{ &ConfigFileEntry.xlinefile,	banconf_parse_xline	},
	{ &ConfigFileEntry.resvfile,	banconf_parse_resv	},
	{ NULL, NULL }
};

void
banconf_parse(void)
{
	char line[BUFSIZE*2];
	char buf[MAXPATHLEN];
	FILE *banfile;
	char *p;
	int i = 0;
	int perm = 0;

	while(banconf_files[i].filename)
	{
		while(perm < 2)
		{
			snprintf(buf, sizeof(buf), *banconf_files[i].filename);

			if(perm)
				strlcat(buf, ".perm", sizeof(buf));

			if((banfile = fopen(buf, "r")) == NULL)
			{
				if(!perm)
				{
					ilog(L_MAIN, "Failed reading ban file %s",
						*banconf_files[i].filename);
					sendto_realops_flags(UMODE_ALL, L_ALL,
							"Can't open %s file bans could be missing!",
							*banconf_files[i].filename);
				}

				continue;
			}


			while(fgets(line, sizeof(line), banfile))
			{
				if((p = strchr(line, '\n')))
					*p = '\0';

				if(EmptyString(line) || (*line == '#'))
					continue;

				(banconf_files[i].func)(line);
			}

			fclose(banfile);
			perm++;
		}

		i++;
	}
}

