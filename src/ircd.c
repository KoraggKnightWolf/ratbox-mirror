/*
 *  ircd-ratbox: A slightly useful ircd.
 *  ircd.c: Starts up and runs the ircd.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002-2005 ircd-ratbox development team
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
 */

#include "stdinc.h"
#include "tools.h"
#include "struct.h"
#include "ircd.h"
#include "channel.h"
#include "class.h"
#include "linebuf.h"
#include "client.h"
#include "event.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd_signal.h"
#include "ircd_lib.h"
#include "hostmask.h"
#include "numeric.h"
#include "parse.h"
#include "restart.h"
#include "s_auth.h"
#include "commio.h"
#include "s_conf.h"
#include "s_log.h"
#include "s_serv.h"		/* try_connections */
#include "s_stats.h"
#include "scache.h"
#include "send.h"
#include "whowas.h"
#include "hook.h"
#include "modules.h"
#include "ircd_getopt.h"
#include "balloc.h"
#include "newconf.h"
#include "patricia.h"
#include "reject.h"
#include "s_conf.h"
#include "s_newconf.h"
#include "cache.h"
#include "monitor.h"
#include "res.h"

/*
 * Try and find the correct name to use with getrlimit() for setting the max.
 * number of files allowed to be open by this process.
 */

/* /quote set variables */
struct SetOptions GlobalSetOptions;

/* configuration set from ircd.conf */
struct config_file_entry ConfigFileEntry;
/* server info set from ircd.conf */
struct server_info ServerInfo;
/* admin info set from ircd.conf */
struct admin_info AdminInfo;

struct Counter Count;
struct ServerStatistics ServerStats;

struct timeval SystemTime;
int ServerRunning;		/* GLOBAL - server execution state */
struct Client me;		/* That's me */
struct LocalUser meLocalUser;	/* That's also part of me */

dlink_list global_client_list;

/* unknown/client pointer lists */
dlink_list unknown_list;	/* unknown clients ON this server only */
dlink_list lclient_list;	/* local clients only ON this server */
dlink_list serv_list;		/* local servers to this server ONLY */
dlink_list global_serv_list;	/* global servers on the network */
dlink_list oper_list;		/* our opers, duplicated in lclient_list */

static unsigned long initialVMTop = 0;	/* top of virtual memory at init */
const char *logFileName = LPATH;
const char *pidFileName = PPATH;

char **myargv;
int dorehash = 0;
int dorehashbans = 0;
int doremotd = 0;
time_t nextconnect = 1;		/* time for next try_connections call */
int kline_queued = 0;
int server_state_foreground = 0;

int testing_conf = 0;

time_t startup_time;

/* Set to zero because it should be initialized later using
 * initialize_server_capabs
 */
int default_server_capabs = CAP_MASK;

int splitmode;
int splitchecking;
int split_users;
int split_servers;
int eob_count;

/*
 * get_vm_top - get the operating systems notion of the resident set size
 */
static unsigned long
get_vm_top(void)
{
	/*
	 * NOTE: sbrk is not part of the ANSI C library or the POSIX.1 standard
	 * however it seems that everyone defines it. Calling sbrk with a 0
	 * argument will return a pointer to the top of the process virtual
	 * memory without changing the process size, so this call should be
	 * reasonably safe (sbrk returns the new value for the top of memory).
	 * This code relies on the notion that the address returned will be an 
	 * offset from 0 (NULL), so the result of sbrk is cast to a size_t and 
	 * returned. We really shouldn't be using it here but...
	 */
	void *vptr = sbrk(0);
	return (unsigned long) vptr;
}

/*
 * get_maxrss - get the operating systems notion of the resident set size
 */
unsigned long
get_maxrss(void)
{
	return get_vm_top() - initialVMTop;
}

/*
 * print_startup - print startup information
 */
static void
print_startup(int pid)
{
	printf("ircd: version %s\n", ircd_version);
	printf("ircd: pid %d\n", pid);
	printf("ircd: running in %s mode from %s\n",
	       !server_state_foreground ? "background" : "foreground",
		ConfigFileEntry.dpath);
}

/*
 * init_sys
 *
 * inputs	- boot_daemon flag
 * output	- none
 * side effects	- if boot_daemon flag is not set, don't daemonize
 */
