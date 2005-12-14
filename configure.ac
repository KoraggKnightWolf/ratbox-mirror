dnl $Id$
dnl Process this file with autoconf to produce a configure script.

dnl TODO: clean up all the OpenSSL and shared module checking stuff;
dnl the most major changes have already been made and it looks like
dnl said functions need to be just about as complex as they already are.

AC_PREREQ(2.57)
AUTOMAKE_OPTIONS = 1.8
dnl Sneaky way to get an Id tag into the configure script
AC_COPYRIGHT([$Id$])
AC_INIT([ircd-ratbox],[devel])
AM_INIT_AUTOMAKE

AM_CONFIG_HEADER(include/setup.h)

AC_PREFIX_DEFAULT(/usr/local/ircd)
AC_GNU_SOURCE

dnl Checks for programs.
AM_PROG_LEX
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_EGREP

if test "$enable_static" = "yes"; then
	AM_DISABLE_SHARED
	AM_ENABLE_STATIC	
        AC_DEFINE([STATIC_MODULES], 1, [Define to 1 if dynamic modules can't be used.])
else
	enable_static="no"
fi

F77=no

AC_LIBLTDL_CONVENIENCE
AC_SUBST(INCLTDL)
AC_SUBST(LIBLTDL)
AC_LIBTOOL_DLOPEN
AM_PROG_CC_C_O
AM_DISABLE_STATIC
AM_ENABLE_SHARED
AM_MAINTAINER_MODE

AC_ISC_POSIX
AC_C_INLINE
AC_PROG_GCC_TRADITIONAL
AC_PROG_MAKE_SET
AC_PROG_INSTALL
AC_PATH_PROG(AUTOMAKE, automake)
AC_PATH_PROG(ACLOCAL, aclocal)
AC_PATH_PROG(AUTOHEADER, autoheader)
AC_PATH_PROG(AS, as)
AC_PATH_PROG(RM, rm)
AC_PATH_PROG(CP, cp)
AC_PATH_PROG(MV, mv)
AC_PATH_PROG(LN, ln)
AC_PATH_PROG(SED, sed)
AC_PATH_PROG(AR, ar)
AC_PATH_PROG(LD, ld)
AC_PATH_PROG(RANLIB, ranlib)
AC_PATH_PROG(TOUCH, touch)
#AC_PATH_PROG(LEX, flex, [no])
AC_PROG_YACC
AC_LANG(C)

AM_CONDITIONAL([STATIC_MODULES], test "$enable_static" = "yes")
AC_PROG_LIBTOOL

LIBTOOL="$LIBTOOL --silent"

AC_CONFIG_SUBDIRS(libltdl)

RESOLVER="adns resolver"

case "$host_os" in
*cygwin*)
     AC_DEFINE_UNQUOTED(CYGWIN,1,[This is a Cygwin system])
     AC_DEFINE_UNQUOTED(WINDOWS,1,[This is a Windows system])
     ;;
*mingw*)
     AC_DEFINE_UNQUOTED(MINGW,1,[This is a MinGW system])
     AC_DEFINE_UNQUOTED(WINDOWS,1,[This is a Windows system])
     AC_CHECK_HEADER(winsock2.h, , [AC_MSG_ERROR([** MinGW and no winsock2.h.  I give up.])])
     LIBS="$LIBS -lws2_32"
     is_mingw="yes"
     ;;
*)
     ;;
esac

AM_CONDITIONAL([MINGW], [test "$is_mingw" = "yes"])


AC_SUBST(RESOLVER)



if test "$LEX" != flex; then
  LEX="$SHELL $missing_dir/missing flex"
  AC_SUBST(LEX_OUTPUT_ROOT, lex.yy)
  AC_SUBST(LEXLIB, '')
fi

# backwards support for IRC_CFLAGS
CFLAGS="$IRC_CFLAGS $CFLAGS -O0 -Wall"

dnl use directory structure of cached as default (hack)
if test "$libexecdir" = '${exec_prefix}/libexec' &&
   test "$localstatedir" = '${prefix}/var'; then
	libexecdir='${bindir}'
	localstatedir='${prefix}'
fi

dnl Checks for header files.
AC_HEADER_STDC

AC_HEADER_SYS_WAIT
AC_HEADER_DIRENT
AC_HEADER_TIME

AC_CHECK_HEADERS([netdb.h crypt.h sys/time.h sys/types.h sys/socket.h netinet/in.h netinet/tcp.h arpa/inet.h sys/resource.h sys/param.h errno.h sys/syslog.h stddef.h  sys/epoll.h sys/uio.h])


dnl Networking Functions
dnl ====================

AC_SEARCH_LIBS(socket, [socket],,)


