/*
 *  ircd-ratbox: A slightly useful ircd.
 *  commio.c: Network/file related functions
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
#include "commio.h"
#include "ircd_memory.h"
#include "snprintf.h"

#ifndef IN_LOOPBACKNET
#define IN_LOOPBACKNET        0x7f
#endif

#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned int) 0xffffffff)
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

const char *const NONB_ERROR_MSG = "set_non_blocking failed for %s:%s";
const char *const SETBUF_ERROR_MSG = "set_sock_buffers failed for server %s:%s";

static const char *comm_err_str[] = { "Comm OK", "Error during bind()",
	"Error during DNS lookup", "connect timeout",
	"Error during connect()",
	"Comm Error"
};

fde_t *fd_table = NULL;

static void fdlist_update_biggest(int fd, int opening);

/* Highest FD and number of open FDs .. */
int highest_fd = -1;		/* Its -1 because we haven't started yet -- adrian */
int number_fd = 0;


static void comm_connect_callback(int fd, int status);
static PF comm_connect_timeout;
static PF comm_connect_tryconnect;

/* 32bit solaris is kinda slow and stdio only supports fds < 256
 * so we got to do this crap below.
 * (BTW Fuck you Sun, I hate your guts and I hope you go bankrupt soon)
 */
#if defined (__SVR4) && defined (__sun) 
static void comm_fd_hack(int *fd)
{
	int newfd;
	if(*fd > 256 || *fd < 0)
		return;
	if((newfd = fcntl(*fd, F_DUPFD, 256)) != -1)
	{
		close(*fd);
		*fd = newfd;
	} 
	return;
}
#else
#define comm_fd_hack(fd) 
#endif


/* close_all_connections() can be used *before* the system come up! */

void
comm_close_all(void)
{
	int i;
#ifndef NDEBUG
	int fd;
#endif

	/* XXX someone tell me why we care about 4 fd's ? */
	/* XXX btw, fd 3 is used for profiler ! */

	for (i = 4; i < MAXCONNECTIONS; ++i)
	{
		if(fd_table[i].flags.open)
			comm_close(i);
		else
			close(i);
	}

	/* XXX should his hack be done in all cases? */
#ifndef NDEBUG
	/* fugly hack to reserve fd == 2 */
	(void) close(2);
	fd = open("stderr.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
	if(fd >= 0)
	{
		dup2(fd, 2);
		close(fd);
	}
#endif
}

/*
 * get_sockerr - get the error value from the socket or the current errno
 *
 * Get the *real* error from the socket (well try to anyway..).
 * This may only work when SO_DEBUG is enabled but its worth the
 * gamble anyway.
 */
int
comm_get_sockerr(int fd)
{
	int errtmp = errno;
#ifdef SO_ERROR
	int err = 0;
	socklen_t len = sizeof(err);

	if(-1 < fd && !getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *) &err, (socklen_t *) & len))
	{
		if(err)
			errtmp = err;
	}
	errno = errtmp;
#endif
	return errtmp;
}

/*
 * set_sock_buffers - set send and receive buffers for socket
 * 
 * inputs	- fd file descriptor
 * 		- size to set
 * output       - returns true (1) if successful, false (0) otherwise
 * side effects -
 */
int
comm_set_buffers(int fd, int size)
{
	if(setsockopt
	   (fd, SOL_SOCKET, SO_RCVBUF, (char *) &size, sizeof(size))
	   || setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char *) &size, sizeof(size)))
		return 0;
	return 1;
}

/*
 * set_non_blocking - Set the client connection into non-blocking mode. 
 *
 * inputs	- fd to set into non blocking mode
 * output	- 1 if successful 0 if not
 * side effects - use POSIX compliant non blocking and
 *                be done with it.
 */
#ifndef comm_set_nb
int
comm_set_nb(int fd)
{
	int nonb = 0;
	int res;

	nonb |= O_NONBLOCK;
	res = fcntl(fd, F_GETFL, 0);
	if(-1 == res || fcntl(fd, F_SETFL, res | nonb) == -1)
		return 0;

	fd_table[fd].flags.nonblocking = 1;
	return 1;
}
#endif

/*
 * stolen from squid - its a neat (but overused! :) routine which we
 * can use to see whether we can ignore this errno or not. It is
 * generally useful for non-blocking network IO related errnos.
 *     -- adrian
 */
int
ignoreErrno(int ierrno)
{
	switch (ierrno)
	{
	case EINPROGRESS:
	case EWOULDBLOCK:
#if EAGAIN != EWOULDBLOCK
	case EAGAIN:
#endif
	case EALREADY:
	case EINTR:
#ifdef ERESTART
	case ERESTART:
#endif
		return 1;
	default:
		return 0;
	}
}


