# $Id$

AUTOMAKE_OPTIONS = foreign
AM_YFLAGS = -d
bin_PROGRAMS = ircd

INCLUDES = $(DEFAULT_INCLUDES) -I../libltdl -I../libircd -I.
version.c: 
	$(SHELL) ./version.c.SH 

version.lo: version.c
	$(LIBTOOL) --mode=compile $(CC) $(CFLAGS) $(INCLUDES) -c $<
	$(MV) version.c version.c.last
        

if MINGW
EXTRA_FLAGS = -Wl,--enable-runtime-pseudo-reloc
endif
        
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

libratbox_la_LDFLAGS = $(EXTRA_FLAGS) -avoid-version -no-undefined -export-symbols-regex '*'
libratbox_la_DEPENDENCIES = $(LIBLTDLC) ../libircd/libircd.la
libratbox_la_LIBADD = ../libircd/libircd.la $(LIBLTDL) @SSL_LIBS@ @CRYPT_LIB@
ircd_LDADD = libratbox.la ../libircd/libircd.la 
ircd_LDFLAGS = $(EXTRA_FLAGS)