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

#include "ircd_lib.h"

#ifndef IN_LOOPBACKNET
#define IN_LOOPBACKNET        0x7f
#endif

#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned int) 0xffffffff)
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

dlink_list ircd_fd_table[FD_HASH_SIZE];
static BlockHeap *fd_heap;

static dlink_list timeout_list;

static const char *ircd_err_str[] = { "Comm OK", "Error during bind()",
	"Error during DNS lookup", "connect timeout",
	"Error during connect()",
	"Comm Error"
};



static void ircd_fdlist_update_biggest(int fd, int opening);

/* Highest FD and number of open FDs .. */
int ircd_highest_fd = -1;		/* Its -1 because we haven't started yet -- adrian */
int number_fd = 0;
int ircd_maxconnections = 0;

static void ircd_connect_callback(int fd, int status);
static PF ircd_connect_timeout;
static PF ircd_connect_tryconnect;

#ifndef HAVE_SOCKETPAIR
static int ircd_inet_socketpair(int d, int type, int protocol, int sv[2]);
#endif


static inline fde_t *
add_fd(int fd)
{
	int hash = hash_fd(fd);
	fde_t *F;
	dlink_list *list;
	/* look up to see if we have it already */
	if((F = find_fd(fd)) != NULL)
		return F; 
	
	F = BlockHeapAlloc(fd_heap);
	F->fd = fd;
	list = &ircd_fd_table[hash];
	ircd_dlinkAdd(F, &F->node, list);
	return(F);
}

static inline void
remove_fd(int fd)
{
	int hash = hash_fd(fd);
	fde_t *F;
	dlink_list *list;
	list = &ircd_fd_table[hash];
	F = find_fd(fd);
	ircd_dlinkDelete(&F->node, list);
	BlockHeapFree(fd_heap, F);
}


/* 32bit solaris is kinda slow and stdio only supports fds < 256
 * so we got to do this crap below.
 * (BTW Fuck you Sun, I hate your guts and I hope you go bankrupt soon)
 */

#if defined (__SVR4) && defined (__sun)
static void
ircd_fd_hack(int *fd)
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
#define ircd_fd_hack(fd)
#endif


/* close_all_connections() can be used *before* the system come up! */