static void
init_sys(void)
{
#if defined(RLIMIT_FD_MAX) && defined(HAVE_SYS_RLIMIT_H)
	struct rlimit limit;

	if(!getrlimit(RLIMIT_FD_MAX, &limit))
	{

		if(limit.rlim_max < MAXCONNECTIONS)
		{
			fprintf(stderr, "ircd fd table too big\n");
			fprintf(stderr, "Hard Limit: %ld IRC max: %d\n",
				(long) limit.rlim_max, MAXCONNECTIONS);
			fprintf(stderr, "Fix MAXCONNECTIONS\n");
			exit(-1);
		}

		limit.rlim_cur = limit.rlim_max;	/* make soft limit the max */
		if(setrlimit(RLIMIT_FD_MAX, &limit) == -1)
		{
			fprintf(stderr, "error setting max fd's to %ld\n", (long) limit.rlim_cur);
			exit(EXIT_FAILURE);
		}
	}
#endif /* RLIMIT_FD_MAX */
}

static int
make_daemon(void)
{
	int pid;

	if((pid = fork()) < 0)
	{
		perror("fork");
		exit(EXIT_FAILURE);
	}
	else if(pid > 0)
	{
		print_startup(pid);
		exit(EXIT_SUCCESS);
	}

	setsid();
	/*  fclose(stdin);
	   fclose(stdout);
	   fclose(stderr); */

	return 0;
}

static int printVersion = 0;

struct lgetopt myopts[] = {
	{"dlinefile", &ConfigFileEntry.dlinefile,
	 STRING, "File to use for dlines.conf"},
	{"configfile", &ConfigFileEntry.configfile,
	 STRING, "File to use for ircd.conf"},
	{"klinefile", &ConfigFileEntry.klinefile,
	 STRING, "File to use for klines.conf"},
	{"xlinefile", &ConfigFileEntry.xlinefile,
	 STRING, "File to use for xlines.conf"},
	{"resvfile", &ConfigFileEntry.resvfile,
	 STRING, "File to use for resv.conf"},
	{"logfile", &logFileName,
	 STRING, "File to use for ircd.log"},
	{"pidfile", &pidFileName,
	 STRING, "File to use for process ID"},
	{"foreground", &server_state_foreground,
	 YESNO, "Run in foreground (don't detach)"},
	{"version", &printVersion,
	 YESNO, "Print version and exit"},
	{"conftest", &testing_conf,
	 YESNO, "Test the configuration files and exit"},
	{"help", NULL, USAGE, "Print this text"},
	{NULL, NULL, STRING, NULL},
};


static void
io_loop(void)
{
	while (ServerRunning)

	{
		/* Run pending events, then get the number of seconds to the next
		 * event
		 */

		eventRun();

		comm_select(250);

		/*
		 * Check to see whether we have to rehash the configuration ..
		 */
		if(dorehash)
		{
			rehash(1);
			dorehash = 0;
		}
		if(dorehashbans)
		{
			rehash_bans(1);
			dorehashbans = 0;
		}
		if(doremotd)
		{
			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "Got signal SIGUSR1, reloading ircd motd file");
			free_cachefile(user_motd);
			user_motd = cache_file(MPATH, "ircd.motd", 0);
			doremotd = 0;
		}
	}
}

/*
 * initalialize_global_set_options
 *
 * inputs       - none
 * output       - none
 * side effects - This sets all global set options needed 
 */
static void
initialize_global_set_options(void)
{
	memset(&GlobalSetOptions, 0, sizeof(GlobalSetOptions));
	/* memset( &ConfigFileEntry, 0, sizeof(ConfigFileEntry)); */

	GlobalSetOptions.maxclients = MAX_CLIENTS;
	GlobalSetOptions.autoconn = 1;

	GlobalSetOptions.spam_time = MIN_JOIN_LEAVE_TIME;
	GlobalSetOptions.spam_num = MAX_JOIN_LEAVE_COUNT;

	if(ConfigFileEntry.default_floodcount)
		GlobalSetOptions.floodcount = ConfigFileEntry.default_floodcount;
	else
		GlobalSetOptions.floodcount = 10;

	split_servers = ConfigChannel.default_split_server_count;
	split_users = ConfigChannel.default_split_user_count;

	if(split_users && split_servers
	   && (ConfigChannel.no_create_on_split || ConfigChannel.no_join_on_split))
	{
		splitmode = 1;
		splitchecking = 1;
	}

	GlobalSetOptions.ident_timeout = IDENT_TIMEOUT;

	strlcpy(GlobalSetOptions.operstring,
		ConfigFileEntry.default_operstring,
		sizeof(GlobalSetOptions.operstring));
	strlcpy(GlobalSetOptions.adminstring,
		ConfigFileEntry.default_adminstring,
		sizeof(GlobalSetOptions.adminstring));

	/* memset( &ConfigChannel, 0, sizeof(ConfigChannel)); */

	/* End of global set options */

}

/*
 * initialize_server_capabs
 *
 * inputs       - none
 * output       - none
 */
