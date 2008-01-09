/*
 *  ircd-ratbox: A slightly useful ircd.
 *  bantool.c: import legacy ircd-ratbox kline configs to ratbox-3 database.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002-2008 ircd-ratbox development team
 *  Copyright (C) 2008 Daniel J Reidy <dubkat@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 *
 *  $Id$
 *
 *
 * Thanks to efnet.port80.se and irc.igs.ca for their assistance 
 * during creation of this tool. and AndroSyn for putting up with
 * never-ending questions.
 *
 * BUGS none that i'm aware of.
 * TODO prevent duplicates.
 *
 */

#include <ratbox_lib.h>
#include "stdinc.h"
#include "common.h"
#include "setup.h"
#include "config.h"
#include "rsdb.h"
#include "bantool.h"

int
main(int argc, char *argv[])
{
	FILE *fd;
	char etc[PATH_MAX];
	char conf[PATH_MAX];
	int err = 0;
	int i_didnt_use_a_flag = 1;
	int klines = 0;
	int dlines = 0;
	int xlines = 0;
	int resvs = 0;
	int opt;
	int pretend = 1;
	int verbose = 0;

	while ((opt = getopt(argc, argv, "hipv")) != -1)
	{
		switch (opt)
		{
		case 'h':
			i_didnt_use_a_flag = 0;
			print_help(EXIT_SUCCESS);
			break;
		case 'i':
			i_didnt_use_a_flag = 0;
			pretend = 0;
			break;
		case 'p':
			i_didnt_use_a_flag = 0;
			pretend = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		default:	/* '?' */
			print_help(EXIT_FAILURE);
		}
	}
	/* they should really read the help. */
	if(i_didnt_use_a_flag)
		print_help(EXIT_FAILURE);

	if(argv[optind] != NULL)
		rb_strlcpy(etc, argv[optind], sizeof(etc));
	else
		rb_strlcpy(etc, ETCPATH, sizeof(ETCPATH));

	if(!pretend)
	{
		rsdb_init();
		check_schema();
	}

	/* checking for our files to import */
	int i;
	for (i = 0; i < 4; i++)
	{
		rb_snprintf(conf, sizeof(conf), "%s/%s.conf", etc, bandb_table[i]);
		fprintf(stdout, "checking for %s: ", conf);	/* debug  */

		if(access(conf, R_OK) == -1)
		{
			fprintf(stdout, "\tMISSING!\n");
			err++;
			continue;
		}
		fprintf(stdout, "\tok!\n");

		/* Open Seseme */
		if(!(fd = fopen(conf, "r")))
		{
			/* fprintf(stderr, "\tParse Failed.\n", conf); */
			continue;
		}
		if(!pretend)
			rsdb_transaction(RSDB_TRANS_START);

		switch (bandb_letter[i])
		{
		case 'K':
			klines = parse_k_file(fd, pretend, verbose);
			break;
		case 'D':
			dlines = parse_d_file(fd, pretend, verbose);
			break;
		case 'X':
			xlines = parse_x_file(fd, pretend, verbose);
			break;
		case 'R':
			resvs = parse_r_file(fd, pretend, verbose);
			break;
		default:
			exit(EXIT_FAILURE);
		}

		if(!pretend)
			rsdb_transaction(RSDB_TRANS_END);

		fclose(fd);
	}

	if(err)
		fprintf(stderr, "I was unable to locate %i config files to import.\n", err);

	fprintf(stdout, "Import Stats: Klines: %i Dlines: %i Xlines: %i Jupes: %i \n",
		klines, dlines, xlines, resvs);

	if(pretend)
		fprintf(stdout,
			"pretend mode engaged. nothing was actually entered into the database.\n");

	return 0;
}


/*
 * parse_k_file
 * Inputs       - pointer to line to parse
 * Output       - NONE
 * Side Effects - Parse one new style K line
 */