static void
ircd_close_all(void)
{
	int i;
#ifndef NDEBUG
	int fd;
#endif

	/* XXX someone tell me why we care about 4 fd's ? */
	/* XXX btw, fd 3 is used for profiler ! */
#ifndef __MINGW32__
	for (i = 4; i < ircd_maxconnections; ++i)
	{
		close(i);
	}
#endif
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
ircd_get_sockerr(int fd)
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
ircd_set_buffers(int fd, int size)
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
int
ircd_set_nb(int fd)
{
	int nonb = 0;
	int res;
	fde_t *F = find_fd(fd);

	if((res = ircd_setup_fd(fd)))
		return res;
#ifdef O_NONBLOCK
	nonb |= O_NONBLOCK;
	res = fcntl(fd, F_GETFL, 0);
	if(-1 == res || fcntl(fd, F_SETFL, res | nonb) == -1)
		return 0;
#else
	nonb = 1;
	res = 0;
	if(ioctl(fd, FIONBIO, &nonb) == -1)
		return 0;
#endif

	F->flags.nonblocking = 1;
	return 1;
}

/*
 * ircd_settimeout() - set the socket timeout
 *
 * Set the timeout for the fd
 */
void
ircd_settimeout(int fd, time_t timeout, PF * callback, void *cbdata)
{
	fde_t *F;
	struct timeout_data *td;
	lircd_assert(fd >= 0);
	F = find_fd(fd);
	lircd_assert(F->flags.open);
	td = F->timeout;

	if(callback == NULL) /* user wants to remove */
	{
		if(td == NULL)
			return;
		ircd_dlinkDelete(&td->node, &timeout_list);
		ircd_free(td);
		F->timeout = NULL;
		return;
	}

	if(F->timeout == NULL)
		td = F->timeout = ircd_malloc(sizeof(struct timeout_data));	
		
	td->F = F;
	td->timeout = ircd_currenttime + (timeout / 1000);
	td->timeout_handler = callback;
	td->timeout_data = cbdata;
	ircd_dlinkAdd(td, &td->node, &timeout_list);
}

/*
 * ircd_checktimeouts() - check the socket timeouts
 *
 * All this routine does is call the given callback/cbdata, without closing
 * down the file descriptor. When close handlers have been implemented,
 * this will happen.
 */
void
ircd_checktimeouts(void *notused)
{
	dlink_node *ptr, *next;
	struct timeout_data *td;
	fde_t *F;
	PF *hdl;
	void *data;
	
	DLINK_FOREACH_SAFE(ptr, next, timeout_list.head)
	{
		td = ptr->data;
		F = td->F;
		if(F == NULL || F->flags.closing || !F->flags.open)
			continue;

		if(td->timeout < ircd_currenttime)
		{
			hdl = td->timeout_handler;
			data = td->timeout_data;
			ircd_dlinkDelete(&td->node, &timeout_list);
			F->timeout = NULL;
			ircd_free(td);
			hdl(F->fd, data);
		}		
	}
}



/*
 * void ircd_connect_tcp(int fd, struct sockaddr *dest,
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
ircd_connect_tcp(int fd, struct sockaddr *dest,
		 struct sockaddr *clocal, int socklen, CNCB * callback, void *data, int timeout)
{
	fde_t *F;
	lircd_assert(fd >= 0);
	F = find_fd(fd);
	F->flags.called_connect = 1;
	lircd_assert(callback);
	F->connect.callback = callback;
	F->connect.data = data;

	memcpy(&F->connect.hostaddr, dest, sizeof(F->connect.hostaddr));

	/* Note that we're using a passed sockaddr here. This is because
	 * generally you'll be bind()ing to a sockaddr grabbed from
	 * getsockname(), so this makes things easier.
	 * XXX If NULL is passed as local, we should later on bind() to the
	 * virtual host IP, for completeness.
	 *   -- adrian
	 */
	if((clocal != NULL) && (bind(F->fd, clocal, socklen) < 0))
	{
		/* Failure, call the callback with IRCD_ERR_BIND */
		ircd_connect_callback(F->fd, IRCD_ERR_BIND);
		/* ... and quit */
		return;
	}

	/* We have a valid IP, so we just call tryconnect */
	/* Make sure we actually set the timeout here .. */
	ircd_settimeout(F->fd, timeout * 1000, ircd_connect_timeout, NULL);
	ircd_connect_tryconnect(F->fd, NULL);
}


/*
 * ircd_connect_callback() - call the callback, and continue with life
 */
static void
ircd_connect_callback(int fd, int status)
{
	CNCB *hdl;
	fde_t *F = find_fd(fd);
	/* This check is gross..but probably necessary */
	if(F->connect.callback == NULL)
		return;
	/* Clear the connect flag + handler */
	hdl = F->connect.callback;
	F->connect.callback = NULL;
	F->flags.called_connect = 0;

	/* Clear the timeout handler */
	ircd_settimeout(F->fd, 0, NULL, NULL);

	/* Call the handler */
	hdl(F->fd, status, F->connect.data);
}


/*
 * ircd_connect_timeout() - this gets called when the socket connection
 * times out. This *only* can be called once connect() is initially
 * called ..
 */
static void
ircd_connect_timeout(int fd, void *notused)
{
	/* error! */
	ircd_connect_callback(fd, IRCD_ERR_TIMEOUT);
}

/* static void ircd_connect_tryconnect(int fd, void *notused)
 * Input: The fd, the handler data(unused).
 * Output: None.
 * Side-effects: Try and connect with pending connect data for the FD. If
 *               we succeed or get a fatal error, call the callback.
 *               Otherwise, it is still blocking or something, so register
 *               to select for a write event on this FD.
 */
static void
ircd_connect_tryconnect(int fd, void *notused)
{
	int retval;
	fde_t *F = find_fd(fd);

	if(F->connect.callback == NULL)
		return;
	/* Try the connect() */
	retval = connect(fd,
			 (struct sockaddr *) &F->connect.hostaddr, GET_SS_LEN(F->connect.hostaddr));
	/* Error? */
	if(retval < 0)
	{
		/*
		 * If we get EISCONN, then we've already connect()ed the socket,
		 * which is a good thing.
		 *   -- adrian
		 */
		if(errno == EISCONN)
			ircd_connect_callback(F->fd, IRCD_OK);
		else if(ignoreErrno(errno))
			/* Ignore error? Reschedule */
			ircd_setselect(F->fd, IRCD_SELECT_CONNECT,
				       ircd_connect_tryconnect, NULL);
		else
			/* Error? Fail with IRCD_ERR_CONNECT */
			ircd_connect_callback(F->fd, IRCD_ERR_CONNECT);
		return;
	}
	/* If we get here, we've suceeded, so call with IRCD_OK */
	ircd_connect_callback(F->fd, IRCD_OK);
}


