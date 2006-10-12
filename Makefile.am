# $Id$
# 
# okay so i've flattened out the build stuff here.
# this is really for the best since so many things depend
# on each other now..

AUTOMAKE_OPTIONS = foreign subdir-objects 

prefix		= @prefix@
libratboxdir 	= @libdir@
modulesdir	= @moduledir@/autoload
coredir		= @moduledir@
servicesdir	= @moduledir@

SUBDIRS = libircd libltdl adns tools doc help @SQLITE_SUBDIR@ bandb @SERVLINK@ 

AM_YFLAGS = -d

INCLUDES = -Ilibircd/include -Ilibltdl -Iadns
src/version.c: src/version.c.SH
	$(SHELL) -c "(cd src; $(SHELL) ./version.c.SH)"

src/version.lo: src/version.c include/patchlevel.h include/serno.h
	$(LIBTOOL) --mode=compile $(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
        

if MINGW
EXTRA_FLAGS = -no-undefined -Wl,--enable-runtime-pseudo-reloc -export-symbols-regex '*'
endif

DLOPEN=$(shell for x in $(modules_LTLIBRARIES) $(core_LTLIBRARIES) $(services_LTLIBRARIES) ;do echo -dlpreopen $$x ;done)
if !STATIC_MODULES
ircd_LDADD = libircd/src/libircd.la libratbox.la $(LIBLTDL)
else
ircd_LDADD = libircd/src/libircd.la libratbox.la $(LIBLTDL) $(DLOPEN) 
endif

ircd_LDFLAGS = $(EXTRA_FLAGS) -dlopen self

resolver_LDADD = adns/libadns.a libircd/src/libircd.la
ident_LDADD = libircd/src/libircd.la

ircd_SOURCES = src/main.c
resolver_SOURCES = src/resolver.c
ident_SOURCES = src/ident.c


libratbox_la_SOURCES =                     \
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
  src/patricia.c                    \
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
  src/whowas.c			    \
  src/version.c			\
  src/ircd_parser.y			\
  src/ircd_lexer.l			

libratbox_LTLIBRARIES = libratbox.la
libratbox_la_LDFLAGS = $(EXTRA_FLAGS) -avoid-version 

if MINGW
libratbox_la_DEPENDENCIES = $(LIBLTDL) libircd/src/libircd.la
libratbox_la_LIBADD = libircd/src/libircd.la $(LIBLTDL) @SSL_LIBS@ @CRYPT_LIB@
else
libratbox_la_DEPENDENCIES = libircd/src/libircd.la
libratbox_la_LIBADD = libircd/src/libircd.la @SSL_LIBS@ @CRYPT_LIB@
endif


# now the modules..

MODULE_FLAGS = $(EXTRA_FLAGS) -avoid-version 

if !STATIC_MODULES

if MINGW
MODULE_LIBS = libircd/src/libircd.la libratbox.la @SSL_LIBS@ @CRYPT_LIB@ 
else
MODULE_LIBS = @SSL_LIBS@ @CRYPT_LIB@
endif # MINGW

endif # !STATIC_MODULES

modules_m_accept_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_admin_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_away_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_capab_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_cap_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_close_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_cmessage_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_connect_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_dline_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_encap_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_etrace_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_gline_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_help_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_info_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_invite_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_ison_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_kline_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_knock_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_links_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_list_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_locops_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_lusers_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_map_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_monitor_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_motd_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_names_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_oper_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_operspy_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_pass_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_ping_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_pong_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_post_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_rehash_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_restart_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_resv_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_set_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_stats_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_svinfo_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_tb_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_testline_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_testmask_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_time_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_topic_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_trace_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_unreject_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_user_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_userhost_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_version_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_wallops_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_who_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_whois_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_whowas_la_LDFLAGS =  -module $(MODULE_FLAGS)  
modules_m_xline_la_LDFLAGS =  -module $(MODULE_FLAGS)  

modules_m_accept_la_SOURCES =	modules/m_accept.c
modules_m_admin_la_SOURCES =	modules/m_admin.c
modules_m_away_la_SOURCES =	modules/m_away.c
modules_m_capab_la_SOURCES =	modules/m_capab.c
modules_m_cap_la_SOURCES =	modules/m_cap.c
modules_m_close_la_SOURCES =	modules/m_close.c
modules_m_cmessage_la_SOURCES =	modules/m_cmessage.c
modules_m_connect_la_SOURCES =	modules/m_connect.c
modules_m_dline_la_SOURCES =	modules/m_dline.c
modules_m_encap_la_SOURCES =	modules/m_encap.c
modules_m_etrace_la_SOURCES =	modules/m_etrace.c
modules_m_gline_la_SOURCES =	modules/m_gline.c
modules_m_help_la_SOURCES =	modules/m_help.c
modules_m_info_la_SOURCES =	modules/m_info.c
modules_m_invite_la_SOURCES =	modules/m_invite.c
modules_m_ison_la_SOURCES =	modules/m_ison.c
modules_m_kline_la_SOURCES =	modules/m_kline.c
modules_m_knock_la_SOURCES =	modules/m_knock.c
modules_m_links_la_SOURCES =	modules/m_links.c
modules_m_list_la_SOURCES =	modules/m_list.c
modules_m_locops_la_SOURCES =	modules/m_locops.c
modules_m_lusers_la_SOURCES =	modules/m_lusers.c
modules_m_map_la_SOURCES =	modules/m_map.c
modules_m_monitor_la_SOURCES =	modules/m_monitor.c
modules_m_motd_la_SOURCES =	modules/m_motd.c
modules_m_names_la_SOURCES =	modules/m_names.c
modules_m_oper_la_SOURCES =	modules/m_oper.c
modules_m_operspy_la_SOURCES =	modules/m_operspy.c
modules_m_pass_la_SOURCES =	modules/m_pass.c
modules_m_ping_la_SOURCES =	modules/m_ping.c
modules_m_pong_la_SOURCES =	modules/m_pong.c
modules_m_post_la_SOURCES =	modules/m_post.c
modules_m_rehash_la_SOURCES =	modules/m_rehash.c
modules_m_restart_la_SOURCES =	modules/m_restart.c
modules_m_resv_la_SOURCES =	modules/m_resv.c
modules_m_set_la_SOURCES =	modules/m_set.c
modules_m_stats_la_SOURCES =	modules/m_stats.c
modules_m_svinfo_la_SOURCES =	modules/m_svinfo.c
modules_m_tb_la_SOURCES =	modules/m_tb.c
modules_m_testline_la_SOURCES =	modules/m_testline.c
modules_m_testmask_la_SOURCES =	modules/m_testmask.c
modules_m_time_la_SOURCES =	modules/m_time.c
modules_m_topic_la_SOURCES =	modules/m_topic.c
modules_m_trace_la_SOURCES =	modules/m_trace.c
modules_m_unreject_la_SOURCES =	modules/m_unreject.c
modules_m_user_la_SOURCES =	modules/m_user.c
modules_m_userhost_la_SOURCES =	modules/m_userhost.c
modules_m_version_la_SOURCES =	modules/m_version.c
modules_m_wallops_la_SOURCES =	modules/m_wallops.c
modules_m_who_la_SOURCES =	modules/m_who.c
modules_m_whois_la_SOURCES =	modules/m_whois.c
modules_m_whowas_la_SOURCES =	modules/m_whowas.c
modules_m_xline_la_SOURCES =	modules/m_xline.c

modules_LTLIBRARIES =   \
modules/m_accept.la	\
modules/m_admin.la	\
modules/m_away.la	\
modules/m_capab.la	\
modules/m_cap.la	\
modules/m_close.la	\
modules/m_cmessage.la	\
modules/m_connect.la	\
modules/m_dline.la	\
modules/m_encap.la	\
modules/m_etrace.la	\
modules/m_gline.la	\
modules/m_help.la	\
modules/m_info.la	\
modules/m_invite.la	\
modules/m_ison.la	\
modules/m_kline.la	\
modules/m_knock.la	\
modules/m_links.la	\
modules/m_list.la	\
modules/m_locops.la	\
modules/m_lusers.la	\
modules/m_map.la	\
modules/m_monitor.la	\
modules/m_motd.la	\
modules/m_names.la	\
modules/m_oper.la	\
modules/m_operspy.la	\
modules/m_pass.la	\
modules/m_ping.la	\
modules/m_pong.la	\
modules/m_post.la	\
modules/m_rehash.la	\
modules/m_restart.la	\
modules/m_resv.la	\
modules/m_set.la	\
modules/m_stats.la	\
modules/m_svinfo.la	\
modules/m_tb.la		\
modules/m_testline.la	\
modules/m_testmask.la	\
modules/m_time.la	\
modules/m_topic.la	\
modules/m_trace.la	\
modules/m_unreject.la	\
modules/m_user.la	\
modules/m_userhost.la	\
modules/m_version.la	\
modules/m_wallops.la	\
modules/m_who.la	\
modules/m_whois.la	\
modules/m_whowas.la	\
modules/m_xline.la	


modules_m_accept_la_LIBADD = $(MODULE_LIBS)     
modules_m_admin_la_LIBADD = $(MODULE_LIBS)     
modules_m_away_la_LIBADD = $(MODULE_LIBS)     
modules_m_capab_la_LIBADD = $(MODULE_LIBS)     
modules_m_cap_la_LIBADD = $(MODULE_LIBS)     
modules_m_close_la_LIBADD = $(MODULE_LIBS)     
modules_m_cmessage_la_LIBADD = $(MODULE_LIBS)     
modules_m_connect_la_LIBADD = $(MODULE_LIBS)     
modules_m_dline_la_LIBADD = $(MODULE_LIBS)     
modules_m_encap_la_LIBADD = $(MODULE_LIBS)     
modules_m_etrace_la_LIBADD = $(MODULE_LIBS)     
modules_m_gline_la_LIBADD = $(MODULE_LIBS)     
modules_m_help_la_LIBADD = $(MODULE_LIBS)     
modules_m_info_la_LIBADD = $(MODULE_LIBS)     
modules_m_invite_la_LIBADD = $(MODULE_LIBS)     
modules_m_ison_la_LIBADD = $(MODULE_LIBS)     
modules_m_kline_la_LIBADD = $(MODULE_LIBS)     
modules_m_knock_la_LIBADD = $(MODULE_LIBS)     
modules_m_links_la_LIBADD = $(MODULE_LIBS)     
modules_m_list_la_LIBADD = $(MODULE_LIBS)     
modules_m_locops_la_LIBADD = $(MODULE_LIBS)     
modules_m_lusers_la_LIBADD = $(MODULE_LIBS)     
modules_m_map_la_LIBADD = $(MODULE_LIBS)     
modules_m_monitor_la_LIBADD = $(MODULE_LIBS)     
modules_m_motd_la_LIBADD = $(MODULE_LIBS)     
modules_m_names_la_LIBADD = $(MODULE_LIBS)     
modules_m_oper_la_LIBADD = $(MODULE_LIBS)     
modules_m_operspy_la_LIBADD = $(MODULE_LIBS)     
modules_m_pass_la_LIBADD = $(MODULE_LIBS)     
modules_m_ping_la_LIBADD = $(MODULE_LIBS)     
modules_m_pong_la_LIBADD = $(MODULE_LIBS)     
modules_m_post_la_LIBADD = $(MODULE_LIBS)     
modules_m_rehash_la_LIBADD = $(MODULE_LIBS)     
modules_m_restart_la_LIBADD = $(MODULE_LIBS)     
modules_m_resv_la_LIBADD = $(MODULE_LIBS)     
modules_m_set_la_LIBADD = $(MODULE_LIBS)     
modules_m_stats_la_LIBADD = $(MODULE_LIBS)     
modules_m_svinfo_la_LIBADD = $(MODULE_LIBS)     
modules_m_tb_la_LIBADD = $(MODULE_LIBS)     
modules_m_testline_la_LIBADD = $(MODULE_LIBS)     
modules_m_testmask_la_LIBADD = $(MODULE_LIBS)     
modules_m_time_la_LIBADD = $(MODULE_LIBS)     
modules_m_topic_la_LIBADD = $(MODULE_LIBS)     
modules_m_trace_la_LIBADD = $(MODULE_LIBS)     
modules_m_unreject_la_LIBADD = $(MODULE_LIBS)     
modules_m_user_la_LIBADD = $(MODULE_LIBS)     
modules_m_userhost_la_LIBADD = $(MODULE_LIBS)     
modules_m_version_la_LIBADD = $(MODULE_LIBS)     
modules_m_wallops_la_LIBADD = $(MODULE_LIBS)     
modules_m_who_la_LIBADD = $(MODULE_LIBS)     
modules_m_whois_la_LIBADD = $(MODULE_LIBS)     
modules_m_whowas_la_LIBADD = $(MODULE_LIBS)     
modules_m_xline_la_LIBADD = $(MODULE_LIBS)     


modules_core_m_die_la_LDFLAGS = -module $(MODULE_FLAGS) 
modules_core_m_error_la_LDFLAGS = -module $(MODULE_FLAGS) 
modules_core_m_join_la_LDFLAGS = -module $(MODULE_FLAGS) 
modules_core_m_kick_la_LDFLAGS = -module $(MODULE_FLAGS) 
modules_core_m_kill_la_LDFLAGS = -module $(MODULE_FLAGS) 
modules_core_m_message_la_LDFLAGS = -module $(MODULE_FLAGS) 
modules_core_m_mode_la_LDFLAGS = -module $(MODULE_FLAGS) 
modules_core_m_nick_la_LDFLAGS = -module $(MODULE_FLAGS) 
modules_core_m_part_la_LDFLAGS = -module $(MODULE_FLAGS) 
modules_core_m_quit_la_LDFLAGS = -module $(MODULE_FLAGS) 
modules_core_m_server_la_LDFLAGS = -module $(MODULE_FLAGS) 
modules_core_m_squit_la_LDFLAGS = -module $(MODULE_FLAGS) 

modules_core_m_die_la_SOURCES =	modules/core/m_die.c
modules_core_m_error_la_SOURCES =	modules/core/m_error.c
modules_core_m_join_la_SOURCES =	modules/core/m_join.c
modules_core_m_kick_la_SOURCES =	modules/core/m_kick.c
modules_core_m_kill_la_SOURCES =	modules/core/m_kill.c
modules_core_m_message_la_SOURCES =	modules/core/m_message.c
modules_core_m_mode_la_SOURCES =	modules/core/m_mode.c
modules_core_m_nick_la_SOURCES =	modules/core/m_nick.c
modules_core_m_part_la_SOURCES =	modules/core/m_part.c
modules_core_m_quit_la_SOURCES =	modules/core/m_quit.c
modules_core_m_server_la_SOURCES =	modules/core/m_server.c
modules_core_m_squit_la_SOURCES =	modules/core/m_squit.c



core_LTLIBRARIES = \
modules/core/m_die.la	\
modules/core/m_error.la	\
modules/core/m_join.la	\
modules/core/m_kick.la	\
modules/core/m_kill.la	\
modules/core/m_message.la	\
modules/core/m_mode.la	\
modules/core/m_nick.la	\
modules/core/m_part.la	\
modules/core/m_quit.la	\
modules/core/m_server.la \
modules/core/m_squit.la	


modules_core_m_die_la_LIBADD = $(MODULE_LIBS)   
modules_core_m_error_la_LIBADD = $(MODULE_LIBS)   
modules_core_m_join_la_LIBADD = $(MODULE_LIBS)   
modules_core_m_kick_la_LIBADD = $(MODULE_LIBS)   
modules_core_m_kill_la_LIBADD = $(MODULE_LIBS)   
modules_core_m_message_la_LIBADD = $(MODULE_LIBS)   
modules_core_m_mode_la_LIBADD = $(MODULE_LIBS)   
modules_core_m_nick_la_LIBADD = $(MODULE_LIBS)   
modules_core_m_part_la_LIBADD = $(MODULE_LIBS) 
modules_core_m_quit_la_LIBADD = $(MODULE_LIBS)  
modules_core_m_server_la_LIBADD = $(MODULE_LIBS)
modules_core_m_squit_la_LIBADD = $(MODULE_LIBS) 

if ENABLE_SERVICES
modules_m_services_la_LDFLAGS = -module $(MODULE_FLAGS)  
modules_m_services_la_SOURCES =	modules/m_services.c
modules_m_services_la_LIBADD = $(MODULE_LIBS)     
services_LTLIBRARIES = modules/m_services.la
endif



bin_PROGRAMS = ircd resolver ident

src: $(libratbox_LTLIBRARIES) ircd

modules: $(modules_LTLIBRARIES) $(services_LTLIBRARIES)

modules_core: $(core_LTLIBRARIES)

.o.so: 
	$(LIBTOOL) --mode=link $(CC) $(CFLAGS) $(MODULE_FLAGS) $(MODULE_LIBS) -avoid-version -shared $< -o $@

install-exec-hook: install-libratboxLTLIBRARIES move-old-modules

move-old-modules:
# move the old module directory out of the way, stale modules cause massive
# amounts of problems.
	-@if test -d $(DESTDIR)$(moduledir)-old; then \
		rm -rf $(DESTDIR)$(moduledir)-old; \
	fi
	-@if test -d $(DESTDIR)$(moduledir); then \
		echo "ircd: backing up modules"; \
		mv $(DESTDIR)$(moduledir) $(DESTDIR)$(moduledir)-old; \
	fi