int
parse_k_file(FILE * file, int mode, int verb)
{
	char *user_field = NULL;
	char *reason_field = NULL;
	char *operreason_field = NULL;
	char newreason[REASONLEN];
	char *host_field = NULL;
	char *oper_field = NULL;
	char *timestamp = NULL;
	char line[BUFSIZE];
	int i = 0;
	char *p;

	while (fgets(line, sizeof(line), file))
	{
		if((p = strpbrk(line, "\r\n")) != NULL)
			*p = '\0';

		if((*line == '\0') || (*line == '#'))
			continue;

		user_field = getfield(line);
		if(EmptyString(user_field))
			continue;

		host_field = getfield(NULL);
		if(EmptyString(host_field))
			continue;

		reason_field = getfield(NULL);
		if(EmptyString(reason_field))
			continue;

		operreason_field = getfield(NULL);
		getfield(NULL);
		oper_field = getfield(NULL);
		timestamp = oper_field + strlen(oper_field) + 2;

		/* append operreason_field to reason_field */
		if(!EmptyString(operreason_field))
			rb_snprintf(newreason, sizeof(newreason), "%s | %s", reason_field,
				    operreason_field);
		else
			rb_snprintf(newreason, sizeof(newreason), "%s", reason_field);

		if(!mode)
			rsdb_exec(NULL, "INSERT INTO kline VALUES('%Q','%Q','%Q','%Q','%Q')",
				  user_field, host_field, oper_field, timestamp, newreason);

		if(mode && verb)
			fprintf(stdout, "KLINE: user(%s@%s) oper(%s) reason(%s) time(%s)\n",
				user_field, host_field, oper_field, newreason, timestamp);

		i++;
	}
	return i;
}


int
parse_x_file(FILE * file, int mode, int verb)
{
	char *gecos_field = NULL;
	char *reason_field = NULL;
	char newreason[REASONLEN];
	char *oper_field = NULL;
	char *operreason_field = NULL;
	char *timestamp = NULL;
	char line[BUFSIZE];
	char *p;
	int i = 0;

	while (fgets(line, sizeof(line), file))
	{
		if((p = strpbrk(line, "\r\n")))
			*p = '\0';

		if((*line == '\0') || (line[0] == '#'))
			continue;

		gecos_field = getfield(line);
		if(EmptyString(gecos_field))
			continue;

		/* field for xline types, which no longer exist */
		getfield(NULL);

		reason_field = getfield(NULL);
		if(EmptyString(reason_field))
			continue;

		oper_field = getfield(NULL);
		timestamp = oper_field + strlen(oper_field) + 2;

		/* append operreason_field to reason_field */
		if(!EmptyString(operreason_field))
			rb_snprintf(newreason, sizeof(newreason), "%s | %s", reason_field,
				    operreason_field);
		else
			rb_snprintf(newreason, sizeof(newreason), "%s", reason_field);

		if(!mode)
			rsdb_exec(NULL, "INSERT INTO xline VALUES('%Q','%Q','%Q','%Q','%Q')",
				  gecos_field, NULL, oper_field, timestamp, newreason);

		if(mode && verb)
			fprintf(stdout, "XLINE: gecos(%s) oper(%s) reason(%s) timestamp(%s)\n",
				gecos_field, oper_field, newreason, timestamp);


		i++;
	}
	return i;
}

/*
 * parse_d_file
 * Inputs       - pointer to line to parse
 * Output       - NONE
 * Side Effects - Parse one new style D line
 */

int
parse_d_file(FILE * file, int mode, int verb)
{
	char *reason_field = NULL;
	char newreason[REASONLEN];
	char *host_field = NULL;
	char *oper_field = NULL;
	char *operreason_field = NULL;
	char *timestamp = NULL;
	char line[BUFSIZE];
	char *p;
	int i = 0;

	while (fgets(line, sizeof(line), file))
	{
		if((p = strpbrk(line, "\r\n")))
			*p = '\0';

		if((*line == '\0') || (line[0] == '#'))
			continue;

		host_field = getfield(line);
		if(EmptyString(host_field))
			continue;

		reason_field = getfield(NULL);
		if(EmptyString(reason_field))
			continue;

		operreason_field = getfield(NULL);
		getfield(NULL);
		oper_field = getfield(NULL);
		timestamp = oper_field + strlen(oper_field) + 2;

		/* append operreason_field to reason_field */
		if(!EmptyString(operreason_field))
			rb_snprintf(newreason, sizeof(newreason), "%s | %s", reason_field,
				    operreason_field);
		else
			rb_snprintf(newreason, sizeof(newreason), "%s", reason_field);

		if(!mode)
			rsdb_exec(NULL, "INSERT INTO dline VALUES('%Q','%Q','%Q','%Q','%Q')",
				  host_field, NULL, oper_field, timestamp, newreason);

		if(mode && verb)
			fprintf(stdout, "DLINE: ip(%s) oper(%s) reason(%s) timestamp(%s)\n",
				host_field, oper_field, newreason, timestamp);

		i++;
	}
	return i;
}


