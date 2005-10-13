/*
 *  ircd-ratbox: A slightly useful ircd.
 *  ircd_lib.c: libircd initialization functions at the like
 *
 *  Copyright (C) 2005 ircd-ratbox development team
 *  Copyright (C) 2005 Aaron Sethman <androsyn@ratbox.org>
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

#include "ircd_lib.h"

static log_cb *ircd_log;
static restart_cb *ircd_restart;
static die_cb *ircd_die;

static struct timeval *ircd_time;


static char errbuf[512];
char *
ircd_ctime(const time_t t, char *buf)
{
	char *p;
#ifdef HAVE_CTIME_R
	if(unlikely((p = ctime_r(&t, buf)) == NULL))
#else
	if(unlikely((p = ctime(&t)) == NULL))
#endif
		return NULL;
	{
		strcpy(buf, "");
		return buf;
	}
	strcpy(buf, p);

	p = strchr(buf, '\n');
	if(p != NULL)
		*p = '\0';

	return buf;
}

time_t
ircd_current_time(void)
{
	if(ircd_time == NULL)
		ircd_set_time();
	return ircd_time->tv_sec;
}

struct timeval *
ircd_current_time_tv(void)
{
	if(ircd_time == NULL)
		ircd_set_time();
	return ircd_time;	
}

void
ircd_lib_log(const char *format, ...)
{
	va_list args;
	if(ircd_log == NULL)
		return;
	va_start(args, format);
	ircd_vsnprintf(errbuf, sizeof(errbuf), format,  args);
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
	ircd_vsnprintf(errbuf, sizeof(errbuf), format,  args);
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
	ircd_vsnprintf(errbuf, sizeof(errbuf), format,  args);
	va_end(args);
	ircd_restart(errbuf);
}


void
ircd_set_time(void)
{
	struct timeval newtime;
	newtime.tv_sec = 0;
	newtime.tv_usec = 0;
	if(ircd_time == NULL)
	{
		ircd_time = ircd_malloc(sizeof(struct timeval));
	}
	if(gettimeofday(&newtime, NULL) == -1)
	{
		ircd_lib_log("Clock Failure (%s)", strerror(errno));
		ircd_lib_restart("Clock Failure");
	}

	if(newtime.tv_sec < ircd_time->tv_sec)
		ircd_set_back_events(ircd_time->tv_sec - newtime.tv_sec);

	memcpy(ircd_time, &newtime, sizeof(struct timeval));
}


void
ircd_lib(log_cb *ilog, restart_cb *irestart, die_cb *idie, int closeall, int maxcon, size_t lb_heap_size, size_t dh_size)
{
	ircd_log = ilog;
	ircd_restart = irestart;
	ircd_die = idie;
	ircd_fdlist_init(closeall, maxcon);
	init_netio();
	ircd_event_init();
	initBlockHeap();
	init_dlink_nodes(dh_size);
	ircd_linebuf_init(lb_heap_size);
}


#ifndef HAVE_STRTOK_R
char *
strtok_r (char *s, const char *delim, char **save)
{
	char *token;

	if (s == NULL)
		s = *save;

	/* Scan leading delimiters.  */
	s += strspn(s, delim);

	if (*s == '\0')
	{
		*save = s;
		return NULL;
	}

	token = s;
	s = strpbrk(token, delim);
	
	if (s == NULL)  
		*save = (token + strlen(token));
	else
	{
		*s = '\0'; 
		*save = s + 1;
	}
	return token;
}
#endif