/*
 * comm_settimeout() - set the socket timeout
 *
 * Set the timeout for the fd
 */
void
comm_settimeout(int fd, time_t timeout, PF * callback, void *cbdata)
{
	fde_t *F;
	s_assert(fd >= 0);
	F = &fd_table[fd];
	s_assert(F->flags.open);

	F->timeout = CurrentTime + (timeout / 1000);
	F->timeout_handler = callback;
	F->timeout_data = cbdata;
}


/*
 * comm_setflush() - set a flush function
 *
 * A flush function is simply a function called if found during
 * comm_timeouts(). Its basically a second timeout, except in this case
 * I'm too lazy to implement multiple timeout functions! :-)
 * its kinda nice to have it seperate, since this is designed for
 * flush functions, and when comm_close() is implemented correctly
 * with close functions, we _actually_ don't call comm_close() here ..
 */
void
comm_setflush(int fd, time_t timeout, PF * callback, void *cbdata)
{
	fde_t *F;
	s_assert(fd >= 0);
	F = &fd_table[fd];
	s_assert(F->flags.open);

	F->flush_timeout = CurrentTime + (timeout / 1000);
	F->flush_handler = callback;
	F->flush_data = cbdata;
}


/*
 * comm_checktimeouts() - check the socket timeouts
 *
 * All this routine does is call the given callback/cbdata, without closing
 * down the file descriptor. When close handlers have been implemented,
 * this will happen.
 */
void
comm_checktimeouts(void *notused)
{
	int fd;
	PF *hdl;
	void *data;
	fde_t *F;
	for (fd = 0; fd <= highest_fd; fd++)
	{
		F = &fd_table[fd];
		if(!F->flags.open)
			continue;
		if(F->flags.closing)
			continue;

		/* check flush functions */
		if(F->flush_handler &&
		   F->flush_timeout > 0 && F->flush_timeout < CurrentTime)
		{
			hdl = F->flush_handler;
			data = F->flush_data;
			comm_setflush(F->fd, 0, NULL, NULL);
			hdl(F->fd, data);
		}

		/* check timeouts */
		if(F->timeout_handler &&
		   F->timeout > 0 && F->timeout < CurrentTime)
		{
			/* Call timeout handler */
			hdl = F->timeout_handler;
			data = F->timeout_data;
			comm_settimeout(F->fd, 0, NULL, NULL);
			hdl(F->fd, data);
		}
	}
}



/*
 * void comm_connect_tcp(int fd, struct sockaddr *dest,
 *                       struct sockaddr *clocal, int socklen,
 *                       CNCB *callback, void *data, int timeout)
 * Input: An fd to connect with, a host and port to connect to,
 *        a local sockaddr to connect from + length(or NULL to use the
 *        default), a callback, the data to pass into the callback, the
 *        address family.
 * Output: None.
 * Side-effects: A non-blocking connection to the host is started, and
 *               if necessary, set up for selection. The callback given
 *               may be called now, or it may be called later.
 */
void
comm_connect_tcp(int fd, struct sockaddr *dest,
		 struct sockaddr *clocal, int socklen, CNCB * callback,
		 void *data, int timeout)
{
	fde_t *F;
	s_assert(fd >= 0);
	F = &fd_table[fd];
	F->flags.called_connect = 1;
	s_assert(callback);
	F->connect.callback = callback;
	F->connect.data = data;

	memcpy(&F->connect.hostaddr, dest, socklen);

	/* Note that we're using a passed sockaddr here. This is because
	 * generally you'll be bind()ing to a sockaddr grabbed from
	 * getsockname(), so this makes things easier.
	 * XXX If NULL is passed as local, we should later on bind() to the
	 * virtual host IP, for completeness.
	 *   -- adrian
	 */
	if((clocal != NULL) && (bind(F->fd, clocal, socklen) < 0))
	{
		/* Failure, call the callback with COMM_ERR_BIND */
		comm_connect_callback(F->fd, COMM_ERR_BIND);
		/* ... and quit */
		return;
	}

	/* We have a valid IP, so we just call tryconnect */
	/* Make sure we actually set the timeout here .. */
	comm_settimeout(F->fd, timeout * 1000, comm_connect_timeout, NULL);
	comm_connect_tryconnect(F->fd, NULL);
}


/*
 * comm_connect_callback() - call the callback, and continue with life
 */
static void
comm_connect_callback(int fd, int status)
{
	CNCB *hdl;
	fde_t *F = &fd_table[fd];
	/* This check is gross..but probably necessary */
	if(F->connect.callback == NULL)
		return;
	/* Clear the connect flag + handler */
	hdl = F->connect.callback;
	F->connect.callback = NULL;
	F->flags.called_connect = 0;

	/* Clear the timeout handler */
	comm_settimeout(F->fd, 0, NULL, NULL);

	/* Call the handler */
	hdl(F->fd, status, F->connect.data);
}