static void
initialize_server_capabs(void)
{
	default_server_capabs &= ~CAP_ZIP;
}


/*
 * write_pidfile
 *
 * inputs       - filename+path of pid file
 * output       - none
 * side effects - write the pid of the ircd to filename
 */
static void
write_pidfile(const char *filename)
{
	FILE *fb;
	char buff[32];
	if((fb = fopen(filename, "w")))
	{
		unsigned int pid = (unsigned int) getpid();

		ircsnprintf(buff, sizeof(buff), "%u\n", pid);
		if((fputs(buff, fb) == -1))
		{
			ilog(L_MAIN, "Error writing %u to pid file %s (%s)",
			     pid, filename, strerror(errno));
		}
		fclose(fb);
		return;
	}
	else
	{
		ilog(L_MAIN, "Error opening pid file %s", filename);
	}
}

/*
 * check_pidfile
 *
 * inputs       - filename+path of pid file
 * output       - none
 * side effects - reads pid from pidfile and checks if ircd is in process
 *                list. if it is, gracefully exits
 * -kre
 */
static void
check_pidfile(const char *filename)
{
	FILE *fb;
	char buff[32];
	pid_t pidfromfile;

	/* Don't do logging here, since we don't have log() initialised */
	if((fb = fopen(filename, "r")))
	{
		if(fgets(buff, 20, fb) != NULL)
		{
			pidfromfile = atoi(buff);
			if(!kill(pidfromfile, 0))
			{
				printf("ircd: daemon is already running\n");
				exit(-1);
			}
		}
		fclose(fb);
	}
}

/*
 * setup_corefile
 *
 * inputs       - nothing
 * output       - nothing
 * side effects - setups corefile to system limits.
 * -kre
 */
static void
setup_corefile(void)
{
#ifdef HAVE_SYS_RESOURCE_H
	struct rlimit rlim;	/* resource limits */

	/* Set corefilesize to maximum */
	if(!getrlimit(RLIMIT_CORE, &rlim))
	{
		rlim.rlim_cur = rlim.rlim_max;
		setrlimit(RLIMIT_CORE, &rlim);
	}
#endif
}

static void
ilogcb(const char *buf)
{
	ilog(L_MAIN, "%s", buf);
}

static void
restartcb(const char *buf)
{
	restart(buf);
}

static void
diecb(const char *buf)
{
	if(buf != NULL)
		ilog(L_MAIN, "%s", buf);
	outofmemory();	
}

