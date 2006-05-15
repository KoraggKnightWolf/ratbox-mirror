/* bandb/bandb.c
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
#include "setup.h"
#include "ircd_lib.h"
#include "rsdb.h"

#define MAXPARA 10

int irc_ifd;	/* data fd */
int irc_ofd;	/* control fd */

buf_head_t sendq;
buf_head_t recvq;

typedef enum
{
	BANDB_KLINE,
	BANDB_DLINE,
	BANDB_XLINE,
	BANDB_RESV,
	LAST_BANDB_TYPE
} bandb_type;

static char bandb_letter[LAST_BANDB_TYPE] =
{
	'K', 'D', 'X', 'R'
};

static const char *bandb_table[LAST_BANDB_TYPE] = 
{
	"kline", "dline", "xline", "resv"
};

static void write_request(const char *format, ...);
static void check_schema(void);

static void
parse_ban(bandb_type type, char *parv[], int parc)
{
	const char *mask1 = NULL;
	const char *mask2 = NULL;
	const char *oper = NULL;
	const char *curtime = NULL;
	const char *reason = NULL;
	int para = 1;

	if(type == BANDB_KLINE)
	{
		if(parc != 6)
			return;
	}
	else if(parc != 5)
		return;

	mask1 = parv[para++];

	if(type == BANDB_KLINE)
		mask2 = parv[para++];

	oper = parv[para++];
	curtime = parv[para++];
	reason = parv[para++];

	rsdb_exec(NULL, "INSERT INTO %s (mask1, mask2, oper, time, reason) VALUES('%Q', '%Q', '%Q', %s, '%Q')",
			bandb_table[type], mask1, mask2 ? mask2 : "", oper, curtime, reason);
}

static void
parse_unban(bandb_type type, char *parv[], int parc)
{
	const char *mask1 = NULL;
	const char *mask2 = NULL;

	if(type == BANDB_KLINE)
	{
		if(parc != 3)
			return;
	}
	else if(parc != 2)
		return;

	mask1 = parv[1];

	if(type == BANDB_KLINE)
		mask2 = parv[2];

	rsdb_exec(NULL, "DELETE FROM %s WHERE mask1='%Q' AND mask2='%Q'",
			bandb_table[type], mask1, mask2 ? mask2 : "");
}

static void
list_bans(void)
{
	static char buf[512];
	struct rsdb_table table;
	int i, j;

	for(i = 0; i < LAST_BANDB_TYPE; i++)
	{
		rsdb_exec_fetch(&table, "SELECT mask1,mask2,oper,reason FROM %s WHERE 1",
				bandb_table[i]);

		for(j = 0; j < table.row_count; j++)
		{
			if(i == BANDB_KLINE)
				snprintf(buf, sizeof(buf), "%c %s %s %s :%s",
					bandb_letter[i], table.row[i][0],
					table.row[i][1], table.row[i][2],
					table.row[i][3]);
			else
				snprintf(buf, sizeof(buf), "%c %s %s :%s",
					bandb_letter[i], table.row[i][0], 
					table.row[i][2], table.row[i][3]);

			write_request("%s", buf);
		}
				
		rsdb_exec_fetch_end(&table);
	}
}

static void
parse_request(void)
{
	static char *parv[MAXPARA+1];
	static char readbuf[READBUF_SIZE];
	int parc;
	int len;

	while((len = ircd_linebuf_get(&recvq, readbuf, sizeof(readbuf), LINEBUF_COMPLETE, LINEBUF_PARSED)) > 0)
	{
		parc = ircd_string_to_array(readbuf, parv, MAXPARA);

		if(parc < 1)
			continue;

		switch(parv[0][0])
		{
			case 'K':
				parse_ban(BANDB_KLINE, parv, parc);
				break;

			case 'D':
				parse_ban(BANDB_DLINE, parv, parc);
				break;

			case 'X':
				parse_ban(BANDB_XLINE, parv, parc);
				break;

			case 'R':
				parse_ban(BANDB_RESV, parv, parc);
				break;

			case 'k':
				parse_unban(BANDB_KLINE, parv, parc);
				break;

			case 'd':
				parse_unban(BANDB_DLINE, parv, parc);
				break;

			case 'x':
				parse_unban(BANDB_XLINE, parv, parc);
				break;

			case 'r':
				parse_unban(BANDB_RESV, parv, parc);
				break;

			case 'L':
				list_bans();
		}
	}
}
		