int
ircd_connect_sockaddr(int fd, struct sockaddr *addr, socklen_t len)
{
	fde_t *F = find_fd(fd);
	
	if(F == NULL)
		return 0;

	memcpy(addr, &F->connect.hostaddr, len);
	return 1;
}

/*
 * ircd_error_str() - return an error string for the given error condition
 */
const char *
ircd_errstr(int error)
{
	if(error < 0 || error >= IRCD_ERR_MAX)
		return "Invalid error number!";
	return ircd_err_str[error];
}


int
ircd_socketpair(int family, int sock_type, int proto, int *nfd, const char *note)
{
	if(number_fd >= ircd_maxconnections)
	{
		errno = ENFILE;
		return -1;
	}

#ifndef __MINGW32__
	if(socketpair(family, sock_type, proto, nfd))
#else
	if(ircd_inet_socketpair(AF_INET, SOCK_STREAM, proto, nfd))
#endif
		return -1;

	ircd_fd_hack(&nfd[0]);
	ircd_fd_hack(&nfd[1]);

	ircd_open(nfd[0], FD_SOCKET, note);
	ircd_open(nfd[1], FD_SOCKET, note);

	if(nfd[0] < 0)
	{
		close(nfd[1]);
		return -1;
	}
	/* Set the socket non-blocking, and other wonderful bits */
	if(unlikely(!ircd_set_nb(nfd[0])))
	{
		ircd_lib_log("ircd_open: Couldn't set FD %d non blocking: %s", nfd[0], strerror(errno));
		ircd_close(nfd[0]);
		ircd_close(nfd[1]);
		return -1;
	}

	if(unlikely(!ircd_set_nb(nfd[1])))
	{
		ircd_lib_log("ircd_open: Couldn't set FD %d non blocking: %s", nfd[1], strerror(errno));
		ircd_close(nfd[0]);
		ircd_close(nfd[1]);
		return -1;
	}

	return 0;
}


int
ircd_pipe(int *fd, const char *desc)
{
#ifndef __MINGW32__
	if(number_fd >= ircd_maxconnections)
	{
		errno = ENFILE;
		return -1;
	}
	if(pipe(fd) == -1)
		return -1;
	ircd_fd_hack(&fd[0]);
	ircd_fd_hack(&fd[1]);
	ircd_open(fd[0], FD_PIPE, desc);
	ircd_open(fd[1], FD_PIPE, desc);
	return 0;
#else
	/* Its not a pipe..but its selectable.  I'll take dirty hacks
	 * for $500 Alex.
	 */
	return ircd_socketpair(AF_INET, SOCK_STREAM, 0, fd, desc); 
#endif
}

/*
 * ircd_socket() - open a socket
 *
 * This is a highly highly cut down version of squid's ircd_open() which
 * for the most part emulates socket(), *EXCEPT* it fails if we're about
 * to run out of file descriptors.
 */
int
ircd_socket(int family, int sock_type, int proto, const char *note)
{
	int fd;
	/* First, make sure we aren't going to run out of file descriptors */
	if(unlikely(number_fd >= ircd_maxconnections))
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
	ircd_fd_hack(&fd);
	if(unlikely(fd < 0))
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
			ircd_lib_log("ircd_socket: Could not set IPV6_V6ONLY option to 1 on FD %d: %s",
				 fd, strerror(errno));
			close(fd);
			return -1;
		}
	}
#endif

	ircd_open(fd, FD_SOCKET, note);

	/* Set the socket non-blocking, and other wonderful bits */
	if(unlikely(!ircd_set_nb(fd)))
	{
		ircd_lib_log("ircd_open: Couldn't set FD %d non blocking: %s", fd, strerror(errno));
		ircd_close(fd);
		return -1;
	}

	return fd;
}

/*
 * If a sockaddr_storage is AF_INET6 but is a mapped IPv4
 * socket manged the sockaddr.
 */