/*
 * comm_connect_timeout() - this gets called when the socket connection
 * times out. This *only* can be called once connect() is initially
 * called ..
 */
static void
comm_connect_timeout(int fd, void *notused)
{
	/* error! */
	comm_connect_callback(fd, COMM_ERR_TIMEOUT);
}

/* static void comm_connect_tryconnect(int fd, void *notused)
 * Input: The fd, the handler data(unused).
 * Output: None.
 * Side-effects: Try and connect with pending connect data for the FD. If
 *               we succeed or get a fatal error, call the callback.
 *               Otherwise, it is still blocking or something, so register
 *               to select for a write event on this FD.
 */
static void
comm_connect_tryconnect(int fd, void *notused)
{
	int retval;
	fde_t *F = &fd_table[fd];

	if(F->connect.callback == NULL)
		return;
	/* Try the connect() */
	retval = connect(fd,
			 (struct sockaddr *) &fd_table[fd].connect.hostaddr, 
						       GET_SS_LEN(fd_table[fd].connect.hostaddr));
	/* Error? */
	if(retval < 0)
	{
		/*
		 * If we get EISCONN, then we've already connect()ed the socket,
		 * which is a good thing.
		 *   -- adrian
		 */
		if(errno == EISCONN)
			comm_connect_callback(F->fd, COMM_OK);
		else if(ignoreErrno(errno))
			/* Ignore error? Reschedule */
			comm_setselect(F->fd, FDLIST_SERVER, COMM_SELECT_READ|COMM_SELECT_WRITE,
				       comm_connect_tryconnect, NULL, 0);
		else
			/* Error? Fail with COMM_ERR_CONNECT */
			comm_connect_callback(F->fd, COMM_ERR_CONNECT);
		return;
	}
	/* If we get here, we've suceeded, so call with COMM_OK */
	comm_connect_callback(F->fd, COMM_OK);
}

/*
 * comm_error_str() - return an error string for the given error condition
 */
const char *
comm_errstr(int error)
{
	if(error < 0 || error >= COMM_ERR_MAX)
		return "Invalid error number!";
	return comm_err_str[error];
}


int
comm_socketpair(int family, int sock_type, int proto, int *nfd, const char *note)
{
	if(number_fd >= MASTER_MAX)
	{
		errno = ENFILE;
		return -1;
	}
	if(socketpair(family, sock_type, proto, nfd))
		return -1;
	
	comm_fd_hack(&nfd[0]);
	if(nfd[0] < 0)
	{
		close(nfd[1]);
		return -1;
	}
	/* Set the socket non-blocking, and other wonderful bits */
	if(!comm_set_nb(nfd[0]))
	{
		lib_ilog("comm_open: Couldn't set FD %d non blocking: %s", nfd[0], strerror(errno));
		close(nfd[0]);
		close(nfd[1]);
		return -1;
	}

	if(!comm_set_nb(nfd[1]))
	{
		lib_ilog("comm_open: Couldn't set FD %d non blocking: %s", nfd[1], strerror(errno));
		close(nfd[0]);
		close(nfd[1]);
		return -1;
	}

	/* Next, update things in our fd tracking */
	comm_open(nfd[0], FD_SOCKET, note);
	return 0;
	
}


int 
comm_pipe(int *fd, const char *desc)
{

	if(number_fd >= MASTER_MAX)
	{
		errno = ENFILE;
		return -1;
	}
	if(pipe(fd) == -1)
		return -1;
	comm_open(fd[0], FD_PIPE, desc);
	comm_open(fd[1], FD_PIPE, desc);
	return 0;
}

/*
 * comm_socket() - open a socket
 *
 * This is a highly highly cut down version of squid's comm_open() which
 * for the most part emulates socket(), *EXCEPT* it fails if we're about
 * to run out of file descriptors.
 */
int
comm_socket(int family, int sock_type, int proto, const char *note)
{
	int fd;
	/* First, make sure we aren't going to run out of file descriptors */
	if(number_fd >= MASTER_MAX)
	{
		errno = ENFILE;
		return -1;
	}

	/*
	 * Next, we try to open the socket. We *should* drop the reserved FD
	 * limit if/when we get an error, but we can deal with that later.
	 * XXX !!! -- adrian
	 */
	fd = socket(family, sock_type, proto);
	comm_fd_hack(&fd);
	if(fd < 0)
		return -1;	/* errno will be passed through, yay.. */

#if defined(IPV6) && defined(IPV6_V6ONLY)
	/* 
	 * Make sure we can take both IPv4 and IPv6 connections
	 * on an AF_INET6 socket
	 */
	if(family == AF_INET6)
	{
		int off = 1;
		if(setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off)) == -1)
		{
			lib_ilog("comm_socket: Could not set IPV6_V6ONLY option to 1 on FD %d: %s",
			     fd, strerror(errno));
			close(fd);
			return -1;
		}
	}
