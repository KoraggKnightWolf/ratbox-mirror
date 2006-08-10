/*
 * $Id$
 */

#ifndef IRCD_LIB_H
#define IRCD_LIB_H 1

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <assert.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>

#include "setup.h"
#include "config.h"
#include "_stdint.h"

#ifdef __GNUC__
#undef alloca
#define alloca __builtin_alloca
#else
# ifdef _MSC_VER
#  include <malloc.h>
#  define alloca _alloca
# else
#  if HAVE_ALLOCA_H
#   include <alloca.h>
#  else
#   ifdef _AIX
 #pragma alloca
#   else
#    ifndef alloca /* predefined by HP cc +Olibcalls */
char *alloca ();
#    endif
#   endif
#  endif
# endif
#endif
 

#ifdef STRING_WITH_STRINGS
# include <string.h>
# include <strings.h> 
#else  
# ifdef HAVE_STRING_H
#  include <string.h>
# else  
#  ifdef HAVE_STRINGS_H
#   include <strings.h>
#  endif  
# endif  
#endif  

#ifdef __GNUC__

#ifdef likely
#undef likely
#endif
#ifdef unlikely
#undef unlikely
#endif

#if __GNUC__ == 2 && __GNUC_MINOR__ < 96 
# define __builtin_expect(x, expected_value) (x)
#endif

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

#else /* !__GNUC__ */

#define UNUSED(x) x

#ifdef likely
#undef likely
#endif
#ifdef unlikely
#undef unlikely
#endif
#define likely(x)	(x)
#define unlikely(x)	(x)
#endif



#ifdef __MINGW32__
#include <windows.h>
#include <winsock2.h>
#include <process.h>
#ifndef socklen_t
#define socklen_t unsigned int
#endif


struct iovec
{
	void *iov_base;     /* Pointer to data.  */
	size_t iov_len;     /* Length of data.  */
};
#define USE_WRITEV 1
#define UIO_MAXIOV 16
#ifndef MAXPATHLEN
#define MAXPATHLEN 128
#endif


#ifdef strerror
#undef strerror
#endif

#define strerror(x) wsock_strerror(x)
const char * wsock_strerror(int error);


#define ENOBUFS	    WSAENOBUFS
#define EINPROGRESS WSAEINPROGRESS
#define EWOULDBLOCK WSAEWOULDBLOCK
#define EMSGSIZE    WSAEMSGSIZE
#define EALREADY    WSAEALREADY
#define EISCONN     WSAEISCONN
#define EADDRINUSE  WSAEADDRINUSE
#define EAFNOSUPPORT WSAEAFNOSUPPORT

#define pipe(x)  _pipe(x, 1024, O_BINARY)
#define ioctl(x,y,z)  ioctlsocket(x,y, (u_long *)z)
#define HAVE_VSNPRINTF 1

int setenv(const char *, const char *, int);
int kill(int pid, int sig);
#define WNOHANG 1
pid_t waitpid(pid_t pid, int *status, int options);
pid_t getpid(void);
unsigned int geteuid(void);

#ifndef SIGKILL
#define SIGKILL SIGTERM
#endif

#else
/* unixy stuff */
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#ifndef UIO_MAXIOV 
#define UIO_MAXIOV      16
#endif

#if defined(HAVE_WRITEV) && defined(HAVE_SENDMSG) && !defined(__CYGWIN__)
#define USE_WRITEV 1
#endif 

#ifdef __MINGW32__
struct iovec
{
	void *iov_base;     /* Pointer to data.  */
	size_t iov_len;     /* Length of data.  */
};
#endif
            
            

#endif

#endif



#ifndef HOSTIPLEN
#define HOSTIPLEN	53
#endif

#ifdef SOFT_ASSERT
#ifdef __GNUC__
#define lircd_assert(expr)	do								\
			if(unlikely(!(expr))) {							\
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
			if(unlikely(!(expr))) {							\
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

char *ircd_ctime(const time_t, char *);
char *ircd_date(const time_t, char *, size_t);
void ircd_lib_log(const char *, ...);
void ircd_lib_restart(const char *, ...);
void ircd_lib_die(const char *, ...);
void ircd_set_time(void);
void ircd_lib(log_cb *xilog, restart_cb *irestart, die_cb *idie, int closeall, int maxfds, size_t lb_hp_size, size_t dh_size);
time_t ircd_current_time(void);
struct timeval *ircd_current_time_tv(void);
pid_t ircd_spawn_process(const char *, const char **);

#ifndef HAVE_STRTOK_R
char *strtok_r(char *, const char *, char **);
#endif

#ifndef HAVE_GETTIMEOFDAY
int ircd_gettimeofday(struct timeval *, void *);
#else
#define ircd_gettimeofday(x, y) gettimeofday(x, y)
#endif

#ifndef ircd_currenttime
#define ircd_currenttime ircd_current_time()
#endif

#ifndef ircd_systemtime
#define ircd_systemtime (*(ircd_current_time_tv()))
#endif

#ifdef NEED_CRYPT
char * crypt(const char *pw, const char *salt);
#else
 #ifdef HAVE_CRYPT_H
  #include <crypt.h>
 #endif
#endif

void ircd_sleep(unsigned int seconds, unsigned int useconds);

unsigned char * ircd_base64_encode(const unsigned char *str, int length);
unsigned char * ircd_base64_decode(const unsigned char *str, int length, int *ret);



#include "tools.h"
#include "ircd_memory.h"
#include "balloc.h"
#include "linebuf.h"
#include "snprintf.h"
#include "commio.h"
#include "event.h"
#include "helper.h"

#endif

