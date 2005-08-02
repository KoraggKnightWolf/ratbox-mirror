# $Id$

prefix          = @prefix@
moduledir       = @moduledir@
automoduledir   = @moduledir@/autoload


INSTALL         = @INSTALL@
INSTALL_PROGRAM = @INSTALL_PROGRAM@

INCLUDES = -I../include -I../libircd $(INCLTDL)

CORE_SRCS = \
  core/m_die.c \
  core/m_error.c \
  core/m_join.c \
  core/m_kick.c \
  core/m_kill.c \
  core/m_message.c \
  core/m_mode.c \
  core/m_nick.c \
  core/m_part.c \
  core/m_quit.c \
  core/m_server.c \
  core/m_squit.c

TSRCS =		\
  m_accept.c \
  m_admin.c \
  m_away.c \
  m_cap.c \
  m_capab.c \
  m_challenge.c \
  m_close.c \
  m_cmessage.c \
  m_connect.c \
  m_dline.c \
  m_encap.c \
  m_etrace.c \
  m_gline.c \
  m_help.c \
  m_info.c \
  m_invite.c \
  m_ison.c \
  m_kline.c \
  m_knock.c \
  m_links.c \
  m_list.c \
  m_locops.c \
  m_lusers.c \
  m_map.c \
  m_monitor.c \
  m_motd.c \
  m_names.c \
  m_oper.c \
  m_operspy.c \
  m_pass.c \
  m_ping.c \
  m_pong.c \
  m_post.c \
  m_rehash.c \
  m_restart.c \
  m_resv.c \
  m_set.c \
  m_stats.c \
  m_svinfo.c \
  m_tb.c \
  m_testline.c \
  m_testmask.c \
  m_time.c \
  m_topic.c \
  m_trace.c \
  m_user.c \
  m_userhost.c \
  m_users.c \
  m_version.c \
  m_wallops.c \
  m_who.c \
  m_whois.c \
  m_whowas.c \
  m_unreject.c \
  m_xline.c

SRCS = $(TSRCS) @M_SERVICES@

ALL_SRCS = $(SRCS)

S_OBJS = $(ALL_SRCS:.c=.la)
S_CORE_OBJS = $(CORE_SRCS:.c=.la)
ST_OBJS = $(ALL_SRCS:.c=.lo) $(CORE_SRCS:.c=.lo)

SUFFIXES = .c .so .lo .la

if STATIC_MODULES
.c.lo:
	$(LIBTOOL) --mode=compile $(CC) -static -DMODNAME=`basename $< .lo`_mheader -DHAVE_CONFIG_H $(INCLUDES) $(AM_CPPFLAGS) $(CFLAGS) -c $< -o $@
else
.c.lo:
	$(LIBTOOL) --mode=compile $(CC) -prefer-pic -DHAVE_CONFIG_H $(INCLUDES) $(AM_CPPFLAGS) $(CFLAGS) -c $< -o $@
endif

.lo.la:
	$(LIBTOOL) --mode=link  $(CC) -rpath $(moduledir) -module -avoid-version $(CFLAGS) -o $@ $< $(LIBS) $(LDFLAGS) 

#BUILD
default: 	build
build:		all

if STATIC_MODULES

static_modules.lo: static_modules.c
	$(LIBTOOL) --mode=compile $(CC) -static -DHAVE_CONFIG_H $(INCLUDES) $(AM_CPPFLAGS) $(CFLAGS) -c $< -o $@

static_modules.c: static_modules.c.SH
	/bin/sh ./static_modules.c.SH $(ST_OBJS)

libmodules.a: $(ST_OBJS) static_modules.lo
	$(LIBTOOL) --mode=link  $(CC) --all-static $(CFLAGS) -o $@ $(ST_OBJS) static_modules.lo

all:	libmodules.a
	
else
        

all: $(S_OBJS) $(S_CORE_OBJS) 

endif 


clean-generic:
	$(LIBTOOL) --mode=clean $(RM) -f $(S_OBJS) $(S_CORE_OBJS)  $(ST_OBJS) static_modules.lo
	
	
		
install_core: $(S_CORE_OBJS)
	$(LIBTOOL) --mode=install $(INSTALL_DATA) $(S_CORE_OBJS) $(DESTDIR)$(moduledir);

install_modules: $(S_OBJS)
	$(LIBTOOL) --mode=install $(INSTALL_DATA) $(S_OBJS) $(DESTDIR)$(automoduledir);


install-mkdirs:
	@echo "ircd: setting up modular directory structure"
	-@if test ! -d $(DESTDIR)$(moduledir); then \
		mkdir $(DESTDIR)$(moduledir); \
	fi
	-@if test -d $(DESTDIR)$(moduledir)-old; then \
		rm -rf $(DESTDIR)$(moduledir)-old; \
	fi
	-@if test -d $(DESTDIR)$(moduledir); then \
		echo "ircd: backing up modules"; \
		mv $(DESTDIR)$(moduledir) $(DESTDIR)$(moduledir)-old; \
	fi

	@mkdir -p -m 755 $(DESTDIR)$(moduledir)
	@mkdir -p -m 755 $(DESTDIR)$(automoduledir)

if !STATIC_MODULES
install: install-mkdirs install_core install_modules
endif