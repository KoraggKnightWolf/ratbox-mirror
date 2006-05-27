/*
 *  ircd-ratbox: A slightly useful ircd.
 *  win32.c: select() compatible network routines.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2001 Adrian Chadd <adrian@creative.net.au>
 *  Copyright (C) 2005-2006 Aaron Sethman <androsyn@ratbox.org>
 *  Copyright (C) 2002-2006 ircd-ratbox development team
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
#include <windows.h>
#include <winsock2.h>

static HWND hwnd;

#define WM_SOCKET WM_USER
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

pid_t
getpid()
{
	return GetCurrentProcessId();
}


int
ircd_gettimeofday(struct timeval *tp, void *not_used)
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
	PROCESS_INFORMATION pi;
	STARTUPINFO si;
	char cmd[MAX_PATH];
	memset(&pi, 0, sizeof(pi));
	memset(&si, 0, sizeof(si));
	strlcpy(cmd, path, sizeof(cmd));
	if(CreateProcess(cmd, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi) == FALSE)
		return -1;

	return(pi.dwProcessId);
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
	buf = ircd_malloc(len);
	ircd_snprintf(buf, len, "%s=%s", name, value);
	len = putenv(buf);
	ircd_free(buf);
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


static LRESULT CALLBACK
ircd_process_events(HWND nhwnd, UINT umsg, WPARAM wparam, LPARAM lparam)
{
	fde_t *F;
	PF *hdl;
	void *data;
	switch(umsg)
	{
		case WM_SOCKET:
		{
			F = find_fd(wparam);
			
			if(F != NULL && F->flags.open)
			{
				switch(WSAGETSELECTEVENT(lparam))
				{
					case FD_ACCEPT:
					case FD_CLOSE:
					case FD_READ:
					{
						if((hdl = F->read_handler) != NULL)
						{
							F->read_handler = NULL;
							data = F->read_data;
							F->read_data = NULL;
							hdl(F->fd, data);
						}
						break;
					}	
					
					case FD_CONNECT:
					case FD_WRITE:
					{
						if((hdl = F->write_handler) != NULL)
						{
							F->write_handler = NULL;
							data = F->write_data;
							F->write_data = NULL;
							hdl(F->fd, data);
						}
					}					
				}
			
			}	
			return 0;
		}
		case WM_DESTROY:
		{
			PostQuitMessage(0);
			return 0;
		}
	
		default:
			return DefWindowProc(nhwnd, umsg, wparam, lparam);
	}
	return 0;
}

void
init_netio(void)
{
	/* this muchly sucks, but i'm too lazy to do overlapped i/o, maybe someday... -androsyn */
	WNDCLASS wc;
	static const char *classname = "ircd-ratbox-class";

	wc.style = 0;
	wc.lpfnWndProc = (WNDPROC) ircd_process_events;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hIcon = NULL;
	wc.hCursor = NULL;
	wc.hbrBackground = NULL;
	wc.lpszMenuName = NULL;
	wc.lpszClassName = classname;		
	wc.hInstance = GetModuleHandle(NULL);
	
	if(!RegisterClass(&wc))
		ircd_lib_die("cannot register window class");

	hwnd = CreateWindow (classname, classname, WS_POPUP, CW_USEDEFAULT, 
			     CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
			     (HWND) NULL, (HMENU) NULL, wc.hInstance, NULL);
	if(!hwnd)
		ircd_lib_die("could not create window");				
		
}

void
ircd_sleep(unsigned int seconds)
{
	Sleep(seconds);
}

int
ircd_setup_fd(int fd)
{
	fde_t *F = find_fd(fd);

	SetHandleInformation((HANDLE)fd, HANDLE_FLAG_INHERIT, 0); 
	switch(F->type)
	{
		case FD_SOCKET:
		{
		        u_long nonb = 1;  
			if(ioctlsocket((SOCKET)fd, FIONBIO, &nonb) == -1)
			{
				get_errno();
				return 0;
			}
			return 1;
		}
		default:
			return 1;
			
	}
}

void
ircd_setselect(int fd, unsigned int type, PF * handler,
	       void *client_data)
{
	fde_t *F = find_fd(fd);
	int old_flags = F->pflags;
	
	lircd_assert(fd >= 0);
	lircd_assert(F->flags.open);

	/* Update the list, even though we're not using it .. */
	if(type & IRCD_SELECT_READ)
	{
		if(handler != NULL)
			F->pflags |= FD_CLOSE | FD_READ | FD_ACCEPT;
		else
			F->pflags &= ~(FD_CLOSE|FD_READ|FD_ACCEPT);
		F->read_handler = handler;
		F->read_data = client_data;
	}

	if(type & IRCD_SELECT_WRITE)
	{
		if(handler != NULL)
			F->pflags |= FD_WRITE | FD_CONNECT;
		else
			F->pflags &= ~(FD_WRITE|FD_CONNECT);
		F->write_handler = handler;
		F->write_data = client_data;
	}
	
	if(old_flags == 0 && F->pflags == 0)
		return;
	
	if(F->pflags != old_flags)
	{
		WSAAsyncSelect(F->fd, hwnd, WM_SOCKET, F->pflags);
	} 
	
}


