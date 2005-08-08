/*
 * $Id$
 */

#ifndef IRCD_LIB_H
#define IRCD_LIB_H 1

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <assert.h>
#include <fcntl.h>
#include <time.h>

#ifdef __MINGW32__
#define FD_SETSIZE 16384 /* this is what cygwin uses..it probably sucks too oh well*/
#include <windows.h>
#include <winsock2.h>
#include <process.h>
#ifndef socklen_t
#define socklen_t unsigned int
#endif

#ifndef MAXPATHLEN
#define MAXPATHLEN 128
#endif

#define ENOBUFS	    WSAENOBUFS
#define EINPROGRESS WSAEINPROGRESS
#define EWOULDBLOCK WSAEWOULDBLOCK
#define EMSGSIZE    WSAEMSGSIZE
#define EALREADY    WSAEALREADY
#define EISCONN     WSAEISCONN
#define EADDRINUSE  WSAEADDRINUSE
#define EAFNOSUPPORT WSAEAFNOSUPPORT

struct iovec { void *dummy; };
#define pipe(x)  _pipe(x, 1024, O_BINARY)
#define ioctl(x,y,z)  ioctlsocket(x,y, (u_long *)z)
#define HAVE_GETTIMEOFDAY 1
#define HAVE_VSNPRINTF 1

int gettimeofday(struct timeval *tv, void *tz);
int kill(int pid, int sig);
unsigned int geteuid(void);


#else
/* unixy stuff */
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "setup.h"
#include "config.h"

#endif



#ifndef HOSTIPLEN
#define HOSTIPLEN	53
#endif

#ifdef SOFT_ASSERT
#ifdef __GNUC__
#define lircd_assert(expr)	do								\
			if(!(expr)) {							\
				lib_ilog(L_MAIN, 						\
				"file: %s line: %d (%s): Assertion failed: (%s)",	\
				__FILE__, __LINE__, __PRETTY_FUNCTION__, #expr); 	\
				sendto_realops_flags(UMODE_ALL, L_ALL, 			\
				"file: %s line: %d (%s): Assertion failed: (%s)",	\
				__FILE__, __LINE__, __PRETTY_FUNCTION__, #expr);	\
			}								\
			while(0)
#else
#define lircd_assert(expr)	do								\
			if(!(expr)) {							\
				lib_ilog(L_MAIN, 						\
				"file: %s line: %d: Assertion failed: (%s)",		\
				__FILE__, __LINE__, #expr); 				\
				sendto_realops_flags(UMODE_ALL, L_ALL,			\
				"file: %s line: %d: Assertion failed: (%s)"		\
				__FILE__, __LINE__, #expr);				\
			}								\
			while(0)
#endif
#else
#define lircd_assert(expr)	assert(expr)
#endif


#ifdef IPV6
#ifndef AF_INET6
#error "AF_INET6 not defined"
#endif


#else /* #ifdef IPV6 */

#ifndef AF_INET6
#define AF_INET6 AF_MAX		/* Dummy AF_INET6 declaration */
#endif
#endif /* #ifdef IPV6 */


#ifdef IPV6
#define irc_sockaddr_storage sockaddr_storage
#else
#define irc_sockaddr_storage sockaddr
#define ss_family sa_family

#ifdef SOCKADDR_IN_HAS_LEN
#define ss_len sa_len
#endif
#endif

#ifdef SOCKADDR_IN_HAS_LEN
#define SET_SS_LEN(x, y) ((struct irc_sockaddr_storage)(x)).ss_len = y
#define GET_SS_LEN(x) x.ss_len
#else
#define SET_SS_LEN(x, y)
#ifdef IPV6
#define GET_SS_LEN(x) x.ss_family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6)
#else
#define GET_SS_LEN(x) sizeof(struct sockaddr_in)
#endif
#endif

#ifndef INADDRSZ
#define INADDRSZ 4
#endif

#ifdef IPV6
#ifndef IN6ADDRSZ
#define IN6ADDRSZ 16
#endif
#endif 
  
#ifndef INT16SZ
#define INT16SZ 2
#endif


typedef void log_cb(const char *buffer);
typedef void restart_cb(const char *buffer);
typedef void die_cb(const char *buffer);

void lib_ilog(const char *, ...);
void lib_restart(const char *, ...);
void lib_die(const char *, ...);
void set_time(void);
void ircd_lib(log_cb *xilog, restart_cb *irestart, die_cb *idie, int closeall, int maxfds, size_t lb_hp_size, size_t dh_size);
extern struct timeval SystemTime;  

#ifndef CurrentTime
#define CurrentTime SystemTime.tv_sec
#endif

#include "tools.h"
#include "ircd_memory.h"
#include "balloc.h"
#include "linebuf.h"
#include "snprintf.h"
#include "commio.h"
#include "event.h"


#endif























