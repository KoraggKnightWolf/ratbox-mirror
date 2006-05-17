/*
 *  ircd-ratbox: A slightly useful ircd.
 *  commio.h: A header for the network subsystem.
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 *  USA
 *
 *  $Id$
 */

#ifndef IRCD_LIB_H
# error "Do not use commio.h directly"                                   
#endif


#ifndef INCLUDED_commio_h
#define INCLUDED_commio_h


#ifdef EINPROGRESS
#define XEINPROGRESS EINPROGRESS
#else
#define XEINPROGRESS 0
#endif

#ifdef EWOULDBLOCK
#define XEWOULDBLOCK EWOULDBLOCK
#else
#define XEWOULDBLOCK 0
#endif

#ifdef EAGAIN
#define XEAGAIN EAGAIN
#else
#define XEAGAIN 0
#endif

#ifdef EINTR
#define XEINTR EINTR
#else
#define XEINTR 0
#endif

#ifdef ERESTART
#define XERESTART ERESTART
#else
#define XERESTART 0
#endif

#ifdef ENOBUFS
#define XENOBUFS ENOBUFS
#else
#define XENOBUFS 0
#endif

#define ignoreErrno(x)	((	\
x == XEINPROGRESS	||	\
x == XEWOULDBLOCK	||	\
x == XEAGAIN		||	\
x == XEINTR       	||	\
x == XERESTART    	||	\
x == XENOBUFS) ? 1 : 0)



/* Callback for completed IO events */
typedef void PF(int fd, void *);

/* Callback for completed connections */
/* int fd, int status, void * */
typedef void CNCB(int fd, int, void *);
/* callback for fd table dumps */
typedef void DUMPCB(int fd, const char *desc, void *);
/*
 * priority values used in fdlist code
 */
#define FDL_SERVER   0x01
#define FDL_BUSY     0x02
#define FDL_OPER     0x04
#define FDL_DEFAULT  0x08
#define FDL_ALL      0xFF

#define FD_DESC_SZ 128		/* hostlen + comment */


/* FD type values */
enum
{
	FD_NONE,
	FD_LOG,
	FD_FILE,
	FD_FILECLOSE,
	FD_SOCKET,
	FD_PIPE,
	FD_UNKNOWN
};

enum
{
	IRCD_OK,
	IRCD_ERR_BIND,
	IRCD_ERR_DNS,
	IRCD_ERR_TIMEOUT,
	IRCD_ERR_CONNECT,
	IRCD_ERROR,
	IRCD_ERR_MAX
};

typedef struct _fde fde_t;

struct timeout_data
{
	fde_t *F;
	dlink_node node;
	time_t timeout;
	PF *timeout_handler;
	void *timeout_data;
};

struct _fde
{
	/* New-school stuff, again pretty much ripped from squid */
	/*
	 * Yes, this gives us only one pending read and one pending write per
	 * filedescriptor. Think though: when do you think we'll need more?
	 */
	int fd;			/* So we can use the fde_t as a callback ptr */
	int type;
	char desc[FD_DESC_SZ];
	PF *read_handler;
	void *read_data;
	PF *write_handler;
	void *write_data;
	struct timeout_data *timeout;
	struct
	{
		unsigned int open:1;
		unsigned int close_request:1;
		unsigned int write_daemon:1;
		unsigned int closing:1;
		unsigned int socket_eof:1;
		unsigned int nolinger:1;
		unsigned int nonblocking:1;
		unsigned int ipc:1;
		unsigned int called_connect:1;
	}
	flags;
	struct
	{
		/* We don't need the host here ? */
		struct irc_sockaddr_storage S;
		struct irc_sockaddr_storage hostaddr;
		CNCB *callback;
		void *data;
		/* We'd also add the retry count here when we get to that -- adrian */
	}
	connect;
	int pflags;
	dlink_node node;

#ifdef SSL_ENABLED
	SSL *ssl;
#endif
};

