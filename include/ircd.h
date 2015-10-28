/*
 *  ircd-ratbox: A slightly useful ircd.
 *  ircd.h: A header for the ircd startup routines.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002-2012 ircd-ratbox development team
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 *  USA
 *
 *  $Id$
 */

#ifndef INCLUDED_ircd_h
#define INCLUDED_ircd_h

struct SetOptions
{
	unsigned int maxclients;		/* max clients allowed */
	unsigned int autoconn;		/* autoconn enabled for all servers? */

	unsigned int floodcount;		/* Number of messages in 1 second */
	unsigned int ident_timeout;	/* timeout for identd lookups */

	unsigned int spam_num;
	unsigned int spam_time;

	char operstring[REALLEN];
	char adminstring[REALLEN];
};

struct Counter
{
	unsigned long oper;		/* Opers */
	unsigned long total;		/* total clients */
	unsigned long invisi;		/* invisible clients */
	unsigned long max_loc;		/* MAX local clients */
	unsigned long max_tot;		/* MAX global clients */
	unsigned long totalrestartcount;	/* Total client count ever */
};

extern struct SetOptions GlobalSetOptions;	/* defined in ircd.c */

extern const char *logFileName;
extern const char *pidFileName;

extern bool dorehash;
extern bool dorehashbans;
extern bool doremotd;
extern bool kline_queued;
extern bool server_state_foreground;

extern struct Client me;
extern rb_dlink_list global_client_list;
extern struct Client *local[];
extern struct Counter Count;

extern int default_server_capabs;

extern time_t startup_time;
extern bool splitmode;
extern bool splitchecking;
extern int split_users;
extern int split_servers;
extern int eob_count;

extern rb_dlink_list unknown_list;
extern rb_dlink_list lclient_list;
extern rb_dlink_list serv_list;
extern rb_dlink_list global_serv_list;
extern rb_dlink_list oper_list;

void get_current_bandwidth(struct Client *source_p, struct Client *target_p);

void ircd_shutdown(const char *reason) RB_noreturn;

uintptr_t get_maxrss(void);

int ratbox_main(int argc, char **argv);
extern bool testing_conf;
extern bool conf_parse_failure;
extern int maxconnections;
extern bool ircd_ssl_ok;
extern bool zlib_ok;

void restart(const char *) RB_noreturn;
void server_reboot(void) RB_noreturn;

#endif
