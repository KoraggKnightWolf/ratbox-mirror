# $Id: Makefile.am 20611 2005-08-02 03:22:22Z androsyn $

AUTOMAKE_OPTIONS = foreign


prefix          = @prefix@
coredir       = @moduledir@

INSTALL         = @INSTALL@
INSTALL_PROGRAM = @INSTALL_PROGRAM@

INCLUDES = -I../../include -I../../libircd $(INCLTDL)



m_die_la_LDFLAGS = -module -avoid-version ../../src/libratbox.la
m_error_la_LDFLAGS = -module -avoid-version ../../src/libratbox.la
m_join_la_LDFLAGS = -module -avoid-version ../../src/libratbox.la
m_kick_la_LDFLAGS = -module -avoid-version ../../src/libratbox.la
m_kill_la_LDFLAGS = -module -avoid-version ../../src/libratbox.la
m_message_la_LDFLAGS = -module -avoid-version ../../src/libratbox.la
m_mode_la_LDFLAGS = -module -avoid-version ../../src/libratbox.la
m_nick_la_LDFLAGS = -module -avoid-version ../../src/libratbox.la
m_part_la_LDFLAGS = -module -avoid-version ../../src/libratbox.la
m_quit_la_LDFLAGS = -module -avoid-version ../../src/libratbox.la
m_server_la_LDFLAGS = -module -avoid-version ../../src/libratbox.la
m_squit_la_LDFLAGS = -module -avoid-version ../../src/libratbox.la

m_die_la_SOURCES =	m_die.c
m_error_la_SOURCES =	m_error.c
m_join_la_SOURCES =	m_join.c
m_kick_la_SOURCES =	m_kick.c
m_kill_la_SOURCES =	m_kill.c
m_message_la_SOURCES =	m_message.c
m_mode_la_SOURCES =	m_mode.c
m_nick_la_SOURCES =	m_nick.c
m_part_la_SOURCES =	m_part.c
m_quit_la_SOURCES =	m_quit.c
m_server_la_SOURCES =	m_server.c
m_squit_la_SOURCES =	m_squit.c



core_LTLIBRARIES = \
m_die.la	\
m_error.la	\
m_join.la	\
m_kick.la	\
m_kill.la	\
m_message.la	\
m_mode.la	\
m_nick.la	\
m_part.la	\
m_quit.la	\
m_server.la	\
m_squit.la	


