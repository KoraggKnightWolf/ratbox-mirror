# $Id$

AUTOMAKE_OPTIONS = foreign
AM_YFLAGS = -d
bin_PROGRAMS = ircd

INCLUDES = $(INCLTDL) -I../libircd/ 

version.c: 
	$(SHELL) ./version.c.SH 

version.lo: version.c
	$(LIBTOOL) --mode=compile $(CC) $(CFLAGS) $(INCLUDES) -c $<
	$(MV) version.c version.c.last
        
        
ircd_SOURCES = main.c
	
libratbox_la_SOURCES =                     \
  dns.c                        \
  cache.c                       \
  channel.c                     \
  class.c                       \
  client.c                      \
  getopt.c                      \
  hash.c                        \
  hook.c                        \
  hostmask.c                    \
  irc_string.c                  \
  ircd.c                        \
  ircd_signal.c                 \
  kdparse.c                     \
  listener.c                    \
  match.c                       \
  modules.c                     \
  monitor.c                     \
  newconf.c                     \
  numeric.c                     \
  packet.c                      \
  parse.c                       \
  patricia.c                    \
  reject.c                      \
  restart.c                     \
  s_auth.c                      \
  s_conf.c                      \
  s_newconf.c                   \
  s_log.c                       \
  s_serv.c                      \
  s_user.c                      \
  scache.c                      \
  send.c                        \
  whowas.c			\
  version.c			\
  ircd_parser.y			\
  ircd_lexer.l			
  
lib_LTLIBRARIES = libratbox.la

if STATIC_MODULES
libratbox_la_LIBADD = @SSL_LIBS@ @CRYPT_LIB@ -L../modules -lmodules
else
libratbox_la_LIBADD = $(LIBLTDL) @SSL_LIBS@ @CRYPT_LIB@
endif

libratbox_la_LDFLAGS = -avoid-version -no-undefined  ../libircd/libircd.la

libratbox_la_DEPENDENCIES = $(LIBLTDLC) ../libircd/libircd.la

ircd_LDADD = libratbox.la ../libircd/libircd.la