#endif

	/* Set the socket non-blocking, and other wonderful bits */
	if(!comm_set_nb(fd))
	{
		lib_ilog("comm_open: Couldn't set FD %d non blocking: %s", fd, strerror(errno));
		close(fd);
		return -1;
	}

	/* Next, update things in our fd tracking */
	comm_open(fd, FD_SOCKET, note);
	return fd;
}


/*
 * comm_accept() - accept an incoming connection
 *
 * This is a simple wrapper for accept() which enforces FD limits like
 * comm_open() does.
 */
int
comm_accept(int fd, struct sockaddr *pn, socklen_t *addrlen)
{
	int newfd;
	if(number_fd >= MASTER_MAX)
	{
		errno = ENFILE;
		return -1;
	}

	/*
	 * Next, do the accept(). if we get an error, we should drop the
	 * reserved fd limit, but we can deal with that when comm_open()
	 * also does it. XXX -- adrian
	 */
	newfd = accept(fd, (struct sockaddr *) pn, addrlen);
        comm_fd_hack(&newfd);
                        
	if(newfd < 0)
		return -1;

	/* Set the socket non-blocking, and other wonderful bits */
	if(!comm_set_nb(newfd))
	{
		lib_ilog("comm_accept: Couldn't set FD %d non blocking!", newfd);
		close(newfd);
		return -1;
	}

	/* Next, tag the FD as an incoming connection */
	comm_open(newfd, FD_SOCKET, "Incoming connection");

	/* .. and return */
	return newfd;
}

/*
 * If a sockaddr_storage is AF_INET6 but is a mapped IPv4
 * socket manged the sockaddr.
 */
#ifndef mangle_mapped_sockaddr
void
mangle_mapped_sockaddr(struct sockaddr *in)
{
	struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)in;								

	if(in->sa_family == AF_INET)
		return;

	if(in->sa_family == AF_INET6 && IN6_IS_ADDR_V4MAPPED(&in6->sin6_addr))
	{
		struct sockaddr_in in4;
		memset(&in4, 0, sizeof(struct sockaddr_in));
		in4.sin_family = AF_INET;
		in4.sin_port = in6->sin6_port;
		in4.sin_addr.s_addr = ((uint32_t *)&in6->sin6_addr)[3];
		memcpy(in, &in4, sizeof(struct sockaddr_in)); 		
	}	
	return;
}
#endif


static void
fdlist_update_biggest(int fd, int opening)
{
	if(fd < highest_fd)
		return;
	s_assert(fd < MAXCONNECTIONS);

	if(fd > highest_fd)
	{
		/*  
		 * s_assert that we are not closing a FD bigger than
		 * our known biggest FD
		 */
		s_assert(opening);
		highest_fd = fd;
		return;
	}
	/* if we are here, then fd == Biggest_FD */
	/*
	 * s_assert that we are closing the biggest FD; we can't be
	 * re-opening it
	 */
	s_assert(!opening);
	while (highest_fd >= 0 && !fd_table[highest_fd].flags.open)
		highest_fd--;
}


void
fdlist_init(void)
{
	static int initialized = 0;

	if(!initialized)
	{
		/* Since we're doing this once .. */
		fd_table = MyMalloc((MAXCONNECTIONS + 1) * sizeof(fde_t));
		initialized = 1;
	}
	init_netio();
}

/* Called to open a given filedescriptor */
void
comm_open(int fd, unsigned int type, const char *desc)
{
	fde_t *F = &fd_table[fd];
	s_assert(fd >= 0);

	if(F->flags.open)
	{
		comm_close(fd);
	}
	s_assert(!F->flags.open);
	F->fd = fd;
	F->type = type;
	F->flags.open = 1;
#ifdef NOTYET
	F->defer.until = 0;
	F->defer.n = 0;
	F->defer.handler = NULL;
#endif
	fdlist_update_biggest(fd, 1);
	F->comm_index = -1;
	F->list = FDLIST_NONE;
	if(desc)
		strlcpy(F->desc, desc, sizeof(F->desc));
	number_fd++;
}


