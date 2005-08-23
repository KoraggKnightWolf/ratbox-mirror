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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
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
typedef void DUMPCB(const char *string, void *);
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
	COMM_OK,
	COMM_ERR_BIND,
	COMM_ERR_DNS,
	COMM_ERR_TIMEOUT,
	COMM_ERR_CONNECT,
	COMM_ERROR,
	COMM_ERR_MAX
};

typedef struct _fde fde_t;


extern int highest_fd;
extern int number_fd;

struct Client;

struct _fde
{
	/* New-school stuff, again pretty much ripped from squid */
	/*
	 * Yes, this gives us only one pending read and one pending write per
	 * filedescriptor. Think though: when do you think we'll need more?
	 */
	int fd;			/* So we can use the fde_t as a callback ptr */
	int type;
	int comm_index;		/* where in the poll list we live */
	char desc[FD_DESC_SZ];
	PF *read_handler;
	void *read_data;
	PF *write_handler;
	void *write_data;
	PF *timeout_handler;
	void *timeout_data;
	time_t timeout;
	PF *flush_handler;
	void *flush_data;
	time_t flush_timeout;
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
#ifdef SSL_ENABLED
	SSL *ssl;
#endif
};


extern fde_t *fd_table;

void fdlist_init(int closeall, int maxfds);

extern void comm_open(int, unsigned int, const char *);
extern void comm_close(int);
extern void comm_dump(DUMPCB *, void *xdata);
#ifndef __GNUC__
extern void comm_note(int fd, const char *format, ...);
#else
extern void comm_note(int fd, const char *format, ...) __attribute__ ((format(printf, 2, 3)));
#endif


#if defined(HAVE_PORTS) || defined(HAVE_SIGIO)
typedef void (*comm_event_cb_t)(void *);

typedef struct timer_data {
	timer_t		 td_timer_id;
	comm_event_cb_t	 td_cb;
	void		*td_udata;
	int		 td_repeat;
} *comm_event_id;

extern comm_event_id comm_schedule_event(time_t, int, comm_event_cb_t, void *);
extern void comm_unschedule_event(comm_event_id);
#endif

#define FB_EOF  0x01
#define FB_FAIL 0x02


/* Size of a read buffer */
#define READBUF_SIZE    16384	/* used by src/packet.c and src/s_serv.c */

/* Type of IO */
#define	COMM_SELECT_READ		0x1
#define	COMM_SELECT_WRITE		0x2

#ifdef __MINGW32__
#define COMM_SELECT_ACCEPT		0x4
#define COMM_SELECT_CONNECT		0x8
#else
#define COMM_SELECT_ACCEPT		COMM_SELECT_READ
#define COMM_SELECT_CONNECT		COMM_SELECT_WRITE
#endif
extern int readcalls;
extern const char *const NONB_ERROR_MSG;
extern const char *const SETBUF_ERROR_MSG;

extern int comm_set_nb(int);
extern int comm_set_buffers(int, int);

extern int comm_get_sockerr(int);

extern void comm_settimeout(int fd, time_t, PF *, void *);
extern void comm_setflush(int fd, time_t, PF *, void *);
extern void comm_checktimeouts(void *);
extern void comm_connect_tcp(int fd, struct sockaddr *,
			     struct sockaddr *, int, CNCB *, void *, int);
extern const char *comm_errstr(int status);
extern int comm_socket(int family, int sock_type, int proto, const char *note);
extern int comm_socketpair(int family, int sock_type, int proto, int *nfd, const char *note);

extern int comm_accept(int fd, struct sockaddr *pn, socklen_t *addrlen);
extern ssize_t comm_write(int fd, void *buf, int count);
extern ssize_t comm_writev(int fd, struct iovec *vector, int count);
extern ssize_t comm_read(int fd, void *buf, int count);
extern int comm_can_writev(int fd);
extern int comm_pipe(int *fd, const char *desc);

/* These must be defined in the network IO loop code of your choice */
extern void comm_setselect(int fd, unsigned int type,
			   PF * handler, void *client_data, time_t timeout);
extern void init_netio(void);
extern int read_message(time_t, unsigned char);
extern int comm_select(unsigned long);
extern int disable_sock_options(int);
extern int comm_setup_fd(int fd);

const char *inetntoa(const char *in_addr);
const char *inetntop(int af, const void *src, char *dst, unsigned int size);
int inetpton(int af, const char *src, void *dst);
const char *inetntop_sock(struct sockaddr *src, char *dst, unsigned int size);
int inetpton_sock(const char *src, struct sockaddr *dst);
extern int maxconnections;
#ifdef IPV6
extern void mangle_mapped_sockaddr(struct sockaddr *in);
#else
#define mangle_mapped_sockaddr(x) 
#endif


#endif /* INCLUDED_commio_h */
