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
 *
 */

#include "stdinc.h"
#include "rsdb.h"

#define EmptyString(x) ((x == NULL) || (*(x) == '\0'))
#define CheckEmpty(x) EmptyString(x) ? "" : x
#define BT_VERSION "0.3.1"

typedef enum
{
	BANDB_KLINE,
	BANDB_KLINE_PERM,
	BANDB_DLINE,
	BANDB_DLINE_PERM,
	BANDB_XLINE,
	BANDB_XLINE_PERM,
	BANDB_RESV,
	BANDB_RESV_PERM,
	LAST_BANDB_TYPE
} bandb_type;


static char bandb_letter[LAST_BANDB_TYPE] = {
	'K', 'K', 'D', 'D', 'X', 'X', 'R', 'R'
};

static const char *bandb_table[LAST_BANDB_TYPE] = {
	"kline", "kline", "dline", "dline", "xline", "xline", "resv", "resv"
};

static const char *bandb_suffix[LAST_BANDB_TYPE] = {
	"", ".perm",
	"", ".perm",
	"", ".perm",
	"", ".perm"
};

static int parse_k_file(FILE * file, int mode, int verb, int dupes);
static int parse_x_file(FILE * file, int mode, int verb, int dupes);
static int parse_d_file(FILE * file, int mode, int verb, int dupes);
static int parse_r_file(FILE * file, int mode, int verb, int dupes);
static char *getfield(char *newline);
static void check_schema(void);
static void print_help(int i_exit);
static void wipe_schema(void);
static int drop_dupes(const char *user, const char *host, const char *t);

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
	int pretend = 0;
	int verbose = 0;
	int wipe = 0;
	int dupes = 1;		/* by default we dont allow duplicate ?-lines to be entered. */
	int opt;
	int i;

	while ((opt = getopt(argc, argv, "hipvwd")) != -1)
	{
		switch (opt)
		{
		case 'h':
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
		case 'w':
			wipe = 1;
			break;
		case 'd':
			dupes = 0;
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
		rsdb_init(NULL);
		check_schema();
		if(wipe)
		{
			dupes = 1;	/* dont check for dupes if we are wiping the db clean */
			for (i = 0; i < 3; i++)
				fprintf(stdout,
					"* WARNING: YOU ARE ABOUT TO WIPE YOUR DATABASE!\n");

			fprintf(stdout, "* Press ^C to abort! ");
			fflush(stdout);
			rb_sleep(10, 0);
			fprintf(stdout, "Carrying on...\n");
			wipe_schema();
		}
	}

	/* checking for our files to import */
	for (i = 0; i < LAST_BANDB_TYPE; i++)
	{
		rb_snprintf(conf, sizeof(conf), "%s/%s.conf%s",
				etc, bandb_table[i], bandb_suffix[i]);
		fprintf(stdout, "* checking for %s: ", conf);	/* debug  */

		/* open config for reading, or skip to the next */
		if(!(fd = fopen(conf, "r")))
		{
			fprintf(stdout, "\tmissing\n");
			err++;
			continue;
		}
		fprintf(stdout, "\tok\n");

		if(!pretend)
			rsdb_transaction(RSDB_TRANS_START);

		switch (bandb_letter[i])
		{
		case 'K':
			klines = parse_k_file(fd, pretend, verbose, dupes);
			break;
		case 'D':
			dlines = parse_d_file(fd, pretend, verbose, dupes);
			break;
		case 'X':
			xlines = parse_x_file(fd, pretend, verbose, dupes);
			break;
		case 'R':
			resvs = parse_r_file(fd, pretend, verbose, dupes);
			break;
		default:
			exit(EXIT_FAILURE);
		}

		if(!pretend)
			rsdb_transaction(RSDB_TRANS_END);

		fclose(fd);
	}

	if(err)
		fprintf(stderr, "* I was unable to locate %i config files to import.\n", err);

	fprintf(stdout, "* Import Stats: Klines: %i Dlines: %i Xlines: %i Jupes: %i \n",
		klines, dlines, xlines, resvs);

	if(pretend)
		fprintf(stdout,
			"* Pretend mode engaged. Nothing was actually entered into the database.\n");

	return 0;
}


/*
 * parse_k_file
 * Inputs       - pointer to line to parse, pretend mode?, verbosoity
 * Output       - Number parsed
 * Side Effects - Parse one new style K line
 */