/* Called to close a given filedescriptor */
void
comm_close(int fd)
{
	fde_t *F = &fd_table[fd];
	s_assert(F->flags.open);
	/* All disk fd's MUST go through file_close() ! */
	s_assert(F->type != FD_FILE);
	if(F->type == FD_FILE)
	{
		s_assert(F->read_handler == NULL);
		s_assert(F->write_handler == NULL);
	}
	comm_setselect(F->fd, FDLIST_NONE, COMM_SELECT_WRITE | COMM_SELECT_READ, NULL, NULL, 0);
	comm_setflush(F->fd, 0, NULL, NULL);
	
	F->flags.open = 0;
	fdlist_update_biggest(fd, 0);
	number_fd--;
	memset(F, '\0', sizeof(fde_t));
	F->timeout = 0;
	/* Unlike squid, we're actually closing the FD here! -- adrian */
	close(fd);
}


/*
 * comm_dump() - dump the list of active filedescriptors
 */
void
comm_dump(DUMPCB *cb, void *data)
{
	int i;
	char buf[128];
	for (i = 0; i <= highest_fd; i++)
	{
		if(!fd_table[i].flags.open)
			continue;

		ircsnprintf(buf, sizeof(buf), "F :fd %-3d desc '%s'",
				   i, fd_table[i].desc);
		cb(buf, data);
	}
}

/*
 * comm_note() - set the fd note
 *
 * Note: must be careful not to overflow fd_table[fd].desc when
 *       calling.
 */
void
comm_note(int fd, const char *format, ...)
{
	va_list args;

	if(format)
	{
		va_start(args, format);
		ircvsnprintf(fd_table[fd].desc, FD_DESC_SZ, format, args);
		va_end(args);
	}
	else
		fd_table[fd].desc[0] = '\0';
}

int
comm_can_writev(int fd)
{
#ifndef HAVE_WRITEV
	return 0;
#endif
/*	fde_t *F = &fd_table[fd];
 */
	return 1;
}

ssize_t
comm_read(int fd, void *buf, int count)
{
	fde_t *F = &fd_table[fd];
	if(F == NULL)
		return 0;

	switch(F->type)
	{
		case FD_SOCKET:
			return recv(fd, buf, count, 0);
		case FD_PIPE:
		default:
			return read(fd, buf, count);
	}
}

ssize_t
comm_write(int fd, void *buf, int count)
{
	fde_t *F = &fd_table[fd];
	if(F == NULL)
		return 0;	
	switch(F->type)
	{
#if 0
	/* i'll finish this some day */
		case FD_SSL:
		{
			r = SSL_write(F->ssl, buf, count);
			if(r < 0)
			{
				switch ((ssl_r = SSL_get_error(con->ssl, r)))
				{
					case SSL_ERROR_WANT_READ:
					case SSL_ERROR_WANT_WRITE:
						errno = EAGAIN;
						return -1;
					case SSL_ERROR_SYSCALL:
						return -1;
					default:
						lib_ilog("Unknown SSL_write error:%s", 
							     ERR_error_string(ERR_get_error(), NULL));
						return -1;					
				
				}
			} 
			return 0;
		}	
#endif
		case FD_SOCKET:
			return send(fd, buf, count, MSG_NOSIGNAL);
		case FD_PIPE:
		default:
			return write(fd, buf, count);
	
	}


}

ssize_t
comm_writev(int fd, struct iovec *vector, int count)
{
	fde_t *F = &fd_table[fd];

	switch(F->type)
	{
		case FD_SOCKET:
		{
			struct msghdr hdr;
			memset(&hdr, 0, sizeof(struct msghdr));
			hdr.msg_iov = vector;
			hdr.msg_iovlen = count;
			return sendmsg(fd, &hdr, MSG_NOSIGNAL);
		}	
		case FD_PIPE:
		default:
			return writev(fd, vector, count);	
	}
}


/* 
 * From: Thomas Helvey <tomh@inxpress.net>
 */
