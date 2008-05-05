#
# $Id$
#
AUTOMAKE_OPTIONS = foreign subdir-objects

if BUILD_SQLITE
SUBDIRS = sqlite3
endif

libexec_PROGRAMS = bandb 
bin_PROGRAMS = bantool
AM_CFLAGS=$(WARNFLAGS)

INCLUDES = -I../include -I../libratbox/include @SQLITE_INCLUDES@


bandb_SOURCES = bandb.c rsdb_sqlite3.c rsdb_snprintf.c

bandb_LDADD = ../libratbox/src/libratbox.la @SQLITE_LD@

bantool_SOURCES = bantool.c rsdb_sqlite3.c rsdb_snprintf.c
bantool_LDADD = ../libratbox/src/libratbox.la @SQLITE_LD@

