#
# $Id$
#
bin_PROGRAMS = bandb

INCLUDES = -I../include -I../libircd/include @SQLITE_INCLUDES@


bandb_SOURCES = bandb.c rsdb_sqlite3.c rsdb_snprintf.c

bandb_LDADD = ../libircd/src/libircd.la @SQLITE_LD@


