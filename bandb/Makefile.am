#
# $Id: Makefile.am 21086 2005-11-05 03:11:25Z androsyn $
#
bin_PROGRAMS = bandb

INCLUDES = -I../include -I../libircd @SQLITE_INCLUDES@


bandb_SOURCES = bandb.c rsdb_sqlite3.c rsdb_snprintf.c

bandb_LDADD = ../libircd/libircd.la @SQLITE_LD@


