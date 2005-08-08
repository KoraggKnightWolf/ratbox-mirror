/*
 *  ircd-ratbox: A slightly useful ircd.
 *  win32.c: select() compatible network routines.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2001 Adrian Chadd <adrian@creative.net.au>
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
 *  $Id: select.c 20593 2005-07-24 02:57:33Z androsyn $
 */

#include "ircd_lib.h"

/*
 * having gettimeofday is nice...
 */

int 
gettimeofday(struct timeval *tv, void *unused)
{	
    SYSTEMTIME st;
    struct tm tm;
    time_t t;
    GetSystemTime(&st);
    tm.tm_sec = st.wSecond;
    tm.tm_min = st.wMinute;
    tm.tm_hour = st.wHour;
    tm.tm_mday = st.wDay;
    tm.tm_mon = st.wMonth - 1;
    tm.tm_year = st.wYear - 1900;
    tm.tm_isdst = -1;
    t = mktime(&tm);
    tv->tv_sec = t;
    tv->tv_usec = st.wMilliseconds * 1000;
    return 0; 
}