static const char *IpQuadTab[] = {
	"0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
	"10", "11", "12", "13", "14", "15", "16", "17", "18", "19",
	"20", "21", "22", "23", "24", "25", "26", "27", "28", "29",
	"30", "31", "32", "33", "34", "35", "36", "37", "38", "39",
	"40", "41", "42", "43", "44", "45", "46", "47", "48", "49",
	"50", "51", "52", "53", "54", "55", "56", "57", "58", "59",
	"60", "61", "62", "63", "64", "65", "66", "67", "68", "69",
	"70", "71", "72", "73", "74", "75", "76", "77", "78", "79",
	"80", "81", "82", "83", "84", "85", "86", "87", "88", "89",
	"90", "91", "92", "93", "94", "95", "96", "97", "98", "99",
	"100", "101", "102", "103", "104", "105", "106", "107", "108", "109",
	"110", "111", "112", "113", "114", "115", "116", "117", "118", "119",
	"120", "121", "122", "123", "124", "125", "126", "127", "128", "129",
	"130", "131", "132", "133", "134", "135", "136", "137", "138", "139",
	"140", "141", "142", "143", "144", "145", "146", "147", "148", "149",
	"150", "151", "152", "153", "154", "155", "156", "157", "158", "159",
	"160", "161", "162", "163", "164", "165", "166", "167", "168", "169",
	"170", "171", "172", "173", "174", "175", "176", "177", "178", "179",
	"180", "181", "182", "183", "184", "185", "186", "187", "188", "189",
	"190", "191", "192", "193", "194", "195", "196", "197", "198", "199",
	"200", "201", "202", "203", "204", "205", "206", "207", "208", "209",
	"210", "211", "212", "213", "214", "215", "216", "217", "218", "219",
	"220", "221", "222", "223", "224", "225", "226", "227", "228", "229",
	"230", "231", "232", "233", "234", "235", "236", "237", "238", "239",
	"240", "241", "242", "243", "244", "245", "246", "247", "248", "249",
	"250", "251", "252", "253", "254", "255"
};

/*
 * inetntoa - in_addr to string
 *      changed name to remove collision possibility and
 *      so behaviour is guaranteed to take a pointer arg.
 *      -avalon 23/11/92
 *  inet_ntoa --  returned the dotted notation of a given
 *      internet number
 *      argv 11/90).
 *  inet_ntoa --  its broken on some Ultrix/Dynix too. -avalon
 */

const char *
inetntoa(const char *in)
{
	static char buf[16];
	char *bufptr = buf;
	const unsigned char *a = (const unsigned char *) in;
	const char *n;

	n = IpQuadTab[*a++];
	while (*n)
		*bufptr++ = *n++;
	*bufptr++ = '.';
	n = IpQuadTab[*a++];
	while (*n)
		*bufptr++ = *n++;
	*bufptr++ = '.';
	n = IpQuadTab[*a++];
	while (*n)
		*bufptr++ = *n++;
	*bufptr++ = '.';
	n = IpQuadTab[*a];
	while (*n)
		*bufptr++ = *n++;
	*bufptr = '\0';
	return buf;
}


/*
 * Copyright (c) 1996-1999 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
 * CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

#define SPRINTF(x) ((size_t)ircsprintf x)

/*
 * WARNING: Don't even consider trying to compile this on a system where
 * sizeof(int) < 4.  sizeof(int) > 4 is fine; all the world's not a VAX.
 */

static const char *inet_ntop4(const u_char * src, char *dst, unsigned int size);
#ifdef IPV6
static const char *inet_ntop6(const u_char * src, char *dst, unsigned int size);
#endif

/* const char *
 * inet_ntop4(src, dst, size)
 *	format an IPv4 address
 * return:
 *	`dst' (as a const)
 * notes:
 *	(1) uses no statics
 *	(2) takes a u_char* not an in_addr as input
 * author:
 *	Paul Vixie, 1996.
 */
static const char *
inet_ntop4(const unsigned char *src, char *dst, unsigned int size)
{
	if(size < 16)
		return NULL;
	return strcpy(dst, inetntoa((const char *) src));
}

/* const char *
 * inet_ntop6(src, dst, size)
 *	convert IPv6 binary address into presentation (printable) format
 * author:
 *	Paul Vixie, 1996.
 */
