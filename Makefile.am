# $Id$
# 
# okay so i've flattened out the build stuff here.
# this is really for the best since so many things depend
# on each other now..

AUTOMAKE_OPTIONS = foreign subdir-objects 

prefix		= @prefix@
libcoredir 	= @libdir@
modulesdir	= @moduledir@/autoload
coredir		= @moduledir@
servicesdir	= @moduledir@/autoload

SUBDIRS = libratbox libltdl modules tools doc help @SQLITE_SUBDIR@ bandb ssld resolver ident

AM_YFLAGS = -d
AM_CFLAGS = $(WARNFLAGS)
INCLUDES = -Ilibratbox/include -Ilibltdl

BUILT_SOURCES = src/version.c

src/version.c: src/version.c.SH CREDITS
	$(SHELL) -c "(cd src; $(SHELL) ./version.c.SH)"
	$(CP) src/version.c src/version.c.last

src/version.lo: src/version.c include/patchlevel.h include/serno.h
	$(LIBTOOL) --mode=compile $(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
        

if MINGW
EXTRA_FLAGS = -no-undefined -Wl,--enable-runtime-pseudo-reloc -export-symbols-regex '*'
endif

if !STATIC_MODULES
ircd_LDADD = libratbox/src/libratbox.la libcore.la $(LIBLTDL)
else
ircd_LDADD = libratbox/src/libratbox.la modules/libmodules.la libcore.la modules/static_modules.o $(LIBLTDL) $(DLOPEN) 
endif

ircd_LDFLAGS = $(EXTRA_FLAGS) -dlopen self

ircd_SOURCES = src/main.c

libcore_la_SOURCES =                     \
  src/dns.c                        \
  src/bandbi.c			\
  src/cache.c                       \
  src/channel.c                     \
  src/class.c                       \
  src/client.c                      \
  src/getopt.c                      \
  src/hash.c                        \
  src/hook.c                        \
  src/hostmask.c                    \
  src/ircd.c                        \
  src/ircd_signal.c                 \
  src/listener.c                    \
  src/match.c                       \
  src/modules.c                     \
  src/monitor.c                     \
  src/newconf.c                     \
  src/numeric.c                     \
  src/operhash.c		\
  src/packet.c                      \
  src/parse.c                       \
  src/reject.c                      \
  src/restart.c                     \
  src/s_auth.c                      \
  src/s_conf.c                      \
  src/s_newconf.c                   \
  src/s_log.c                       \
  src/s_serv.c                      \
  src/s_user.c                      \
  src/scache.c                      \
  src/send.c                        \
  src/sslproc.c			    \
  src/whowas.c			    \
  src/version.c			\
  src/ircd_parser.y			\
  src/ircd_lexer.l			

libcore_LTLIBRARIES = libcore.la
libcore_la_LDFLAGS = $(EXTRA_FLAGS) -avoid-version 

if MINGW
libcore_la_DEPENDENCIES = $(LIBLTDL) libratbox/src/libratbox.la
libcore_la_LIBADD = libratbox/src/libratbox.la $(LIBLTDL) @SSL_LIBS@
else
libcore_la_DEPENDENCIES = libratbox/src/libratbox.la
libcore_la_LIBADD = libratbox/src/libratbox.la $(LIBLTDL) @SSL_LIBS@
endif


# now the modules..



bin_PROGRAMS = ircd

src: $(libcore_LTLIBRARIES) ircd

#modules: $(modules_LTLIBRARIES) $(services_LTLIBRARIES)

#modules_core: $(core_LTLIBRARIES)

install-exec-hook: install-libcoreLTLIBRARIES
	$(RM) src/version.c

move-old-modules:
# move the old module directory out of the way, stale modules cause massive
# amounts of problems.
	-@if test -d $(DESTDIR)$(moduledir)-old; then \
		rm -rf $(DESTDIR)$(moduledir)-old; \
	fi
	-@if test -d $(DESTDIR)$(moduledir); then \
		echo "ircd: backing up modules"; \
		mv $(DESTDIR)$(moduledir) $(DESTDIR)$(moduledir)-old; \
		mkdir $(DESTDIR)$(moduledir); \
	fi


