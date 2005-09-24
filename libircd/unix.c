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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 *
 *  $Id: select.c 20593 2005-07-24 02:57:33Z androsyn $
 */

#include "ircd_lib.h"

pid_t
ircd_spawn_process(const char *path, const char **argv)
{
	pid_t pid;
	if(!(pid = vfork()))
	{
		execv(path, (void *)argv); /* make gcc shut up */
		_exit(1); /* if we're still here, we're screwed */
	}
	return(pid);
}

#ifndef HAVE_GETTIMEOFDAY
int
gettimeofday(struct timeval *tv, struct timezone *tz)
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