#ifdef IPV6
static const char *
inet_ntop6(const unsigned char *src, char *dst, unsigned int size)
{
	/*
	 * Note that int32_t and int16_t need only be "at least" large enough
	 * to contain a value of the specified size.  On some systems, like
	 * Crays, there is no such thing as an integer variable with 16 bits.
	 * Keep this in mind if you think this function should have been coded
	 * to use pointer overlays.  All the world's not a VAX.
	 */
	char tmp[sizeof "ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255"], *tp;
	struct
	{
		int base, len;
	}
	best, cur;
	u_int words[IN6ADDRSZ / INT16SZ];
	int i;

	/*
	 * Preprocess:
	 *      Copy the input (bytewise) array into a wordwise array.
	 *      Find the longest run of 0x00's in src[] for :: shorthanding.
	 */
	memset(words, '\0', sizeof words);
	for (i = 0; i < IN6ADDRSZ; i += 2)
		words[i / 2] = (src[i] << 8) | src[i + 1];
	best.base = -1;
	cur.base = -1;
	for (i = 0; i < (IN6ADDRSZ / INT16SZ); i++)
	{
		if(words[i] == 0)
		{
			if(cur.base == -1)
				cur.base = i, cur.len = 1;
			else
				cur.len++;
		}
		else
		{
			if(cur.base != -1)
			{
				if(best.base == -1 || cur.len > best.len)
					best = cur;
				cur.base = -1;
			}
		}
	}
	if(cur.base != -1)
	{
		if(best.base == -1 || cur.len > best.len)
			best = cur;
	}
	if(best.base != -1 && best.len < 2)
		best.base = -1;

	/*
	 * Format the result.
	 */
	tp = tmp;
	for (i = 0; i < (IN6ADDRSZ / INT16SZ); i++)
	{
		/* Are we inside the best run of 0x00's? */
		if(best.base != -1 && i >= best.base && i < (best.base + best.len))
		{
			if(i == best.base)
			{
				if(i == 0)
					*tp++ = '0';
				*tp++ = ':';
			}
			continue;
		}
		/* Are we following an initial run of 0x00s or any real hex? */
		if(i != 0)
			*tp++ = ':';
		/* Is this address an encapsulated IPv4? */
		if(i == 6 && best.base == 0 &&
		   (best.len == 6 || (best.len == 5 && words[5] == 0xffff)))
		{
			if(!inet_ntop4(src + 12, tp, sizeof tmp - (tp - tmp)))
				return (NULL);
			tp += strlen(tp);
			break;
		}
		tp += SPRINTF((tp, "%x", words[i]));
	}
	/* Was it a trailing run of 0x00's? */
	if(best.base != -1 && (best.base + best.len) == (IN6ADDRSZ / INT16SZ))
		*tp++ = ':';
	*tp++ = '\0';

	/*
	 * Check for overflow, copy, and we're done.
	 */

	if((unsigned int)(tp - tmp) > size)
	{
		return (NULL);
	}
	return strcpy(dst, tmp);
}
#endif

int
inetpton_sock(const char *src, struct sockaddr *dst)
{
	if(inetpton(AF_INET, src, &((struct sockaddr_in *)dst)->sin_addr))
	{
		((struct sockaddr_in *)dst)->sin_port = 0;
		((struct sockaddr_in *)dst)->sin_family = AF_INET;
		SET_SS_LEN(*((struct irc_sockaddr_storage *)dst), sizeof(struct sockaddr_in));
		return 1;		
	} 
#ifdef IPV6
	else if(inetpton(AF_INET6, src, &((struct sockaddr_in6 *)dst)->sin6_addr))
	{
		((struct sockaddr_in6 *)dst)->sin6_port = 0;
		((struct sockaddr_in6 *)dst)->sin6_family = AF_INET6;
		SET_SS_LEN(*((struct irc_sockaddr_storage *)dst), sizeof(struct sockaddr_in6));
		return 1;
	}
#endif
	return 0;
}

const char *
inetntop_sock(struct sockaddr *src, char *dst, unsigned int size)
{
	switch(src->sa_family)
	{
		case AF_INET:
			return(inetntop(AF_INET, &((struct sockaddr_in *)src)->sin_addr, dst, size));
			break;
#ifdef IPV6	
		case AF_INET6:
			return(inetntop(AF_INET6, &((struct sockaddr_in6 *)src)->sin6_addr, dst, size));
			break;
#endif
		default:
			return NULL;
			break;
	}
}
/* char *
 * inetntop(af, src, dst, size)
 *	convert a network format address to presentation format.
 * return:
 *	pointer to presentation format address (`dst'), or NULL (see errno).
 * author:
 *	Paul Vixie, 1996.
 */
const char *
inetntop(int af, const void *src, char *dst, unsigned int size)
{
	switch (af)
	{
	case AF_INET:
		return (inet_ntop4(src, dst, size));
#ifdef IPV6
	case AF_INET6:
		if(IN6_IS_ADDR_V4MAPPED((const struct in6_addr *) src) ||
		   IN6_IS_ADDR_V4COMPAT((const struct in6_addr *) src))
			return (inet_ntop4
				((const unsigned char *)
				 &((const struct in6_addr *) src)->s6_addr[12], dst, size));
		else
			return (inet_ntop6(src, dst, size));


#endif
	default:
		return (NULL);
	}
	/* NOTREACHED */
}

/*
 * WARNING: Don't even consider trying to compile this on a system where
 * sizeof(int) < 4.  sizeof(int) > 4 is fine; all the world's not a VAX.
 */

/* int
 * inetpton(af, src, dst)
 *	convert from presentation format (which usually means ASCII printable)
 *	to network format (which is usually some kind of binary format).
 * return:
 *	1 if the address was valid for the specified address family
 *	0 if the address wasn't valid (`dst' is untouched in this case)
 *	-1 if some other error occurred (`dst' is untouched in this case, too)
 * author:
 *	Paul Vixie, 1996.
 */