int
parse_r_file(FILE * file, int mode, int verb)
{
	char *reason_field;
	char *host_field;
	char line[BUFSIZE];
	char *oper_field = NULL;
	char *timestamp = NULL;
	int i = 0;
	char *p;

	while (fgets(line, sizeof(line), file))
	{
		if((p = strpbrk(line, "\r\n")))
			*p = '\0';

		if((*line == '\0') || (line[0] == '#'))
			continue;

		host_field = getfield(line);
		if(EmptyString(host_field))
			continue;

		reason_field = getfield(NULL);
		if(EmptyString(reason_field))
			continue;

		oper_field = getfield(NULL);
		timestamp = oper_field + strlen(oper_field) + 2;

		if(!mode)
			rsdb_exec(NULL, "INSERT INTO resv VALUES('%Q','%Q','%Q','%Q','%Q')",
				  host_field, NULL, oper_field, timestamp, reason_field);

		if(mode && verb)
			fprintf(stdout, "JUPE: mask(%s) reason(%s) oper(%s) timestamp(%s)\n",
				host_field, reason_field, oper_field, timestamp);

		i++;
	}
	return i;
}


/*
 * getfield
 *
 * inputs	- input buffer
 * output	- next field
 * side effects	- field breakup for ircd.conf file.
 */
char *
getfield(char *newline)
{
	static char *line = NULL;
	char *end, *field;

	if(newline != NULL)
		line = newline;

	if(line == NULL)
		return (NULL);

	field = line;

	/* XXX make this skip to first " if present */
	if(*field == '"')
		field++;
	else
		return (NULL);	/* mal-formed field */

	end = strchr(line, ',');

	while (1)
	{
		/* no trailing , - last field */
		if(end == NULL)
		{
			end = line + strlen(line);
			line = NULL;

			if(*end == '"')
			{
				*end = '\0';
				return field;
			}
			else
				return NULL;
		}
		else
		{
			/* look for a ", to mark the end of a field.. */
			if(*(end - 1) == '"')
			{
				line = end + 1;
				end--;
				*end = '\0';
				return field;
			}

			/* search for the next ',' */
			end++;
			end = strchr(end, ',');
		}
	}

	return NULL;
}

static void
check_schema(void)
{
	struct rsdb_table table;
	int i;

	for (i = 0; i < LAST_BANDB_TYPE; i++)
	{
		rsdb_exec_fetch(&table,
				"SELECT name FROM sqlite_master WHERE type='table' AND name='%s'",
				bandb_table[i]);

		rsdb_exec_fetch_end(&table);

		if(!table.row_count)
			rsdb_exec(NULL,
				  "CREATE TABLE %s (mask1 TEXT, mask2 TEXT, oper TEXT, time INTEGER, reason TEXT)",
				  bandb_table[i]);
	}
}


void
print_help(int i_exit)
{
	char version[10];
	rb_strlcpy(version, BT_VERSION, sizeof(version));

	fprintf(stderr, "bantool v.%s (C) 2008 Daniel J Reidy <dubkat@gmail.com>\n", version);
	fprintf(stderr, "$Id$\n\n");
	fprintf(stderr, "This program is distributed in the hope that it will be useful,\n"
		"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
		"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
		"GNU General Public License for more details.\n\n");

	fprintf(stderr, "Usage: bantool <-i|-p> [-v] [-h] [path]\n");
	fprintf(stderr, "       -h : display some slightly useful help.\n");
	fprintf(stderr, "       -i : actually import configs into your database.\n");
	fprintf(stderr,
		"       -p : pretend, checks for the configs, and parses them, then tells you some data...\n");
	fprintf(stderr, "          : but does not touch your database.\n");
	fprintf(stderr,
		"       -v : be verbose... and it *is* very verbose! (intended for debugging)\n");
	fprintf(stderr, "     path : an optional directory containing old ratbox configs.\n");
	fprintf(stderr, "            If not specified, it looks in PREFIX/etc.\n");
	exit(i_exit);
}
