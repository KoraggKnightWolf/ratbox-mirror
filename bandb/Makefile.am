#
# $Id$
#
AUTOMAKE_OPTIONS = foreign subdir-objects

SUBDIR = @SQLITE_SUBDIR@

bin_PROGRAMS = bandb bantool
AM_CFLAGS=$(WARNFLAGS)

INCLUDES = -I../include -I../libratbox/include @SQLITE_INCLUDES@


bandb_SOURCES = bandb.c rsdb_sqlite3.c rsdb_snprintf.c

bandb_LDADD = ../libratbox/src/libratbox.la @SQLITE_LD@

bantool_SOURCES = bantool.c rsdb_sqlite3.c rsdb_snprintf.c
bantool_LDADD = ../libratbox/src/libratbox.la @SQLITE_LD@