extern dlink_list ircd_fd_table[];

void ircd_fdlist_init(int closeall, int maxfds);

void ircd_open(int, unsigned int, const char *);
void ircd_close(int);
void ircd_dump_fd(DUMPCB *, void *xdata);
#ifndef __GNUC__
void ircd_note(int fd, const char *format, ...);
#else
void ircd_note(int fd, const char *format, ...) __attribute__ ((format(printf, 2, 3)));
#endif


#if defined(HAVE_PORTS) || defined(HAVE_SIGIO)
typedef void (*ircd_event_cb_t)(void *);

typedef struct timer_data {
	timer_t		 td_timer_id;
	ircd_event_cb_t	 td_cb;
	void		*td_udata;
	int		 td_repeat;
} *ircd_event_id;

ircd_event_id ircd_schedule_event(time_t, int, ircd_event_cb_t, void *);
void ircd_unschedule_event(ircd_event_id);
#endif

#define FB_EOF  0x01
#define FB_FAIL 0x02


/* Size of a read buffer */
#define READBUF_SIZE    16384	/* used by src/packet.c and src/s_serv.c */

/* Type of IO */
#define	IRCD_SELECT_READ		0x1
#define	IRCD_SELECT_WRITE		0x2

#define IRCD_SELECT_ACCEPT		IRCD_SELECT_READ
#define IRCD_SELECT_CONNECT		IRCD_SELECT_WRITE

int ircd_set_nb(int);
int ircd_set_buffers(int, int);

int ircd_get_sockerr(int);

void ircd_settimeout(int fd, time_t, PF *, void *);
void ircd_checktimeouts(void *);
void ircd_connect_tcp(int fd, struct sockaddr *,
			     struct sockaddr *, int, CNCB *, void *, int);
int ircd_connect_sockaddr(int fd, struct sockaddr *addr, socklen_t len);

const char *ircd_errstr(int status);
int ircd_socket(int family, int sock_type, int proto, const char *note);
int ircd_socketpair(int family, int sock_type, int proto, int *nfd, const char *note);

int ircd_accept(int fd, struct sockaddr *pn, socklen_t *addrlen);
ssize_t ircd_write(int fd, const void *buf, int count);
#if defined(USE_WRITEV) 
ssize_t ircd_writev(int fd, const struct iovec *vector, int count);
#endif
ssize_t ircd_read(int fd, void *buf, int count);
int ircd_pipe(int *fd, const char *desc);

/* These must be defined in the network IO loop code of your choice */
void ircd_setselect(int fd, unsigned int type,
			   PF * handler, void *client_data);
void init_netio(void);
int read_message(time_t, unsigned char);
int ircd_select(unsigned long);
int disable_sock_options(int);
int ircd_setup_fd(int fd);

const char *ircd_inet_ntop(int af, const void *src, char *dst, unsigned int size);
int ircd_inet_pton(int af, const char *src, void *dst);
const char *ircd_inet_ntop_sock(struct sockaddr *src, char *dst, unsigned int size);
int ircd_inet_pton_sock(const char *src, struct sockaddr *dst);
int ircd_getmaxconnect(void);

#ifndef FD_HASH_SIZE
#define FD_HASH_SIZE 3000
#endif

#ifdef __MINGW32__
#define get_errno() do { errno = WSAGetLastError(); WSASetLastError(errno); } while(0)
#else
#define get_errno()
#endif

#define hash_fd(x) (fd % FD_HASH_SIZE)

static inline fde_t *
find_fd(int fd)
{
	dlink_list *hlist;
	dlink_node *ptr;
		
	if(unlikely(fd < 0))
		return NULL;

	hlist = &ircd_fd_table[hash_fd(fd)];

	DLINK_FOREACH(ptr, hlist->head)
	{
		fde_t *F = ptr->data;
		if(F->fd == fd)
			return F;
	}	
	return NULL;
}


#endif /* INCLUDED_commio_h */
