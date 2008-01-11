/*
 *  ircd-ratbox: A slightly useful ircd.
 *  bantool.h: import legacy ircd-ratbox kline configs.
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
	*/

#define EmptyString(x) ((x == NULL) || (*(x) == '\0'))
#define CheckEmpty(x) EmptyString(x) ? "" : x
#define BT_VERSION "0.3"

typedef enum
{
	BANDB_KLINE,
	BANDB_DLINE,
	BANDB_XLINE,
	BANDB_RESV,
	LAST_BANDB_TYPE
} bandb_type;


static char bandb_letter[LAST_BANDB_TYPE] = {
	'K', 'D', 'X', 'R'
};

static const char *bandb_table[LAST_BANDB_TYPE] = {
	"kline", "dline", "xline", "resv"
};

static int parse_k_file(FILE * file, int mode, int verb, int dupes);
static int parse_x_file(FILE * file, int mode, int verb, int dupes);
static int parse_d_file(FILE * file, int mode, int verb, int dupes);
static int parse_r_file(FILE * file, int mode, int verb, int dupes);
static char *getfield(char *newline);
static void check_schema(void);
static void print_help(int i_exit);
static void wipe_schema(void);
static int drop_dupes(char user[], char host[], char t[]);