static void
read_request(int fd, void *unused)
{
	static char readbuf[READBUF_SIZE];
	int length;

	while((length = ircd_read(fd, readbuf, sizeof(readbuf))) > 0)
	{
		ircd_linebuf_parse(&recvq, readbuf, length, 0);
		parse_request();
	}

	if(length == 0)
		exit(1);

	if(length == -1 && !ignoreErrno(errno))
		exit(1);

	ircd_setselect(irc_ifd, IRCD_SELECT_READ, read_request, NULL);
}

static void
read_io(void)
{
	read_request(irc_ifd, NULL);

	while(1)
	{
		ircd_select(1000);
		ircd_event_run();
	}
}

static void
write_sendq(int fd, void *unused)
{
	int retlen;

	if(ircd_linebuf_len(&sendq) > 0)
	{
		while((retlen = ircd_linebuf_flush(fd, &sendq)) > 0)
			;

		if(retlen == 0 || (retlen < 0 && !ignoreErrno(errno)))
			exit(1);
	}

	if(ircd_linebuf_len(&sendq) > 0)
		ircd_setselect(irc_ofd, IRCD_SELECT_WRITE, write_sendq, NULL);
}

static void
write_request(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	ircd_linebuf_putmsg(&sendq, format, &args, NULL);
	va_end(args);

	write_sendq(irc_ofd, NULL);
}

int
main(int argc, char *argv[])
{
	char *tifd, *tofd, *tmaxfd;
	int maxfd;
	int i;

	tifd = getenv("IFD");
	tofd = getenv("OFD");
	tmaxfd = getenv("MAXFD");

	if(tifd == NULL || tofd == NULL || tmaxfd == NULL)
	{
		fprintf(stderr, "This is ircd-ratbox bandb.  You know you aren't supposed to run me directly?\n");
		fprintf(stderr, "You can get an Id tag for this: $Id$\n");
		exit(1);
	}

	irc_ifd = (int) strtol(tifd, NULL, 10);
	irc_ofd = (int) strtol(tofd, NULL, 10);
	maxfd = (int) strtol(tmaxfd, NULL, 10);

#ifndef _WIN32
	for(i = 0; i < maxfd; i++)
	{
		if(i != irc_ifd && i != irc_ofd)
			close(i);
	}
#endif

	ircd_lib(NULL, NULL, NULL, 0, 256, 1024, 256); /* XXX fix me */

	ircd_linebuf_newbuf(&sendq);
	ircd_linebuf_newbuf(&recvq);

	ircd_open(irc_ifd, FD_PIPE, "incoming pipe");
	ircd_open(irc_ofd, FD_PIPE, "outgoing pipe");
	ircd_set_nb(irc_ifd);
	ircd_set_nb(irc_ofd);

	rsdb_init();
	check_schema();
	read_io();

	return 0;
}

static void
check_schema(void)
{
	struct rsdb_table table;
	int i;

	for(i = 0; i < LAST_BANDB_TYPE; i++)
	{
		rsdb_exec_fetch(&table, "SELECT name FROM sqlite_master WHERE type='table' AND name='%s'",
				bandb_table[i]);

		rsdb_exec_fetch_end(&table);

		if(!table.row_count)
			rsdb_exec(NULL, "CREATE TABLE %s (mask1 TEXT, mask2 TEXT, oper TEXT, time INTEGER, reason TEXT)",
					bandb_table[i]);
	}
}