int
main(int argc, char *argv[])
{
	/* Check to see if the user is running us as root, which is a nono */
	if(geteuid() == 0)
	{
		fprintf(stderr, "Don't run ircd as root!!!\n");
		return -1;
	}

	/*
	 * save server boot time right away, so getrusage works correctly
	 */
	set_time();
	/*
	 * Setup corefile size immediately after boot -kre
	 */
	setup_corefile();

	/*
	 * set initialVMTop before we allocate any memory
	 */
	initialVMTop = get_vm_top();

	ServerRunning = 0;
	/* It ain't random, but it ought to be a little harder to guess */
	srand(SystemTime.tv_sec ^ (SystemTime.tv_usec | (getpid() << 20)));
	memset(&me, 0, sizeof(me));
	memset(&meLocalUser, 0, sizeof(meLocalUser));
	me.localClient = &meLocalUser;

	/* Make sure all lists are zeroed */
	memset(&unknown_list, 0, sizeof(unknown_list));
	memset(&lclient_list, 0, sizeof(lclient_list));
	memset(&serv_list, 0, sizeof(serv_list));
	memset(&global_serv_list, 0, sizeof(global_serv_list));
	memset(&oper_list, 0, sizeof(oper_list));

	dlinkAddTail(&me, &me.node, &global_client_list);

	memset((void *) &Count, 0, sizeof(Count));
	memset((void *) &ServerInfo, 0, sizeof(ServerInfo));
	memset((void *) &AdminInfo, 0, sizeof(AdminInfo));
	memset(&ServerStats, 0, sizeof(struct ServerStatistics));

	/* Initialise the channel capability usage counts... */
	init_chcap_usage_counts();

	ConfigFileEntry.dpath = DPATH;
	ConfigFileEntry.configfile = CPATH;	/* Server configuration file */
	ConfigFileEntry.klinefile = KPATH;	/* Server kline file */
	ConfigFileEntry.dlinefile = DLPATH;	/* dline file */
	ConfigFileEntry.xlinefile = XPATH;
	ConfigFileEntry.resvfile = RESVPATH;
	ConfigFileEntry.connect_timeout = 30;	/* Default to 30 */
	myargv = argv;
	umask(077);		/* better safe than sorry --SRB */

	parseargs(&argc, &argv, myopts);

	if(printVersion)
	{
		printf("ircd: version %s\n", ircd_version);
		exit(EXIT_SUCCESS);
	}

	if(chdir(ConfigFileEntry.dpath))
	{
		fprintf(stderr, "Unable to chdir to %s: %s\n", ConfigFileEntry.dpath, strerror(errno));
		exit(EXIT_FAILURE);
	}

	setup_signals();

#ifdef __CYGWIN__
	server_state_foreground = 1;
#endif

	if (testing_conf)
		server_state_foreground = 1;



	/* Check if there is pidfile and daemon already running */
	if(!testing_conf)
	{
		check_pidfile(pidFileName);

		if(!server_state_foreground)
			make_daemon();
		else
			print_startup(getpid());
	}

	/* This must be after we daemonize.. */
	ircd_lib(ilogcb, restartcb, diecb, 1);

	init_sys();
	init_main_logfile();
	init_patricia();
	newconf_init();
	init_s_conf();
	init_s_newconf();
	init_hash();
	clear_scache_hash_table();	/* server cache name table */
	init_host_hash();
	clear_hash_parse();
	init_client();
	initUser();
	init_channels();
	initclass();
	initwhowas();
	init_hook();
	init_reject();
	init_cache();
	init_monitor();
	load_all_modules(1);
#ifndef STATIC_MODULES
	load_core_modules(1);
#endif
	init_resolver();	/* Needs to be setup before the io loop */
	init_auth();		/* Initialise the auth code */

	if (testing_conf)
		fprintf(stderr, "\nBeginning config test\n");
	read_conf_files(YES);	/* cold start init conf files */
	rehash_bans(0);
#ifndef STATIC_MODULES

	mod_add_path(MODULE_DIR); 
	mod_add_path(MODULE_DIR "/autoload"); 
#endif

	initialize_server_capabs();	/* Set up default_server_capabs */
	initialize_global_set_options();

	if(ServerInfo.name == NULL)
	{
		fprintf(stderr, "ERROR: No server name specified in serverinfo block.\n");
		ilog(L_MAIN, "No server name specified in serverinfo block.");
		exit(EXIT_FAILURE);
	}
	strlcpy(me.name, ServerInfo.name, sizeof(me.name));

	if(ServerInfo.sid[0] == '\0')
	{
		fprintf(stderr, "ERROR: No server sid specified in serverinfo block.\n");
		ilog(L_MAIN, "No server sid specified in serverinfo block.");
		exit(EXIT_FAILURE);
	}
	strcpy(me.id, ServerInfo.sid);
	init_uid();

	/* serverinfo{} description must exist.  If not, error out. */
	if(ServerInfo.description == NULL)
	{
		fprintf(stderr, "ERROR: No server description specified in serverinfo block.\n");
		ilog(L_MAIN, "ERROR: No server description specified in serverinfo block.");
		exit(EXIT_FAILURE);
	}
	strlcpy(me.info, ServerInfo.description, sizeof(me.info));

	if (testing_conf)
	{
		fprintf(stderr, "\nConfig testing complete.\n");
		fflush(stderr);
		exit(EXIT_SUCCESS);
	}

	me.from = &me;
	me.servptr = &me;
	SetMe(&me);
	make_server(&me);
	me.serv->up = me.name;
	startup_time = CurrentTime;
	add_to_client_hash(me.name, &me);
	add_to_id_hash(me.id, &me);

	dlinkAddAlloc(&me, &global_serv_list);

	check_class();
	write_pidfile(pidFileName);
	load_help();
	open_logfiles();

	ilog(L_MAIN, "Server Ready");

	/* We want try_connections to be called as soon as possible now! -- adrian */
	/* No, 'cause after a restart it would cause all sorts of nick collides */
	/* um.  by waiting even longer, that just means we have even *more*
	 * nick collisions.  what a stupid idea. set an event for the IO loop --fl
	 */
	eventAddIsh("try_connections", try_connections, NULL, STARTUP_CONNECTIONS_TIME);
	eventAddOnce("try_connections_startup", try_connections, NULL, 0);

	eventAddIsh("collect_zipstats", collect_zipstats, NULL, ZIPSTATS_TIME);

	/* Setup the timeout check. I'll shift it later :)  -- adrian */
	eventAddIsh("comm_checktimeouts", comm_checktimeouts, NULL, 1);


	if(splitmode)
		eventAdd("check_splitmode", check_splitmode, NULL, 2);

	ServerRunning = 1;

	io_loop();
	return 0;
}
