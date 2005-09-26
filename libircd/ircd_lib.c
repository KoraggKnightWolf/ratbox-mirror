/*
 * $Id$
 */
#include "ircd_lib.h"



static log_cb *ircd_log;
static restart_cb *ircd_restart;
static die_cb *ircd_die;

static struct timeval *SystemTime;


static char errbuf[512];

time_t
ircd_current_time(void)
{
	if(SystemTime == NULL)
		ircd_set_time();
	return SystemTime->tv_sec;
}

struct timeval *
ircd_current_time_tv(void)
{
	if(SystemTime == NULL)
		ircd_set_time();
	return SystemTime;	
}

void
ircd_lib_log(const char *format, ...)
{
	va_list args;
	if(ircd_log == NULL)
		return;
	va_start(args, format);
	ircvsnprintf(errbuf, sizeof(errbuf), format,  args);
	va_end(args);
	ircd_log(errbuf);
}

void
ircd_lib_die(const char *format, ...)
{
	va_list args;
	if(ircd_die == NULL)
		return;
	va_start(args, format);
	ircvsnprintf(errbuf, sizeof(errbuf), format,  args);
	va_end(args);
	ircd_die(errbuf);
}

void
ircd_lib_restart(const char *format, ...)
{
	va_list args;
	if(ircd_restart == NULL)
		return;
	va_start(args, format);
	ircvsnprintf(errbuf, sizeof(errbuf), format,  args);
	va_end(args);
	ircd_restart(errbuf);
}


void
ircd_set_time(void)
{
	struct timeval newtime;
	newtime.tv_sec = 0;
	newtime.tv_usec = 0;
	if(SystemTime == NULL)
	{
		SystemTime = ircd_malloc(sizeof(struct timeval));
	}
	if(gettimeofday(&newtime, NULL) == -1)
	{
		ircd_lib_log("Clock Failure (%d)", errno);
		ircd_lib_restart("Clock Failure");
	}

	if(newtime.tv_sec < SystemTime->tv_sec)
		set_back_events(SystemTime->tv_sec - newtime.tv_sec);

	memcpy(SystemTime, &newtime, sizeof(struct timeval));
}


void
ircd_lib(log_cb *ilog, restart_cb *irestart, die_cb *idie, int closeall, int maxcon, size_t lb_heap_size, size_t dh_size)
{
	ircd_log = ilog;
	ircd_restart = irestart;
	ircd_die = idie;
	fdlist_init(closeall, maxcon);
	init_netio();
	ircd_event_init();
	initBlockHeap();
	init_dlink_nodes(dh_size);
	linebuf_init(lb_heap_size);
}