#ifdef IPV6
static void
mangle_mapped_sockaddr(struct sockaddr *in)
{
	struct sockaddr_in6 *in6 = (struct sockaddr_in6 *) in;

	if(in->sa_family == AF_INET)
		return;

	if(in->sa_family == AF_INET6 && IN6_IS_ADDR_V4MAPPED(&in6->sin6_addr))
	{
		struct sockaddr_in in4;
		memset(&in4, 0, sizeof(struct sockaddr_in));
		in4.sin_family = AF_INET;
		in4.sin_port = in6->sin6_port;
		in4.sin_addr.s_addr = ((uint32_t *) & in6->sin6_addr)[3];
		memcpy(in, &in4, sizeof(struct sockaddr_in));
	}
	return;
}
#endif

/*
 * ircd_accept() - accept an incoming connection
 *
 * This is a simple wrapper for accept() which enforces FD limits like
 * ircd_open() does.
 */
int
ircd_accept(int fd, struct sockaddr *pn, socklen_t * addrlen)
{
	int newfd;
	if(number_fd >= ircd_maxconnections)
	{
		errno = ENFILE;
		return -1;
	}

	/*
	 * Next, do the accept(). if we get an error, we should drop the
	 * reserved fd limit, but we can deal with that when ircd_open()
	 * also does it. XXX -- adrian
	 */
	newfd = accept(fd, (struct sockaddr *) pn, addrlen);
	get_errno();
	if(unlikely(newfd < 0))
		return -1;
	ircd_fd_hack(&newfd);
	ircd_open(newfd, FD_SOCKET, "Incoming connection");
	
	/* Set the socket non-blocking, and other wonderful bits */
	if(unlikely(!ircd_set_nb(newfd)))
	{
		get_errno();
		ircd_lib_log("ircd_accept: Couldn't set FD %d non blocking!", newfd);
		ircd_close(newfd);
		return -1;
	}

#ifdef IPV6
	mangle_mapped_sockaddr((struct sockaddr *)pn);
#endif
	/* .. and return */
	return newfd;
}



static void
ircd_fdlist_update_biggest(int fd, int opening)
{
	if(fd < ircd_highest_fd)
		return;

	if(unlikely(fd > ircd_highest_fd))
	{
		/*  
		 * lircd_assert that we are not closing a FD bigger than
		 * our known biggest FD
		 */
		ircd_highest_fd = fd;
		return;
	}
}


void
ircd_fdlist_init(int closeall, int maxfds)
{
	static int initialized = 0;
#ifdef __MINGW32__
	WSADATA wsaData;
	int err;

	err = WSAStartup(0x101, &wsaData);
	if(err != 0)
	{
		ircd_lib_die("WSAStartup failed");
	}

#endif
	if(!initialized)
	{
		ircd_maxconnections = maxfds;
		if(closeall)
			ircd_close_all();
		/* Since we're doing this once .. */
		initialized = 1;
	}
	fd_heap = BlockHeapCreate(sizeof(fde_t), FD_HEAP_SIZE);
	ircd_event_add("ircd_checktimeouts", ircd_checktimeouts, NULL, 2);

}


/* Called to open a given filedescriptor */
void
ircd_open(int fd, unsigned int type, const char *desc)
{
	fde_t *F = add_fd(fd);
	lircd_assert(fd >= 0);

	if(unlikely(F->flags.open))
	{
		ircd_close(fd);
	}
	lircd_assert(!F->flags.open);
	F->fd = fd;
	F->type = type;
	F->flags.open = 1;

	ircd_fdlist_update_biggest(fd, 1);
	F->ircd_index = -1;
	if(desc)
		strlcpy(F->desc, desc, sizeof(F->desc));
	number_fd++;
}


/* Called to close a given filedescriptor */
void
ircd_close(int fd)
{
	int type;
	fde_t *F = find_fd(fd);

	lircd_assert(F->flags.open);
	/* All disk fd's MUST go through file_close() ! */
	lircd_assert(F->type != FD_FILE);
	if(unlikely(F->type == FD_FILE))
	{
		lircd_assert(F->read_handler == NULL);
		lircd_assert(F->write_handler == NULL);
	}
	ircd_setselect(F->fd, 0, NULL, NULL);
	ircd_settimeout(F->fd, 0, NULL, NULL);

	F->flags.open = 0;
	ircd_fdlist_update_biggest(fd, 0);
	number_fd--;
	type = F->type;
	remove_fd(fd);

#ifdef __MINGW32__
	if(type == FD_SOCKET)
	{
		closesocket(fd);
		return;
	} else
#endif
	close(fd);
}