int
parse_k_file(FILE * file, int mode, int verb, int dupes)
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
		{
			if(!dupes)
				drop_dupes(user_field, host_field, "kline");

			rsdb_exec(NULL, "INSERT INTO kline VALUES('%Q','%Q','%Q','%Q','%Q')",
				  user_field, host_field, oper_field, timestamp, newreason);
		}

		if(mode && verb)
			fprintf(stdout, "KLINE: user(%s@%s) oper(%s) reason(%s) time(%s)\n",
				user_field, host_field, oper_field, newreason, timestamp);

		i++;
	}
	return i;
}

static const char *
clean_gecos_field(const char *gecos)
{
	static char buf[BUFSIZE * 2];
	char *str = buf;
	
	if (gecos == NULL)
		return NULL;
	while(*gecos)
	{
		if(*gecos == ' ') 
		{
			*str++ = '\\';
			*str++ = 's';
		}	
		else
			*str++ = *gecos;
		gecos++;
	}
	*str = '\0';
	return buf;
}



int
parse_x_file(FILE * file, int mode, int verb, int dupes)
{
	const char *gecos_field = NULL;
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

		gecos_field = clean_gecos_field(getfield(line));
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
		{
			if(!dupes)
				drop_dupes(gecos_field, NULL, "xline");

			rsdb_exec(NULL, "INSERT INTO xline VALUES('%Q','%Q','%Q','%Q','%Q')",
				  gecos_field, NULL, oper_field, timestamp, newreason);
		}

		if(mode && verb)
			fprintf(stdout, "XLINE: gecos(%s) oper(%s) reason(%s) timestamp(%s)\n",
				gecos_field, oper_field, newreason, timestamp);


		i++;
	}
	return i;
}

int
parse_d_file(FILE * file, int mode, int verb, int dupes)
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
		{
			if(!dupes)
				drop_dupes(host_field, NULL, "dline");

			rsdb_exec(NULL, "INSERT INTO dline VALUES('%Q','%Q','%Q','%Q','%Q')",
				  host_field, NULL, oper_field, timestamp, newreason);
		}

		if(mode && verb)
			fprintf(stdout, "DLINE: ip(%s) oper(%s) reason(%s) timestamp(%s)\n",
				host_field, oper_field, newreason, timestamp);

		i++;
	}
	return i;
}


int
parse_r_file(FILE * file, int mode, int verb, int dupes)
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
		{
			if(!dupes)
				drop_dupes(host_field, NULL, "resv");

			rsdb_exec(NULL, "INSERT INTO resv VALUES('%Q','%Q','%Q','%Q','%Q')",
				  host_field, NULL, oper_field, timestamp, reason_field);
		}

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

/* this function completly wipes out an existing ban.db of all entries.
	* i don't yet see the point of it, but was requested. --dubkat
	*/
static void
wipe_schema(void)
{
	int i;
	rsdb_transaction(RSDB_TRANS_START);
	for (i = 0; i < LAST_BANDB_TYPE; i++)
		rsdb_exec(NULL, "DROP TABLE %s", bandb_table[i]);
	rsdb_transaction(RSDB_TRANS_END);

	check_schema();
}

int
drop_dupes(const char *user, const char *host, const char *t)
{

	/*
	 * actually looking for a duplicate before inserting is an enormous
	 * performance hit, so instead, drop the old, and insert then new.
	 * --dubkat
	 */
	/*
	   struct rsdb_table table;
	   rsdb_exec_fetch(&table,
	   "SELECT mask1,mask2 FROM '%s' WHERE mask1='%s' AND mask2='%s'",
	   t, user, host);

	   rsdb_exec_fetch_end(&table);
	 */

	rsdb_exec(NULL, "DELETE FROM %s WHERE mask1='%Q' AND mask2='%Q'", t, user, host);
	return 0;
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

	fprintf(stderr, "Usage: bantool <-i|-p> [-v] [-h] [-d] [-w] [path]\n");
	fprintf(stderr, "       -h : Display some slightly useful help.\n");
	fprintf(stderr, "       -i : Actually import configs into your database.\n");
	fprintf(stderr,
		"       -p : pretend, checks for the configs, and parses them, then tells you some data...\n");
	fprintf(stderr, "            but does not touch your database.\n");
	fprintf(stderr,
		"       -v : Be verbose... and it *is* very verbose! (intended for debugging)\n");
	fprintf(stderr, "       -d : Enable checking for redunant entries.\n");
	fprintf(stderr, "       -w : Completly wipe your database clean.\n");
	fprintf(stderr, "     path : An optional directory containing old ratbox configs.\n");
	fprintf(stderr, "            If not specified, it looks in PREFIX/etc.\n");
	exit(i_exit);
}
