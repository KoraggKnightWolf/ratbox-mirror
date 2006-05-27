/*
 *  ircd-ratbox: A slightly useful ircd.
 *  unix.c: various unix type functions
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 *  USA
 *
 *  $Id$
 */

#include "ircd_lib.h"

pid_t
ircd_spawn_process(const char *path, const char **argv)
{
	pid_t pid;
	if(!(pid = vfork()))
	{
		execv(path, (const void *)argv); /* make gcc shut up */
		_exit(1); /* if we're still here, we're screwed */
	}
	return(pid);
}

#ifndef HAVE_GETTIMEOFDAY
int
ircd_gettimeofday(struct timeval *tv, void *tz)
{
	if(tv == NULL)
	{
		errno = EFAULT;
		return -1;
	}
	tv->tv_usec = 0;
	if(time(&tv->tv_sec) == -1)
		return -1;
	return 0;
}
#endif

void
ircd_sleep(unsigned int seconds)
{
#ifdef HAVE_NANOSLEEP
	struct timespec tv;
	tv.tv_nsec = 0;
	tv.tv_sec = seconds;
	nanosleep(&tv, NULL);
#else 
	struct timeval tv;
	tv.tv_sec = seconds;
	tv.tv_usec = 0;
	select(0, NULL, NULL, NULL, &tv);
#endif
}



