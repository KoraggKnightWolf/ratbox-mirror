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
#include "s_conf.h"
#include "s_log.h"
#include "match.h"
#include "bandbi.h"

static pid_t bandb_pid;

static int bandb_ifd = -1;
static int bandb_ofd = -1;

static buf_head_t bandb_sendq;
static buf_head_t bandb_recvq;

static void fork_bandb(void);

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

static char bandb_add_letter[LAST_BANDB_TYPE] =
{
	'K', 'D', 'X', 'R'
};

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
