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

static pid_t bandb_pid;

static int bandb_ifd = -1;
static int bandb_ofd = -1;

static char bandb_add_letter[LAST_BANDB_TYPE] =
{
	'K', 'D', 'X', 'R'
};

dlink_list bandb_pending;

static buf_head_t bandb_sendq;
static buf_head_t bandb_recvq;

static void fork_bandb(void);

static void bandb_read(int fd, void *unused);
static void bandb_parse(void);

void
init_bandb(void)
{
	fork_bandb();

	ircd_linebuf_newbuf(&bandb_recvq);
	ircd_linebuf_newbuf(&bandb_sendq);

	if(bandb_pid < 0)
	{
		ilog(L_MAIN, "Unable to fork bandb: %s", strerror(errno));
		exit(0);
	}
}

static void
fork_bandb(void)
{
	static int fork_count = 0;
	const char *parv[2];
	char fullpath[PATH_MAX+1];
#ifdef _WIN32
	const char *suffix = ".exe";
#else
	const char *suffix = "";
#endif
	char fx[6], fy[6];
	pid_t pid;
	int ifd[2];
	int ofd[2];

	/* XXX fork_count debug */

	ircd_snprintf(fullpath, sizeof(fullpath), "%s/bandb%s", BINPATH, suffix);

	if(access(fullpath, X_OK) == -1)
	{
		ircd_snprintf(fullpath, sizeof(fullpath), "%s/bin/bandb%s", ConfigFileEntry.dpath, suffix);

		if(access(fullpath, X_OK) == -1)
		{
			ilog(L_MAIN, "Unable to execute bandb in %s or %s/bin",
				BINPATH, ConfigFileEntry.dpath);
			fork_count++;
			bandb_ifd = bandb_ofd = -1;
			return;
		}
	}

	fork_count++;

	if(bandb_ifd > 0)
		ircd_close(bandb_ifd);
	if(bandb_ofd > 0)
		ircd_close(bandb_ofd);
	if(bandb_pid > 0)
		kill(bandb_pid, SIGKILL);

	ircd_pipe(ifd, "bandb daemon - read");
	ircd_pipe(ofd, "bandb daemon - write");

	ircd_snprintf(fx, sizeof(fx), "%d", ifd[1]);
	ircd_snprintf(fy, sizeof(fy), "%d", ofd[0]);
	ircd_set_nb(ifd[0]);
	ircd_set_nb(ifd[1]);
	ircd_set_nb(ofd[0]);
	ircd_set_nb(ofd[1]);

	setenv("IFD", fy, 1);
	setenv("OFD", fx, 1);
	setenv("MAXFD", "128", 1);
	parv[0] = "-ircd bandb interface";
	parv[1] = NULL;

#ifdef _WIN32      
	SetHandleInformation((HANDLE)ifd[1], HANDLE_FLAG_INHERIT, 1);
	SetHandleInformation((HANDLE)ofd[0], HANDLE_FLAG_INHERIT, 1);
#endif

	pid = ircd_spawn_process(fullpath, (const char **)parv);

	if(pid == -1)
	{
		ilog(L_MAIN, "ircd_spawn_process failed: %s", strerror(errno));
		ircd_close(ifd[0]);
		ircd_close(ifd[1]);
		ircd_close(ofd[0]);
		ircd_close(ofd[1]);
		bandb_ifd = bandb_ofd = -1;
		return;
	}

	ircd_close(ifd[1]);
	ircd_close(ofd[0]);

	bandb_ifd = ifd[0];
	bandb_ofd = ofd[1];

	fork_count = 0;
	bandb_pid = pid;

	bandb_read(bandb_ifd, NULL);

	return;
}

static void
bandb_write_sendq(int fd, void *unused)
{
	int retlen;

	if(ircd_linebuf_len(&bandb_sendq) > 0)
	{
		while((retlen = ircd_linebuf_flush(bandb_ofd, &bandb_sendq)) > 0)
			;

		if(retlen == 0 || (retlen < 0 && !ignoreErrno(errno)))
			fork_bandb();
	}

	if(bandb_ofd < 0)
		return;

	if(ircd_linebuf_len(&bandb_sendq) > 0)
		ircd_setselect(bandb_ofd, IRCD_SELECT_WRITE, bandb_write_sendq, NULL);
}

void
bandb_write(const char *format, ...)
{
	va_list args;

	if(bandb_ifd < 0 || bandb_ofd < 0)
	{
		/* XXX error */
		return;
	}

	va_start(args, format);
	ircd_linebuf_putmsg(&bandb_sendq, format, &args, NULL);
	va_end(args);

	bandb_write_sendq(bandb_ofd, NULL);
}

static void
bandb_read(int fd, void *unused)
{
	static char buf[READBUF_SIZE];
	int length;

	while((length = ircd_read(fd, buf, sizeof(buf))) > 0)
	{
		ircd_linebuf_parse(&bandb_recvq, buf, length, 0);
		bandb_parse();
	}

	if(length == 0 || (length < 0 && !ignoreErrno(errno)))
		fork_bandb();

	if(bandb_ifd == -1)
		return;

	ircd_setselect(fd, IRCD_SELECT_READ, bandb_read, NULL);
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

	bandb_write("%s", buf);
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

	bandb_write("%s", buf);
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
	if(strstr(aconf->host, "\\s"))
	{
		char *tmp = LOCAL_COPY(aconf->host);
		char *orig = tmp;
		char *new = tmp;

		ircd_free(aconf->host);

		while(*orig)
		{
			if(*orig == '\\')
			{
				if(*(orig+1) == 's')
				{
					*new++ = ' ';
					orig += 2;
				}
				/* skip next two chars, to avoid
				 * mistaking \\s as \s
				 */
				else
				{
					*new++ = *orig++;
					*new++ = *orig++;
				}
			}
			else
				*new++ = *orig++;
		}

		*new = '\0';

		aconf->host = ircd_strdup(tmp);
	}

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
bandb_parse(void)
{
	static char buf[READBUF_SIZE];
	char *parv[MAXPARA+1];
	int len, parc;

	while((len = ircd_linebuf_get(&bandb_recvq, buf, sizeof(buf),
					LINEBUF_COMPLETE, LINEBUF_PARSED)) > 0)
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
