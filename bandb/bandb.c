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


static ircd_helper *bandb_helper;

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

	/* schedule a clear of anything already pending */
	ircd_helper_write(bandb_helper, "C");

	for(i = 0; i < LAST_BANDB_TYPE; i++)
	{
		rsdb_exec_fetch(&table, "SELECT mask1,mask2,oper,reason FROM %s WHERE 1",
				bandb_table[i]);

		for(j = 0; j < table.row_count; j++)
		{
			if(i == BANDB_KLINE)
				snprintf(buf, sizeof(buf), "%c %s %s %s :%s",
					bandb_letter[i], table.row[j][0],
					table.row[j][1], table.row[j][2],
					table.row[j][3]);
			else
				snprintf(buf, sizeof(buf), "%c %s %s :%s",
					bandb_letter[i], table.row[j][0], 
					table.row[j][2], table.row[j][3]);

			ircd_helper_write(bandb_helper, "%s", buf);
		}
				
		rsdb_exec_fetch_end(&table);
	}

	ircd_helper_write(bandb_helper, "F");
}

static void
parse_request(ircd_helper *helper)
{
	static char *parv[MAXPARA+1];
	static char readbuf[READBUF_SIZE];
	int parc;
	int len;

		
	while((len = ircd_helper_readline(helper, readbuf, sizeof(readbuf))) > 0)
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
				break;
			default:
				break;
		}
	}
}
		

static void
read_io(void)
{
	ircd_helper_read(bandb_helper->ifd, bandb_helper);

	while(1)
	{
		ircd_select(1000);
		ircd_event_run();
	}
}

static void
error_cb(ircd_helper *helper)
{
	exit(1);
}

int
main(int argc, char *argv[])
{

	bandb_helper = ircd_helper_child(parse_request, error_cb, NULL, NULL, NULL, 256, 1024, 256, 256); /* XXX fix me */

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

