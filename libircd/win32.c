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
#include <windows.h>
#include <winsock2.h>

static WSAEVENT Events;


/*
 * having gettimeofday is nice...
 */

typedef union {
    unsigned __int64    ft_i64;
    FILETIME            ft_val;
} FT_t;
   
#ifdef __GNUC__
#define Const64(x) x##LL   
#else
#define Const64(x) x##i64
#endif
/* Number of 100 nanosecond units from 1/1/1601 to 1/1/1970 */
#define EPOCH_BIAS  Const64(116444736000000000)


int
gettimeofday(struct timeval *tp, void *not_used)
{
    FT_t ft;

    /* this returns time in 100-nanosecond units  (i.e. tens of usecs) */
    GetSystemTimeAsFileTime(&ft.ft_val);

    /* seconds since epoch */
    tp->tv_sec = (long)((ft.ft_i64 - EPOCH_BIAS) / Const64(10000000));

    /* microseconds remaining */
    tp->tv_usec = (long)((ft.ft_i64 / Const64(10)) % Const64(1000000));

    return 0;
}


pid_t
ircd_spawn_process (const char *path, const char **argv)
{
	return _spawnv (_P_NOWAIT, path, (const char *const *) argv);
}

pid_t
waitpid (int pid, int *status, int flags)
{
	DWORD timeout = (flags & WNOHANG) ? 0 : INFINITE;
	HANDLE hProcess;
	DWORD waitcode;

	hProcess = OpenProcess (PROCESS_ALL_ACCESS, TRUE, pid);
	if(hProcess)
	{
		waitcode = WaitForSingleObject (hProcess, timeout);
		if(waitcode == WAIT_TIMEOUT)
		{
			CloseHandle (hProcess);
			return 0;
		}
		else if(waitcode == WAIT_OBJECT_0)
		{
			if(GetExitCodeProcess (hProcess, &waitcode))
			{
				*status = (int) ((waitcode & 0xff) << 8);
				CloseHandle (hProcess);
				return pid;
			}
		}
		CloseHandle (hProcess);
	} else
		errno = ECHILD;

	return -1;
}

int 
setenv(const char *name, const char *value, int overwrite)
{
	char *buf;
	int len;
	if(!overwrite)
	{
		if((buf = getenv(name)) != NULL)
		{
			if(strlen(buf) > 0)
			{
				return 0;
			}
		}
	}
	if(name == NULL || value == NULL)
		return -1;
	len = strlen(name) + strlen(value) + 5;
	buf = MyMalloc(len);
	ircsnprintf(buf, len, "%s=%s", name, value);
	len = putenv(buf);
	MyFree(buf);
	return(len);
}

int 
kill(int pid, int sig)
{
	HANDLE hProcess;
	hProcess = OpenProcess (PROCESS_ALL_ACCESS, TRUE, pid);
	int ret = -1;
	if(hProcess)
	{
		switch(sig)
		{
			case 0:
				ret = 0;
				break;

			default:
				if(TerminateProcess(hProcess, sig))
					ret = 0;
				break;
		}
		CloseHandle (hProcess);
	} else
		errno = EINVAL;

	return ret;


}

static void
update_events(fde_t *F)
{
	int events = 0;

	if (F->read_handler != NULL)
		events |= FD_ACCEPT | FD_CLOSE | FD_READ;

	if (F->write_handler != NULL)
		events |= FD_CONNECT | FD_WRITE;

	WSAEventSelect(F->fd, Events, WM_SOCKET, events);
}

void
init_netio(void)
{
	events = WSACreateEvent();

}

void
comm_setselect(int fd, unsigned int type, PF *handler, void *client_data, time_t timeout)
{
	int update = 0;
	fde_t *F = find_fd(fd);
	
	if(F == NULL)
		return;

	if(type & COMM_SELECT_READ)
	{
		if(F->read_handler != handler)
			update = 1;
		F->read_handler = handler;
		F->read_data = client_data;
	}
	
	if(type & COMM_SELECT_WRITE)
	{
		F->write_handler = handler;
		F->write_data = client_data;
	}
	if(timeout)
		F->timeout = SystemTime.tv_sec + (timeout / 1000);

	if(update)
		update_events(F);
}

