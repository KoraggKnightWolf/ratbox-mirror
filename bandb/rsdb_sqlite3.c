/* src/rsdb_sqlite.h
 *   Contains the code for the sqlite database backend.
 *
 * Copyright (C) 2003-2006 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2006 ircd-ratbox development team
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
 * $Id: rsdb_sqlite3.c 22247 2006-03-24 23:39:15Z leeh $
 */
#include "stdinc.h"
#include "rsdb.h"

#include <sqlite3.h>

struct sqlite3 *ircd_bandb;

void
rsdb_init(void)
{
	if(sqlite3_open(DBPATH, &ircd_bandb))
	{
		exit(1);
	}
}

void
rsdb_shutdown(void)
{
	if(ircd_bandb)
		sqlite3_close(ircd_bandb);
}

const char *
rsdb_quote(const char *src)
{
	static char buf[BUFSIZE*4];
	char *p = buf;

	/* cheap and dirty length check.. */
	if(strlen(src) >= (sizeof(buf) / 2))
		return NULL;

	while(*src)
	{
		if(*src == '\'')
			*p++ = '\'';

		*p++ = *src++;
	}

	*p = '\0';
	return buf;
}

static int
rsdb_callback_func(void *cbfunc, int argc, char **argv, char **colnames)
{
	rsdb_callback cb = cbfunc;
	(cb)(argc, (const char **) argv);
	return 0;
}

void
rsdb_exec(rsdb_callback cb, const char *format, ...)
{
	static char buf[BUFSIZE*4];
	va_list args;
	char *errmsg;
	unsigned int i;

	va_start(args, format);
	i = rs_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(i >= sizeof(buf))
	{
//		mlog("fatal error: length problem with compiling sql");
		exit(1);
	}

	if((i = sqlite3_exec(ircd_bandb, buf, (cb ? rsdb_callback_func : NULL), cb, &errmsg)))
	{
//		mlog("fatal error: problem with db file: %s", errmsg);
		exit(1);
	}
}

void
rsdb_exec_fetch(struct rsdb_table *table, const char *format, ...)
{
	static char buf[BUFSIZE*4];
	va_list args;
	char *errmsg;
	char **data;
	int pos;
	unsigned int retlen;
	int i, j;

	va_start(args, format);
	retlen = rs_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if(retlen >= sizeof(buf))
	{
//		mlog("fatal error: length problem with compiling sql");
		exit(1);
	}

	if(sqlite3_get_table(ircd_bandb, buf, &data, &table->row_count, &table->col_count, &errmsg))
	{
//		mlog("fatal error: problem with db file: %s", errmsg);
		exit(1);
	}

	/* we need to be able to free data afterward */
	table->arg = data;

	if(table->row_count == 0)
	{
		table->row = NULL;
		return;
	}

	/* sqlite puts the column names as the first row */
	pos = table->col_count;
	table->row = ircd_malloc(sizeof(char **) * table->row_count);
	for(i = 0; i < table->row_count; i++)
	{
		table->row[i] = ircd_malloc(sizeof(char *) * table->col_count);

		for(j = 0; j < table->col_count; j++)
		{
			table->row[i][j] = data[pos++];
		}
	}
}

void
rsdb_exec_fetch_end(struct rsdb_table *table)
{
	int i;

	for(i = 0; i < table->row_count; i++)
	{
		ircd_free(table->row[i]);
	}
	ircd_free(table->row);

	sqlite3_free_table((char **) table->arg);
}

void
rsdb_transaction(rsdb_transtype type)
{
	if(type == RSDB_TRANS_START)
		rsdb_exec(NULL, "BEGIN TRANSACTION");
	else if(type == RSDB_TRANS_END)
		rsdb_exec(NULL, "COMMIT TRANSACTION");
}

