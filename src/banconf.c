/* ircd-ratbox: an advanced Internet Relay Chat Daemon(ircd).
 * banconf.c - Code for reading ban configs.
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
#include "s_conf.h"
#include "s_log.h"
#include "send.h"
#include "banconf.h"
#include "hostmask.h"
#include "reject.h"
#include "s_newconf.h"
#include "hash.h"

/* banconf_parse_field()
 *
 * inputs	- new line pointer, NULL to parse existing line
 * outputs	- next field from line
 * side effects	- parses the given line, splitting it into fields
 */
static char *
banconf_parse_field(char *nline, int quoted)
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
	/* field must be quoted */
	else if(quoted)
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
			/* not supposed to be quoted, so ok */
			else if(!quoted)
				return begin;
			
			return NULL;
		}
		else if(*(end - 1) == '"')
		{
			line = end + 1;
			*(end - 1) = '\0';
			return begin;
		}
		else if(!quoted)
		{
			line = end + 1;
			*end = '\0';
			return begin;
		}

		/* we require a ", to end the field -- this is ugly,
		 * but bans are never escaped on writing..
		 */
		line = end + 1;
	}

	return NULL;
}

/* banconf_set_field()
 *
 * inputs	- line to take fields from, location to store field
 * outputs	- 1 on success, otherwise 0
 * side effects	- parses a field into the given location
 */
static int
banconf_set_field(char *line, char **field)
{
	static const char *empty = "";
	char *tmp;

	tmp = banconf_parse_field(line, 1);

	if(tmp == NULL)
		return 0;

	if(*tmp == '\0')
		*field = NULL;
	else
		DupString(*field, tmp);

	return 1;
}

/* banconf_set_field_unquoted()
 *
 * inputs	- line to take fields from, location to store field
 * outputs	- 1 on success, otherwise 0
 * side effects - parses a field into the given location, without expecting
 *		  it to be quoted
 */
static int
banconf_set_field_unquoted(char *line, char **field)
{
	static const char *empty = "";
	char *tmp;

	tmp = banconf_parse_field(line, 0);

	if(tmp == NULL)
		return 0;

	if(*tmp == '\0')
		*field = NULL;
	else
		DupString(*field, tmp);

	return 1;
}

/* banconf_parse_line()
 *
 * inputs	- line to take fields from, locations for storage
 * outputs	- 1 on success, otherwise 0
 * side effects	- parses given line generically according to passed fields
 */
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

/* banconf_parse_kline()
 *
 * inputs	- line to parse
 * outputs	-
 * side effects	- parses the given kline line
 */
void
banconf_parse_kline(char *line)
{
	struct ConfItem *aconf;

	aconf = make_conf();
	aconf->status = CONF_KILL;

	if(banconf_parse_line(line, &aconf->user, &aconf->host,
				&aconf->passwd, &aconf->spasswd))
		add_conf_by_address(aconf->host, aconf->status,
					aconf->user, aconf);
	else
		free_conf(aconf);
}

/* banconf_parse_dline()
 *
 * inputs	- line to parse
 * outputs	-
 * side effects	- parses the given dline line
 */
void
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

/* banconf_parse_xline()
 *
 * inputs	- line to parse
 * outputs	-
 * side effects - parses the given xline line
 */
void
banconf_parse_xline(char *line)
{
	struct ConfItem *aconf;

	aconf = make_conf();
	aconf->status = CONF_XLINE;

	if(banconf_parse_line(line, &aconf->name, NULL, 
				&aconf->passwd, NULL))
		ircd_dlinkAddAlloc(aconf, &xline_conf_list);
	else
		free_conf(aconf);
}

/* banconf_parse_resv()
 *
 * inputs	- line to parse
 * outputs	-
 * side effects - parses the given resv line
 */
void
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
			add_to_hash(HASH_RESV, aconf->name, aconf);
		}
		else if(clean_resv_nick(aconf->name))
		{
			if(find_nick_resv(aconf->name))
				return;

			aconf->status = CONF_RESV_NICK;
			ircd_dlinkAddAlloc(aconf, &resv_conf_list);
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

/* banconf_parse()
 *
 * inputs	-
 * outputs	-
 * side effects	- parses all given ban files
 */
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
		/* iterate twice, once for normal files, once for
		 * "permanent" bans that cant be removed via ircd, with the
		 * suffix ".perm"
		 */
		while(perm < 2)
		{
			snprintf(buf, sizeof(buf), *banconf_files[i].filename);

			if(perm)
				strlcat(buf, ".perm", sizeof(buf));

			if((banfile = fopen(buf, "r")) == NULL)
			{
				/* its natural for permanent files to not exist */
				if(!perm)
				{
					ilog(L_MAIN, "Failed reading ban file %s",
						*banconf_files[i].filename);
					sendto_realops_flags(UMODE_ALL, L_ALL,
							"Can't open %s file bans could be missing!",
							*banconf_files[i].filename);
				}

				perm++;
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

