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

/* banconf_parse_line()
 *
 * inputs	- line to take fields from, locations for storage
 * outputs	- 1 on success, otherwise 0
 * side effects	- parses given line generically according to passed fields
 */
static int
banconf_parse_line(char *line, char ***params, int *quoted, int parcount)
{
	static const char *empty = "";
	char *tmp;
	int i;

	for(i = 0; i < parcount; i++)
	{
		/* skip field */
		if(params[i] == NULL)
			continue;

		tmp = banconf_parse_field(i ? NULL : line, quoted[i]);

		if(tmp == NULL)
			return 0;

		if(*tmp == '\0')
			*params[i] = NULL;
		else
			DupString(*params[i], tmp);
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
banconf_parse_kline(char *line, int perm)
{
	struct ConfItem *aconf;
	char **params[7];
	int quoted[7] = { 1, 1, 1, 1, 1, 1, 0 };

	/* user,host,reason,operreason,humandate,oper,timestamp */

	aconf = make_conf();
	aconf->status = CONF_KILL;

	if(perm)
		aconf->flags |= CONF_FLAGS_PERMANENT;

	params[0] = &aconf->user;
	params[1] = &aconf->host;
	params[2] = &aconf->passwd;
	params[3] = &aconf->spasswd;

	if(banconf_parse_line(line, params, quoted, 4))
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
banconf_parse_dline(char *line, int perm)
{
	struct ConfItem *aconf;
	char **params[6];
	int quoted[6] = { 1, 1, 1, 1, 1, 0 };

	/* host,reason,operreason,humandate,oper,timestamp */

	aconf = make_conf();
	aconf->status = CONF_DLINE;

	if(perm)
		aconf->flags |= CONF_FLAGS_PERMANENT;

	params[0] = &aconf->host;
	params[1] = &aconf->passwd;
	params[2] = &aconf->spasswd;

	if(banconf_parse_line(line, params, quoted, 3))
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
banconf_parse_xline(char *line, int perm)
{
	struct ConfItem *aconf;
	char **params[5];
	int quoted[5] = { 1, 1, 1, 1, 0 };

	/* gecos,type,reason,oper,timestamp */

	aconf = make_conf();
	aconf->status = CONF_XLINE;

	if(perm)
		aconf->flags |= CONF_FLAGS_PERMANENT;

	params[0] = &aconf->host;
	params[1] = NULL;		/* skip unused type */
	params[2] = &aconf->passwd;

	if(banconf_parse_line(line, params, quoted, 3))
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
banconf_parse_resv(char *line, int perm)
{
	struct ConfItem *aconf;
	char **params[4];
	int quoted[4] = { 1, 1, 1, 0 };

	/* resv,reason,oper,timestamp */

	aconf = make_conf();

	if(perm)
		aconf->flags |= CONF_FLAGS_PERMANENT;

	params[0] = &aconf->host;
	params[1] =  &aconf->passwd;

	if(banconf_parse_line(line, params, quoted, 2))
	{
		if(IsChannelName(aconf->host))
		{
			if(!hash_find_resv(aconf->host))
			{
				aconf->status = CONF_RESV_CHANNEL;
				add_to_hash(HASH_RESV, aconf->host, aconf);
				return;
			}
		}
		else if(clean_resv_nick(aconf->host))
		{
			if(!find_nick_resv(aconf->host))
			{
				aconf->status = CONF_RESV_NICK;
				ircd_dlinkAddAlloc(aconf, &resv_conf_list);
				return;
			}
		}
	}

	/* invalid */
	free_conf(aconf);
}

static struct banconf_file
{
	const char **filename;
	void (*func) (char *, int);
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
	int perm;

	while(banconf_files[i].filename)
	{
		/* iterate twice, once for normal files, once for
		 * "permanent" bans that cant be removed via ircd, with the
		 * suffix ".perm"
		 */
		for(perm = 0; perm < 2; perm++)
		{
			ircd_snprintf(buf, sizeof(buf), *banconf_files[i].filename);

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

				continue;
			}


			while(fgets(line, sizeof(line), banfile))
			{
				if((p = strchr(line, '\n')))
					*p = '\0';

				if(EmptyString(line) || (*line == '#'))
					continue;

				(banconf_files[i].func)(line, perm);
			}

			fclose(banfile);
		}

		i++;
	}
}