/*
 * ircd_dump_fd() - dump the list of active filedescriptors
 */
void
ircd_dump_fd(DUMPCB * cb, void *data)
{
	int i;
	for (i = 0; i <= ircd_highest_fd; i++)
	{
		fde_t *F = find_fd(i);
		if(F == NULL || !F->flags.open)
			continue;

		cb(i, F->desc, data);
	}
}

/*
 * ircd_note() - set the fd note
 *
 * Note: must be careful not to overflow ircd_fd_table[fd].desc when
 *       calling.
 */
void
ircd_note(int fd, const char *format, ...)
{
	va_list args;
	fde_t *F = find_fd(fd);
	if(format)
	{
		va_start(args, format);
		ircd_vsnprintf(F->desc, FD_DESC_SZ, format, args);
		va_end(args);
	}
	else
		F->desc[0] = '\0';
}

ssize_t
ircd_read(int fd, void *buf, int count)
{
	fde_t *F = find_fd(fd);
	if(F == NULL)
		return 0;

	switch (F->type)
	{
#ifdef __MINGW32__ /* pipes are sockets for us..deal */
		case FD_PIPE:
#endif
		case FD_SOCKET:
		{
			int ret;
			ret = recv(fd, buf, count, 0);

			if(ret < 0)
			{
				get_errno();
			}
			return ret;
		}
#ifndef __MINGW32__	
		case FD_PIPE:
#endif
		default:
			return read(fd, buf, count);
	}

}

ssize_t
ircd_write(int fd, const void *buf, int count)
{
	fde_t *F = find_fd(fd);
	if(F == NULL)
		return 0;

	switch (F->type)
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
					ircd_lib_log("Unknown SSL_write error:%s",
						 ERR_error_string(ERR_get_error(), NULL));
					return -1;

				}
			}
			return 0;
		}
#endif

#ifdef __MINGW32__
	case FD_PIPE:
#endif
	case FD_SOCKET:
		{
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif		
			int ret = send(fd, buf, count, MSG_NOSIGNAL);
			if(ret < 0) {
				get_errno();
			}
			return ret;
		}
#ifndef __MINGW32__
	case FD_PIPE:
#endif
	default:
		{
			return write(fd, buf, count);
		}
	}
}

#ifdef __MINGW32__
static int
writev(int fd, const struct iovec *iov, size_t iovcnt)
{
	size_t i;
	char *base;
	int TotalBytesWritten = 0, len;
	int ret;

	for (i = 0; i < iovcnt; i++)
	{
		base = iov[i].iov_base;
		len = iov[i].iov_len;
		ret = ircd_write(fd, base, len);
		if(ret == 0)
			return -1;
		TotalBytesWritten += ret;
	}
	return (int) TotalBytesWritten;

}
#endif

#ifdef USE_WRITEV
ssize_t
ircd_writev(int xfd, const struct iovec *vector, int count)
{
	fde_t *F = find_fd(xfd);

	switch (F->type)
	{
	case FD_SOCKET:
		{
			return writev(xfd, vector, count);
		}
	case FD_PIPE:
	default:
		return writev(xfd, vector, count);
	}
}
#endif


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

static const char *
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

#define SPRINTF(x) ((size_t)ircd_sprintf x)

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
	best.len = 0;
	cur.base = -1;
	cur.len = 0;
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

	if((unsigned int) (tp - tmp) > size)
	{
		return (NULL);
	}
	return strcpy(dst, tmp);
}
#endif

int
ircd_inet_pton_sock(const char *src, struct sockaddr *dst)
{
	if(ircd_inet_pton(AF_INET, src, &((struct sockaddr_in *) dst)->sin_addr))
	{
		((struct sockaddr_in *) dst)->sin_port = 0;
		((struct sockaddr_in *) dst)->sin_family = AF_INET;
		SET_SS_LEN(*((struct irc_sockaddr_storage *) dst), sizeof(struct sockaddr_in));
		return 1;
	}
#ifdef IPV6
	else if(ircd_inet_pton(AF_INET6, src, &((struct sockaddr_in6 *) dst)->sin6_addr))
	{
		((struct sockaddr_in6 *) dst)->sin6_port = 0;
		((struct sockaddr_in6 *) dst)->sin6_family = AF_INET6;
		SET_SS_LEN(*((struct irc_sockaddr_storage *) dst), sizeof(struct sockaddr_in6));
		return 1;
	}
#endif
	return 0;
}