static int has_set_timer = 0;

int 
ircd_select(unsigned long delay)
{
	MSG msg;
	if(has_set_timer == 0)
	{
		/* XXX should probably have this handle all the events
		 * instead of busy looping 
		 */
		SetTimer(hwnd, 0, delay, NULL);
		has_set_timer = 1;
	}
	
	if(GetMessage(&msg, NULL, 0, 0) == FALSE)
	{
		ircd_lib_die("GetMessage failed..byebye");
	}
	ircd_set_time();	

	DispatchMessage(&msg);	
	return IRCD_OK;
}

#ifdef strerror
#undef strerror
#endif

const char *
wsock_strerror(int error)
{
	switch(error)
	{
		case  0:			return "Success";
		case  WSAEINTR:			return "Interrupted system call";
		case  WSAEBADF:			return "Bad file number";
		case  WSAEACCES:		return "Permission denied";
		case  WSAEFAULT:		return "Bad address";
		case  WSAEINVAL:		return "Invalid argument";
		case  WSAEMFILE:		return "Too many open sockets";
		case  WSAEWOULDBLOCK:		return "Operation would block";
		case  WSAEINPROGRESS:		return "Operation now in progress";
		case  WSAEALREADY:		return "Operation already in progress";
		case  WSAENOTSOCK:		return "Socket operation on non-socket";
		case  WSAEDESTADDRREQ:		return "Destination address required";
		case  WSAEMSGSIZE:		return "Message too long";
		case  WSAEPROTOTYPE:		return "Protocol wrong type for socket";
		case  WSAENOPROTOOPT:		return "Bad protocol option";
		case  WSAEPROTONOSUPPORT:	return "Protocol not supported";
		case  WSAESOCKTNOSUPPORT:	return "Socket type not supported";
		case  WSAEOPNOTSUPP:		return "Operation not supported on socket";
		case  WSAEPFNOSUPPORT:		return "Protocol family not supported";
		case  WSAEAFNOSUPPORT:		return "Address family not supported";
		case  WSAEADDRINUSE:		return "Address already in use";
		case  WSAEADDRNOTAVAIL:		return "Can't assign requested address";
		case  WSAENETDOWN:		return "Network is down";
		case  WSAENETUNREACH:		return "Network is unreachable";
		case  WSAENETRESET:		return "Net connection reset";
		case  WSAECONNABORTED:		return "Software caused connection abort";
		case  WSAECONNRESET:		return "Connection reset by peer";
		case  WSAENOBUFS:		return "No buffer space available";
		case  WSAEISCONN:		return "Socket is already connected";
		case  WSAENOTCONN:		return "Socket is not connected";
		case  WSAESHUTDOWN:		return "Can't send after socket shutdown";
		case  WSAETOOMANYREFS:		return "Too many references, can't splice";
		case  WSAETIMEDOUT:		return "Connection timed out";
		case  WSAECONNREFUSED:		return "Connection refused";
		case  WSAELOOP:			return "Too many levels of symbolic links";
		case  WSAENAMETOOLONG:		return "File name too long";
		case  WSAEHOSTDOWN:		return "Host is down";
		case  WSAEHOSTUNREACH:		return "No route to host";
		case  WSAENOTEMPTY:		return "Directory not empty";
		case  WSAEPROCLIM:		return "Too many processes";
		case  WSAEUSERS:		return "Too many users";
		case  WSAEDQUOT:		return "Disc quota exceeded";
		case  WSAESTALE:		return "Stale NFS file handle";
		case  WSAEREMOTE:		return "Too many levels of remote in path";
		case  WSASYSNOTREADY:		return "Network system is unavailable";
		case  WSAVERNOTSUPPORTED:	return "Winsock version out of range";
		case  WSANOTINITIALISED:	return "WSAStartup not yet called";
		case  WSAEDISCON:		return "Graceful shutdown in progress";
		case  WSAHOST_NOT_FOUND:	return "Host not found";
		case  WSANO_DATA:	 	return "No host data of that type was found";
		default:			return strerror(error);
	}
};