AC_CHECK_MEMBER([struct sockaddr.sa_len], [AC_DEFINE(SOCKADDR_IN_HAS_LEN, 1, [Define to 1 if sockaddr has a 'sa_len'
member.])],,[[
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_WINSOCK2_H
#include <winsock2.h>
#endif
]])

AC_CHECK_TYPE(socklen_t, ,
[AC_DEFINE([socklen_t], [unsigned int],
[If we don't have a real socklen_t, unsigned int is good enough.])],
[
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_WINSOCK2_H
#include <winsock2.h>
#endif
])

AC_ARG_ENABLE(ipv6,
AC_HELP_STRING([--enable-ipv6],[Enable IPv6 support]),[ipv6=$enableval],[ipv6=no])

if test $ipv6 != yes; then
	have_v6="no"
else
AC_MSG_CHECKING([for core IPv6 support])

AC_COMPILE_IFELSE(
[AC_LANG_PROGRAM(
	[[#define IN_AUTOCONF
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netinet/in.h>]],
	[[struct sockaddr_in6 s; 
	  s.sin6_family = 0;]]
	)],
[
	if test "$CYGWIN" = "yes"; then
		AC_MSG_RESULT([no, Cygwin's IPv6 is incomplete])
		have_v6=no
	else
		have_v6=yes
		AC_DEFINE(IPV6, 1, [Define if IPv6 support is present and available.])
		AC_MSG_RESULT(yes)
		AC_MSG_CHECKING([for struct in6addr_any])
		AC_COMPILE_IFELSE(
			[AC_LANG_PROGRAM(
				[[#define IN_AUTOCONF
				#include <sys/types.h>
				#include <sys/socket.h>
				#include <netinet/in.h>]],
				[[struct in6_addr a = in6addr_any; void foo() { struct in6_addr *p; p = &a;  }]]
			)],
			[AC_MSG_RESULT(yes)],
			[
				AC_MSG_RESULT(no)
				AC_DEFINE(NO_IN6ADDR_ANY, 1, [Define to 1 if your system has no in6addr_any.])
				inet_misc=1
			]
		)
	fi
],
[AC_MSG_RESULT(no)
have_v6="no"])
fi

save_LIBS=$LIBS
AC_SEARCH_LIBS(crypt, [crypt descrypt],,)
LIBS=$save_LIBS

CRYPT_LIB=$ac_cv_search_crypt

if test "$CRYPT_LIB" = "none required"; then
	unset CRYPT_LIB
elif test "$CRYPT_LIB" = no; then
	need_crypt=yes;
	AC_DEFINE(NEED_CRYPT, 1, [Define if your system needs crypt.])
	unset CRYPT_LIB
fi

AM_CONDITIONAL([NEED_CRYPT], [test x"$need_crypt" == "xyes"])


AC_SUBST(CRYPT_LIB)

dnl See whether we can include both string.h and strings.h.
AC_CACHE_CHECK([whether string.h and strings.h may both be included],
gcc_cv_header_string,
[
	AC_COMPILE_IFELSE(
	[#include <string.h>
	#include <strings.h>], 
	[gcc_cv_header_string=yes],
	[gcc_cv_header_string=no])
])

if test "$gcc_cv_header_string" = "yes"; then
	AC_DEFINE(STRING_WITH_STRINGS, 1, [Define to 1 if string.h may be included along with strings.h])
fi

AC_C_BIGENDIAN

dnl Check for stdarg.h - if we can't find it, halt configure
AC_CHECK_HEADER(stdarg.h, , [AC_MSG_ERROR([** stdarg.h could not be found - ircd-ratbox will not compile without it **])])

dnl Checks for the existence of strlcat, strlcpy, basename...
dnl This more reliable test only works with gcc though.

if test "$ac_cv_c_compiler_gnu" = yes; then

AC_MSG_CHECKING(for strlcpy)
save_CFLAGS=$CFLAGS
CFLAGS="$CFLAGS -Wimplicit -Werror"

AC_LINK_IFELSE(
	[AC_LANG_PROGRAM(
		[[#include <string.h>
		#include <stdlib.h>]],
		[[char *a = malloc(6);
		strlcpy(a, "hello", 6);]]
	)],
	[AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_STRLCPY, 1, [Define if strlcpy is available (most BSDs.)])],
	[AC_MSG_RESULT(no)]
)

AC_MSG_CHECKING(for strlcat)
AC_LINK_IFELSE(
	[AC_LANG_PROGRAM(
		[[#include <string.h>
		#include <stdlib.h>]],
		[[char *a = malloc(6);
		a[0] = '\0';
		strlcat(a, "hello", 6);]]
	)],
	[AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_STRLCAT, 1, [Define if strlcat is available (most BSDs.)])],
	[AC_MSG_RESULT(no)]
)

CFLAGS=$save_CFLAGS

else

dnl Better than nothing. The more complicated test above probably fixes powerpc,
dnl so who cares.

AC_CHECK_FUNCS([strlcat strlcpy])

fi

AX_CREATE_STDINT_H([include/_stdint.h])

AC_CHECK_TYPE([in_port_t], [],
[AC_DEFINE(in_port_t, [u_int16_t], [If system does not define in_port_t, define it to what it should be.])],
[[#include <sys/types.h>
#include <netinet/in.h>]])

AC_CHECK_TYPE([sa_family_t], [],
[AC_DEFINE(sa_family_t, [u_int16_t], [If system does not define sa_family_t, define it here.])],
[[#include <sys/types.h>
#include <sys/socket.h>]])

AC_CHECK_TYPES([uintptr_t])

dnl check for various functions...
AC_CHECK_FUNCS([socketpair vsnprintf mmap gettimeofday strdup strndup writev sendmsg ctime_r strtok_r])

AC_FUNC_ALLOCA
AC_FUNC_VFORK

dnl Specialized functions checks
dnl ============================


dnl OpenSSL support
AC_MSG_CHECKING(for OpenSSL)
AC_ARG_ENABLE(openssl,
[AC_HELP_STRING([--enable-openssl[=DIR]],[Enable OpenSSL support (DIR optional).])
AC_HELP_STRING([--disable-openssl],[Disable OpenSSL support.])],
[cf_enable_openssl=$enableval],
[cf_enable_openssl="auto"])

if test "$cf_enable_openssl" != "no" ; then
	cf_openssl_basedir=""
	if test "$cf_enable_openssl" != "auto" &&
	test "$cf_enable_openssl" != "yes" ; then
		dnl Support for --enable-openssl=/some/place
		cf_openssl_basedir="`echo ${cf_enable_openssl} | sed 's/\/$//'`"
	else
	dnl Do the auto-probe here.  Check some common directory paths.
		for dirs in /usr/local/ssl /usr/pkg /usr/local \
		/usr/local/openssl ; do
			if test -f "${dirs}/include/openssl/opensslv.h" ; then
				cf_openssl_basedir="${dirs}"
			break
			fi
		done
		unset dirs
	fi
	dnl Now check cf_openssl_found to see if we found anything.
	if test ! -z "$cf_openssl_basedir"; then
		if test -f "${cf_openssl_basedir}/include/openssl/opensslv.h" ; then
			SSL_INCLUDES="-I${cf_openssl_basedir}/include"
			SSL_LIBS="-L${cf_openssl_basedir}/lib"
		else
		dnl OpenSSL wasn't found in the directory specified.  Naughty
		dnl administrator...
		cf_openssl_basedir=""
		fi
	else
	dnl Check for stock FreeBSD 4.x and 5.x systems, since their files
	dnl are in /usr/include and /usr/lib.  In this case, we don't want to
	dnl change INCLUDES or LIBS, but still want to enable OpenSSL.
	dnl We can't do this check above, because some people want two versions
	dnl of OpenSSL installed (stock FreeBSD 4.x/5.x and /usr/local/ssl)
	dnl and they want /usr/local/ssl to have preference.
		if test -f "/usr/include/openssl/opensslv.h" ; then
			cf_openssl_basedir="/usr"
		fi
	fi

	dnl If we have a basedir defined, then everything is okay.  Otherwise,
	dnl we have a problem.
	if test ! -z "$cf_openssl_basedir"; then
		AC_MSG_RESULT($cf_openssl_basedir)
		cf_enable_openssl="yes"
	else
		AC_MSG_RESULT([not found. Specify a correct path?])
		cf_enable_openssl="no"
	fi
	unset cf_openssl_basedir
else
	dnl If --disable-openssl was specified
	AC_MSG_RESULT(disabled)
fi

save_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $SSL_INCLUDES"
save_LIBS="$LIBS"
LIBS="$LIBS $SSL_LIBS"
if test "$cf_enable_openssl" != no; then
	dnl Check OpenSSL version (must be 0.9.6 or above!)
	AC_MSG_CHECKING(for OpenSSL 0.9.6 or above)
	AC_RUN_IFELSE(
		AC_LANG_PROGRAM(
	    	[#include <openssl/opensslv.h>
		#include <stdlib.h>],
		[[if ( (OPENSSL_VERSION_NUMBER & 0x00906000) == 0x00906000)
		exit(1); else exit(0);]]),
	cf_openssl_version_ok=yes,
	cf_openssl_version_ok=no,
	cf_openssl_version_ok=no)

	if test "$cf_openssl_version_ok" = yes; then
		AC_MSG_RESULT(found)

		dnl Do all the HAVE_LIBCRYPTO magic -- and check for ciphers
		CPPFLAGS="$CPPFLAGS $SSL_LIBS"
		AC_CHECK_LIB(crypto, RSA_free)
		SSL_LIBS="$SSL_LIBS -lcrypto"
		SSL_SRCS_ENABLE='$(SSL_SRCS)'
	else
	        AC_MSG_RESULT(no - OpenSSL support disabled)
	fi
fi

CPPFLAGS="$save_CPPFLAGS"
LIBS="$save_LIBS"

dnl End OpenSSL detection


dnl Specialized functions and libraries
dnl ===================================

AC_ARG_WITH(zlib-path,
AC_HELP_STRING([--with-zlib-path=DIR],[Path to libz.so for ziplinks support.]),
[LIBS="$LIBS -L$withval"],)

AC_ARG_ENABLE(zlib,
AC_HELP_STRING([--disable-zlib],[Disable ziplinks support]),
[zlib=$enableval],[zlib=yes])
 
if test "$zlib" = yes; then

AC_CHECK_HEADER(zlib.h, [
	AC_CHECK_LIB(z, zlibVersion,
	[
		AC_SUBST(ZLIB_LD, -lz)
		AC_DEFINE(HAVE_LIBZ, 1, [Define to 1 if zlib (-lz) is available.])
	], zlib=no)
], zlib=no)

fi

if test "$zlib" = "yes"; then
	SERVLINK=servlink
	AC_SUBST(SERVLINK)
fi

dnl IO Loop Selection
dnl =================

AC_ARG_ENABLE(poll, AC_HELP_STRING([--enable-poll],[Force poll() usage.]),
[ if test $enableval = yes; then 
	SELECT_TYPE_EXPLICIT="poll"
  else
  	use_poll=no
  fi
],)

AC_ARG_ENABLE(select, AC_HELP_STRING([--enable-select],[Force select() usage.]),
[ if test $enableval = yes; then 
	SELECT_TYPE_EXPLICIT="select" 
  else
  	use_select=no
  fi
],)

AC_ARG_ENABLE(kqueue, AC_HELP_STRING([--enable-kqueue],[Force kqueue() usage.]),
[ if test $enableval = yes; then 
	SELECT_TYPE_EXPLICIT="kqueue"
  else
  	use_kqueue=no
  fi
],)

AC_ARG_ENABLE(ports, AC_HELP_STRING([--enable-ports],[Force Solaris ports usage.]),
[ if test $enableval = yes; then
	SELECT_TYPE_EXPLICIT="ports"
  else
	use_ports=no
  fi
],)

AC_ARG_ENABLE(devpoll,AC_HELP_STRING([--enable-devpoll],[Force usage of /dev/poll.]),
[ if test $enableval = yes; then 
	SELECT_TYPE_EXPLICIT="devpoll" 
	dnl These need to be defined or not defined
	AC_CHECK_HEADERS([sys/devpoll.h devpoll.h])
  else
  	use_devpoll=no;
  fi
],)

AC_ARG_ENABLE(epoll, AC_HELP_STRING([--enable-epoll],[Force sys_epoll usage (Linux only).]),
[ if test $enableval = yes; then 
	SELECT_TYPE_EXPLICIT="epoll"
  else
  	use_epoll=no
  fi
],)

AC_ARG_ENABLE(rtsigio, AC_HELP_STRING([--enable-rtsigio],[Force rtsigio usage (Linux only).]),
[ if test $enableval = yes; then
        SELECT_TYPE_EXPLICIT="sigio"
  else
        use_sigio=no
  fi
],)


dnl **********************************************************************
dnl Check for --with-confdir
dnl **********************************************************************

AC_MSG_CHECKING([whether to modify confdir])
AC_ARG_WITH(confdir, 
AC_HELP_STRING([--with-confdir=DIR],
	       [Directory to install config files.]),
	       [ confdir=`echo $withval | sed 's/\/$//'`
		 AC_MSG_RESULT(yes)
		 AC_DEFINE_DIR(ETC_DIR, confdir, [Prefix where config files are installed.])
		 AC_SUBST_DIR([confdir]) ],
	       [ confdir='${prefix}/etc'
		 AC_MSG_RESULT(no)
	         AC_DEFINE_DIR(ETC_DIR, confdir, [Prefix where config files are installed.])
	  	 AC_SUBST_DIR([confdir])]
)

dnl **********************************************************************
dnl Check for --with-logdir
dnl **********************************************************************

AC_MSG_CHECKING([whether to modify logdir])
AC_ARG_WITH(logdir, 
AC_HELP_STRING([--with-logdir=DIR],
	       [Directory where to write logfiles.]),
	       [ logdir=`echo $withval | sed 's/\/$//'`
		 AC_MSG_RESULT(yes)
		 AC_DEFINE_DIR(LOG_DIR, logdir, [Prefix where to write logfiles.])
		 AC_SUBST_DIR([logdir]) ],
	       [ logdir='${prefix}/logs'
		 AC_MSG_RESULT(no)
	         AC_DEFINE_DIR(LOG_DIR, logdir, [Prefix where to write logfiles.])
	  	 AC_SUBST_DIR([logdir])]
)

dnl **********************************************************************
dnl Check for --with-helpdir
dnl **********************************************************************

AC_MSG_CHECKING([whether to modify helpdir])
AC_ARG_WITH(helpdir, 
AC_HELP_STRING([--with-helpdir=DIR],
	       [Directory to install help files.]),
	       [ helpdir=`echo $withval | sed 's/\/$//'`
		 AC_MSG_RESULT(yes)
		 AC_DEFINE_DIR(HELP_DIR, helpdir, [Prefix where help files are installed.])
		 AC_SUBST_DIR([helpdir]) ],
	       [ helpdir='${prefix}/help'
		 AC_MSG_RESULT(no)
	         AC_DEFINE_DIR(HELP_DIR, helpdir, [Prefix where help file are installed.])
	  	 AC_SUBST_DIR([helpdir])]
)

dnl **********************************************************************
dnl Check for --with-moduledir
dnl **********************************************************************

AC_MSG_CHECKING([whether to modify moduledir])
AC_ARG_WITH(moduledir, 
AC_HELP_STRING([--with-moduledir=DIR],
	       [Directory to install modules.]),
	       [ moduledir=`echo $withval | sed 's/\/$//'`
		 AC_MSG_RESULT(yes)
		 AC_DEFINE_DIR(MODULE_DIR, moduledir, [Prefix where modules are installed.])
		 AC_SUBST_DIR([moduledir]) ],
	       [ moduledir='${prefix}/modules'
		 AC_MSG_RESULT(no)
	         AC_DEFINE_DIR(MODULE_DIR, moduledir, [Prefix where modules are installed.])
	  	 AC_SUBST_DIR([moduledir])]
)

dnl The force type assumes the users knows what they are doing..
dnl if it doesn't work, their problem.
if test ! -z "$SELECT_TYPE_EXPLICIT"; then
	SELECT_TYPE="$SELECT_TYPE_EXPLICIT";
	echo "Forcing $SELECT_TYPE to be enabled"
	if test "x$SELECT_TYPE" = "xports" ; then
		AC_DEFINE(HAVE_PORTS, 1, [using ports])
	fi
	if test "x$SELECT_TYPE" = "xsigio" ; then
		AC_DEFINE(HAVE_SIGIO, 1, [using rtsigio])
	fi
else

if test ! "x$use_select" = "xno"; then
	AC_CHECK_FUNCS(select, [haveselect=yes], [haveselect=no])
	if test "x$haveselect" = "xyes" ; then
		SELECT_TYPE="select"
	fi
fi


if test ! "x$use_poll" = "xno"; then
	AC_CHECK_FUNCS(poll, [havepoll=yes], [havepoll=no])
	if test "x$havepoll" = "xyes" ; then
		SELECT_TYPE="poll"
	fi
fi

if test ! "x$use_devpoll" = "xno"; then
	AC_MSG_CHECKING(for /dev/poll)
	if test -c "/dev/poll"; then
	        AC_MSG_RESULT(yes)
		AC_CHECK_HEADERS([devpoll.h sys/devpoll.h])
	        SELECT_TYPE="devpoll"
	else
		AC_MSG_RESULT(no)	
	fi
fi

if test ! "x$use_kqueue" = "xno"; then
	AC_CHECK_FUNCS(kevent, [havekqueue=yes], [havekqueue=no])
	if test "x$havekqueue" = "xyes" ; then
		SELECT_TYPE="kqueue"
	fi
fi

if test ! "x$use_ports" = "xno"; then
	AC_CHECK_FUNCS(port_create, [haveports=yes], [haveports=no])
	if test "x$haveports" = "xyes" ; then
		SELECT_TYPE="ports"
		AC_DEFINE(HAVE_PORTS, 1, [using ports])
	fi
fi

if test ! "x$use_rtsigio" = "xno"; then
	havertsig=no
	if test "x$havepoll" = "xyes" ; then
		AC_CHECK_FUNCS(sigtimedwait, [havertsig=yes], )
	fi

	if test "x$havertsig" = "xyes" ; then
		AC_MSG_CHECKING(for F_SETSIG in fcntl.h)
		AC_EGREP_CPP(yes,
[
#define _GNU_SOURCE
#include <fcntl.h>
#ifdef F_SETSIG
yes
#endif
],	[ AC_MSG_RESULT(yes) ], [ AC_MSG_RESULT(no); havertsig=no])

	fi

	if test "x$havertsig" = "xyes"; then
		SELECT_TYPE="sigio"
		AC_DEFINE(HAVE_SIGIO, 1, [using rtsigio])
	fi
fi

if test ! "x$use_epoll" = "xno"; then
	AC_CHECK_FUNCS(epoll_ctl, [haveepoll=yes], [haveepoll=no])
	if test "x$ac_cv_header_sys_epoll_h" = "xyes"; then
		if test "x$haveepoll" = "xyes" ; then
                	AC_MSG_CHECKING(for epoll support in kernel)
	                AC_TRY_RUN(
#include <sys/param.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/syscall.h>
#include <unistd.h>     

int
main(int argc, char **argv)
{
        int epfd;

        epfd = epoll_create(256);
        exit (epfd == -1 ? 1 : 0);
}, [AC_MSG_RESULT(yes)
	    AC_DEFINE(HAVE_EPOLL, 1,
	        [Define if your system supports the epoll system calls])
	    SELECT_TYPE="epoll"], 
	    AC_MSG_RESULT(no), AC_MSG_RESULT(no))
		fi
	fi

haveepollsyscall=no

if test "x$ac_cv_header_sys_epoll_h" = "xyes"; then
	if test "x$haveepoll" = "xno" ; then
                AC_MSG_CHECKING(for epoll system call)
                AC_TRY_RUN(
#include <sys/param.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/syscall.h>
#include <unistd.h>     

int
epoll_create(int size)
{
        return (syscall(__NR_epoll_create, size));
}
 
int
main(int argc, char **argv)
{
        int epfd;

        epfd = epoll_create(256);
        exit (epfd == -1 ? 1 : 0);
}, [AC_MSG_RESULT(yes)
    AC_DEFINE(HAVE_EPOLL, 1,
        [Define if your system supports the epoll system calls])
    SELECT_TYPE="epoll"], 
    AC_MSG_RESULT(no), AC_MSG_RESULT(no))
	fi
fi

fi

fi

if test "$is_mingw" = "yes"; then
	SELECT_TYPE="win32"
fi

if test -z "$SELECT_TYPE"; then
	AC_MSG_WARN([Unable to find an i/o interface using select],)
	SELECT_TYPE="select"
fi

echo "Using $SELECT_TYPE for select loop."

case  $SELECT_TYPE in
	epoll)
		AC_DEFINE([USE_EPOLL], 1, [Define to use epoll])
		;;
	kqueue) 
		AC_DEFINE([USE_KQUEUE], 1, [Define to use kqueue])
		;;
	ports)
		AC_DEFINE([USE_PORTS], 1, [Define to use ports])
		AC_CHECK_FUNC(timer_create,,[AC_CHECK_LIB(rt, timer_create, AC_DEFINE(HAVE_TIMER_CREATE, 1, [Have timer_create]) LIBS="${LIBS} -lrt",,)])
		;;
	poll)
		AC_DEFINE([USE_POLL], 1, [Define to use poll])
		;;
	sigio)
		AC_DEFINE([USE_SIGIO],1 , [Define to use sigio])
		AC_CHECK_FUNC(timer_create,,[AC_CHECK_LIB(rt, timer_create, AC_DEFINE(HAVE_TIMER_CREATE, 1, [Have timer_create]) LIBS="${LIBS} -lrt",,)])
		;;
	select)
		AC_DEFINE([USE_SELECT], 1, [Define to use select])
		;;
	win32)
		AC_DEFINE([USE_WIN32], 1, [Define to use win32 io])
		;;
	*)
		AC_DEFINE([USE_SELECT], 1, [Define to use select])
		;;
esac
	
AC_DEFINE_UNQUOTED(SELECT_TYPE, "$SELECT_TYPE", [This is the type of IO loop we are using])
AC_SUBST(SELECT_TYPE)

dnl Debug-related options
dnl =====================

AC_ARG_ENABLE(assert,
AC_HELP_STRING([--enable-assert],[Enable assert(). Choose between soft(warnings) and hard(aborts the daemon)]),
[assert=$enableval], [assert=no])

if test "$assert" = no; then
	AC_DEFINE(NDEBUG, 1, [Define this to disable debugging support.])
elif test "$assert" = soft; then
	AC_DEFINE(SOFT_ASSERT, 1, [Define this to enable soft asserts.])
	AC_DEFINE(NDEBUG, 1, [Define this to disable debugging support.])
elif test "$assert" = yes; then
	assert = "hard";
fi

AC_MSG_CHECKING(if you want IO Debugging hooks)
AC_ARG_ENABLE(iodebug,
AC_HELP_STRING([--enable-iodebug],[Enable IO Debugging hooks]),
[iodebug=$enableval], [iodebug=no])

if test "$iodebug" = yes; then
	AC_DEFINE(USE_IODEBUG_HOOKS, 1, [Define this to enable IO Debug hooks.])
	AC_MSG_RESULT(yes)
else
	AC_MSG_RESULT(no)
fi


AC_MSG_CHECKING(if you want to do a profile build)
AC_ARG_ENABLE(profile,
AC_HELP_STRING([--enable-profile],[Enable profiling]),
[profile=$enableval], [profile=no])

if test "$profile" = yes; then
	if test "$ac_cv_c_compiler_gnu" = yes; then
		CFLAGS="$CFLAGS -pg -static"
		AC_MSG_RESULT([yes, adding -pg -static])
	else
		AC_MSG_RESULT([no, profile builds only work with gcc])
	fi
else
	AC_MSG_RESULT(no)
fi

AC_ARG_ENABLE(warnings,
AC_HELP_STRING([--enable-warnings],[Enable all sorts of warnings for debugging.]),
[CFLAGS="$CFLAGS -Wall -Werror -Wcast-qual -Wmissing-declarations -Wmissing-prototypes -Wnested-externs -Wshadow -Wwrite-strings -W -Wno-unused"],[])

dnl Server Tweaks
dnl =============

AC_ARG_ENABLE(services,
AC_HELP_STRING([--enable-services],[Enable ratbox-services compatibility code.]),
[ratbox_services=$enableval], [ratbox_services=no])

M_SERVICES=""

if test "x$ratbox_services" = "xyes"; then
	AC_DEFINE([ENABLE_SERVICES], 1, [ratbox-services compatibility code])
	M_SERVICES="m_services.la"
fi

AM_CONDITIONAL([ENABLE_SERVICES], [test x"$M_SERVICES" != "x"])

AC_ARG_ENABLE(small-net,
AC_HELP_STRING([--enable-small-net],[Enable small network support.]),
[small_net=$enableval], [small_net=no])

if test "$small_net" = yes; then
dnl	AC_DEFINE([HASHSIZE], 4096, [Max number of buckets in hash tables.])
	AC_DEFINE([NICKNAMEHISTORYLENGTH], 1500, [Size of the WHOWAS array.])
	AC_DEFINE([CHANNEL_HEAP_SIZE], 256, [Size of the channel heap.])
	AC_DEFINE([BAN_HEAP_SIZE], 128, [Size of the ban heap.])
	AC_DEFINE([CLIENT_HEAP_SIZE], 256, [Size of the client heap.])
	AC_DEFINE([LCLIENT_HEAP_SIZE], 128, [Size of the local client heap.])
	AC_DEFINE([USER_HEAP_SIZE], 128, [Size of the user heap.])
	AC_DEFINE([DNODE_HEAP_SIZE], 512, [Size of the dlink_node heap.])
	AC_DEFINE([TOPIC_HEAP_SIZE], 256, [Size of the topic heap.])
	AC_DEFINE([LINEBUF_HEAP_SIZE], 128, [Size of the linebuf heap.])
	AC_DEFINE([MEMBER_HEAP_SIZE], 256, [Sizeof member heap.])
	AC_DEFINE([ND_HEAP_SIZE], 128, [Size of the nick delay heap.])
	AC_DEFINE([CONFITEM_HEAP_SIZE], 128, [Size of the confitem heap.])
	AC_DEFINE([MONITOR_HEAP_SIZE], 128, [Size of the monitor heap.])
	AC_DEFINE([AUTH_HEAP_SIZE], 128, [Size of the auth heap.])
	AC_DEFINE([FD_HASH_SIZE], 1000, [Size of fd hash.])
	AC_DEFINE([FD_HEAP_SIZE], 128, [Size of fd heap.])
else
dnl These settings are for a large network like efnet..they will use lots of memory
dnl so enable small net unless you really need this much support
        AC_DEFINE([NICKNAMEHISTORYLENGTH], 15000, [Size of the WHOWAS array.])
        AC_DEFINE([CHANNEL_HEAP_SIZE], 8192, [Size of the channel heap.])
        AC_DEFINE([BAN_HEAP_SIZE], 4096, [Size of the ban heap.])
        AC_DEFINE([CLIENT_HEAP_SIZE], 8192, [Size of the client heap.])
        AC_DEFINE([LCLIENT_HEAP_SIZE], 1024, [Size of the local client heap.])
        AC_DEFINE([USER_HEAP_SIZE], 8192, [Size of the user heap.])
        AC_DEFINE([DNODE_HEAP_SIZE], 8192, [Size of the dlink_node heap.])
        AC_DEFINE([TOPIC_HEAP_SIZE], 4096, [Size of the topic heap.])
        AC_DEFINE([LINEBUF_HEAP_SIZE], 2048, [Size of the linebuf heap.])
        AC_DEFINE([MEMBER_HEAP_SIZE], 32768, [Sizeof member heap.])
        AC_DEFINE([ND_HEAP_SIZE], 512, [Size of the nick delay heap.])
        AC_DEFINE([CONFITEM_HEAP_SIZE], 1024, [Size of the confitem heap.])
	AC_DEFINE([MONITOR_HEAP_SIZE], 1024, [Size of the monitor heap.])
	AC_DEFINE([AUTH_HEAP_SIZE], 256, [Size of the auth heap.])
	AC_DEFINE([FD_HASH_SIZE], 12000, [Size of fd hash.])
	AC_DEFINE([FD_HEAP_SIZE], 1024, [Size of fd heap.])
fi

AC_ARG_WITH(nicklen,
AC_HELP_STRING([--with-nicklen=LENGTH],[Set the nick length to LENGTH (default 9, max 50)]),
[
  if test $withval -ge 50; then
	NICKLEN=50
	AC_MSG_WARN([NICKLEN has a hard limit of 50. Setting NICKLEN=50])
  else
	NICKLEN="$withval"
  fi
], [NICKLEN=9])

AC_ARG_WITH(topiclen,           
AC_HELP_STRING([--with-topiclen=NUMBER],[Set the max topic length to NUMBER (default 160, max 390)]),
[
 if test $withval -ge 390; then
	TOPICLEN=390
	AC_MSG_WARN([TOPICLEN has a hard limit of 390. Setting TOPICLEN=390])
 else
 	TOPICLEN=$withval
 fi
], [TOPICLEN=160])

AC_DEFINE_UNQUOTED(TOPICLEN, ${TOPICLEN}, [Maximum topic length (<=390)])
AC_DEFINE_UNQUOTED(NICKLEN, (${NICKLEN}+1), [Nickname length])


#case $PLATFORM in
#	*hpux* )
#	AC_DEFINE([MODULE_SUFFIX], ".sl", [Use .sl for hpux])
#	;;
#	*aix* )
#	AC_DEFINE([MODULE_SUFFIX], ".a", [Use .a for AIX])
#	;;
#	* )
#	AC_DEFINE([MODULE_SUFFIX], ".so", [Assume .so for anything else])
#	;;
#esac
#fi
    

#AC_SUBST(MODULE_SUFFIX)
#AC_SUBST(MODULES_LIBS)
#AC_SUBST(MOD_TARGET)

AC_SUBST(SSL_SRCS_ENABLE)
AC_SUBST(SSL_INCLUDES)
AC_SUBST(SSL_LIBS)

AC_SUBST(LDFLAGS)
AC_SUBST(PICFLAGS)
AC_SUBST(CFLAGS)
AC_SUBST(SEDOBJ)

if test "$prefix" = "NONE"; then 
	AC_DEFINE_UNQUOTED(IRCD_PREFIX, "$ac_default_prefix", [Prefix where the ircd is installed.])

else

dnl Don't get bitten by Cygwin's stupidity if the user specified
dnl a custom prefix with a trailing slash

	prefix=`echo $prefix | sed 's/\/$//'`
	AC_DEFINE_UNQUOTED(IRCD_PREFIX, "$prefix", [Prefix where the ircd is installed.])
	
fi

AC_SUBST(IRCD_PREFIX)

AC_CONFIG_FILES(		\
	Makefile		\
	libircd/Makefile	\
	adns/Makefile		\
	servlink/Makefile	\
	contrib/Makefile	\
	tools/Makefile		\
	doc/Makefile		\
	help/Makefile		
)

AC_OUTPUT

dnl Make it look sexay!

echo
echo "Compiling $PACKAGE_NAME $PACKAGE_VERSION"
echo

echo "Installing into: $prefix"

echo "Ziplinks ....................... $zlib"

echo "OpenSSL ........................ $cf_enable_openssl"

tmpresult=shared

if test "x$enable_static" = "xyes"; then
	tmpresult=static
fi

echo "Modules ........................ $tmpresult"
echo "IPv6 support ................... $have_v6"
echo "Net I/O implementation ......... $SELECT_TYPE"
echo "IO Debugging hooks ............. $iodebug"
echo "Assert debugging ............... $assert"
echo "Small network .................. $small_net"
echo "ratbox-services compatibility .. $ratbox_services"
echo
echo "Configured limits:"
echo "NICKLEN ................. $NICKLEN"
echo "TOPICLEN ................ $TOPICLEN"
echo