const char *
ircd_inet_ntop_sock(struct sockaddr *src, char *dst, unsigned int size)
{
	switch (src->sa_family)
	{
	case AF_INET:
		return (ircd_inet_ntop(AF_INET, &((struct sockaddr_in *) src)->sin_addr, dst, size));
		break;
#ifdef IPV6
	case AF_INET6:
		return (ircd_inet_ntop(AF_INET6, &((struct sockaddr_in6 *) src)->sin6_addr, dst, size));
		break;
#endif
	default:
		return NULL;
		break;
	}
}

/* char *
 * ircd_inet_ntop(af, src, dst, size)
 *	convert a network format address to presentation format.
 * return:
 *	pointer to presentation format address (`dst'), or NULL (see errno).
 * author:
 *	Paul Vixie, 1996.
 */
const char *
ircd_inet_ntop(int af, const void *src, char *dst, unsigned int size)
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
 * ircd_inet_pton(af, src, dst)
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
ircd_inet_pton(af, src, dst)
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
			ircd_sprintf(tmp, "::ffff:%s", src);
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


#ifndef HAVE_SOCKETPAIR
int
ircd_inet_socketpair(int family, int type, int protocol, int fd[2])
{
	int listener = -1;
	int connector = -1;
	int acceptor = -1;
	struct sockaddr_in listen_addr;
	struct sockaddr_in connect_addr;
	size_t size;

	if(protocol || family != AF_INET)
	{
		errno = EAFNOSUPPORT;
		return -1;
	}
	if(!fd)
	{
		errno = EINVAL;
		return -1;
	}

	listener = socket(AF_INET, type, 0);
	if(listener == -1)
		return -1;
	memset(&listen_addr, 0, sizeof(listen_addr));
	listen_addr.sin_family = AF_INET;
	listen_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	listen_addr.sin_port = 0;	/* kernel choses port.  */
	if(bind(listener, (struct sockaddr *) &listen_addr, sizeof(listen_addr)) == -1)
		goto tidy_up_and_fail;
	if(listen(listener, 1) == -1)
		goto tidy_up_and_fail;

	connector = socket(AF_INET, type, 0);
	if(connector == -1)
		goto tidy_up_and_fail;
	/* We want to find out the port number to connect to.  */
	size = sizeof(connect_addr);
	if(getsockname(listener, (struct sockaddr *) &connect_addr, &size) == -1)
		goto tidy_up_and_fail;
	if(size != sizeof(connect_addr))
		goto abort_tidy_up_and_fail;
	if(connect(connector, (struct sockaddr *) &connect_addr, sizeof(connect_addr)) == -1)
		goto tidy_up_and_fail;

	size = sizeof(listen_addr);
	acceptor = accept(listener, (struct sockaddr *) &listen_addr, &size);
	if(acceptor == -1)
		goto tidy_up_and_fail;
	if(size != sizeof(listen_addr))
		goto abort_tidy_up_and_fail;
	close(listener);
	/* Now check we are talking to ourself by matching port and host on the
	   two sockets.  */
	if(getsockname(connector, (struct sockaddr *) &connect_addr, &size) == -1)
		goto tidy_up_and_fail;
	if(size != sizeof(connect_addr)
	   || listen_addr.sin_family != connect_addr.sin_family
	   || listen_addr.sin_addr.s_addr != connect_addr.sin_addr.s_addr
	   || listen_addr.sin_port != connect_addr.sin_port)
	{
		goto abort_tidy_up_and_fail;
	}
	fd[0] = connector;
	fd[1] = acceptor;
	return 0;

      abort_tidy_up_and_fail:
	errno = EINVAL;		/* I hope this is portable and appropriate.  */

      tidy_up_and_fail:
	{
		int save_errno = errno;
		if(listener != -1)
			close(listener);
		if(connector != -1)
			close(connector);
		if(acceptor != -1)
			close(acceptor);
		errno = save_errno;
		return -1;
	}
}

#endif
