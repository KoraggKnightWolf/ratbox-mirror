#
# $Id$
#
bin_PROGRAMS = bandb
AM_CFLAGS=$(WARNFLAGS)

INCLUDES = -I../include -I../libratbox/include @SQLITE_INCLUDES@


bandb_SOURCES = bandb.c rsdb_sqlite3.c rsdb_snprintf.c

bandb_LDADD = ../libratbox/src/libratbox.la @SQLITE_LD@