/* int
 * inet_pton4(src, dst)
 *	like inet_aton() but without all the hexadecimal and shorthand.
 * return:
 *	1 if `src' is a valid dotted quad, else 0.
 * notice:
 *	does not touch `dst' unless it's returning 1.
 * author:
 *	Paul Vixie, 1996.
 */
static int
inet_pton4(src, dst)
     const char *src;
     u_char *dst;
{
	int saw_digit, octets, ch;
	u_char tmp[INADDRSZ], *tp;

	saw_digit = 0;
	octets = 0;
	*(tp = tmp) = 0;
	while ((ch = *src++) != '\0')
	{

		if(ch >= '0' && ch <= '9')
		{
			u_int new = *tp * 10 + (ch - '0');

			if(new > 255)
				return (0);
			*tp = new;
			if(!saw_digit)
			{
				if(++octets > 4)
					return (0);
				saw_digit = 1;
			}
		}
		else if(ch == '.' && saw_digit)
		{
			if(octets == 4)
				return (0);
			*++tp = 0;
			saw_digit = 0;
		}
		else
			return (0);
	}
	if(octets < 4)
		return (0);
	memcpy(dst, tmp, INADDRSZ);
	return (1);
}

#ifdef IPV6
/* int
 * inet_pton6(src, dst)
 *	convert presentation level address to network order binary form.
 * return:
 *	1 if `src' is a valid [RFC1884 2.2] address, else 0.
 * notice:
 *	(1) does not touch `dst' unless it's returning 1.
 *	(2) :: in a full address is silently ignored.
 * credit:
 *	inspired by Mark Andrews.
 * author:
 *	Paul Vixie, 1996.
 */

static int
inet_pton6(src, dst)
     const char *src;
     u_char *dst;
{
	static const char xdigits[] = "0123456789abcdef";
	u_char tmp[IN6ADDRSZ], *tp, *endp, *colonp;
	const char *curtok;
	int ch, saw_xdigit;
	u_int val;

	tp = memset(tmp, '\0', IN6ADDRSZ);
	endp = tp + IN6ADDRSZ;
	colonp = NULL;
	/* Leading :: requires some special handling. */
	if(*src == ':')
		if(*++src != ':')
			return (0);
	curtok = src;
	saw_xdigit = 0;
	val = 0;
	while ((ch = tolower(*src++)) != '\0')
	{
		const char *pch;

		pch = strchr(xdigits, ch);
		if(pch != NULL)
		{
			val <<= 4;
			val |= (pch - xdigits);
			if(val > 0xffff)
				return (0);
			saw_xdigit = 1;
			continue;
		}
		if(ch == ':')
		{
			curtok = src;
			if(!saw_xdigit)
			{
				if(colonp)
					return (0);
				colonp = tp;
				continue;
			}
			else if(*src == '\0')
			{
				return (0);
			}
			if(tp + INT16SZ > endp)
				return (0);
			*tp++ = (u_char) (val >> 8) & 0xff;
			*tp++ = (u_char) val & 0xff;
			saw_xdigit = 0;
			val = 0;
			continue;
		}
		if(*src != '\0' && ch == '.')
		{
			if(((tp + INADDRSZ) <= endp) && inet_pton4(curtok, tp) > 0)
			{
				tp += INADDRSZ;
				saw_xdigit = 0;
				break;	/* '\0' was seen by inet_pton4(). */
			}
		}
		else
			continue;
		return (0);
	}
	if(saw_xdigit)
	{
		if(tp + INT16SZ > endp)
			return (0);
		*tp++ = (u_char) (val >> 8) & 0xff;
		*tp++ = (u_char) val & 0xff;
	}
	if(colonp != NULL)
	{
		/*
		 * Since some memmove()'s erroneously fail to handle
		 * overlapping regions, we'll do the shift by hand.
		 */
		const int n = tp - colonp;
		int i;

		if(tp == endp)
			return (0);
		for (i = 1; i <= n; i++)
		{
			endp[-i] = colonp[n - i];
			colonp[n - i] = 0;
		}
		tp = endp;
	}
	if(tp != endp)
		return (0);
	memcpy(dst, tmp, IN6ADDRSZ);
	return (1);
}
#endif
int
inetpton(af, src, dst)
     int af;
     const char *src;
     void *dst;
{
	switch (af)
	{
	case AF_INET:
		return (inet_pton4(src, dst));
#ifdef IPV6
	case AF_INET6:
		/* Somebody might have passed as an IPv4 address this is sick but it works */
		if(inet_pton4(src, dst))
		{
			char tmp[HOSTIPLEN];
			ircsprintf(tmp, "::ffff:%s", src);
			return (inet_pton6(tmp, dst));
		}
		else
			return (inet_pton6(src, dst));
#endif
	default:
		return (-1);
	}
	/* NOTREACHED */
}


