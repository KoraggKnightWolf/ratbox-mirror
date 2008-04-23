#
# $Id: Makefile.am 24471 2007-11-07 02:25:56Z androsyn $
#
libexec_PROGRAMS = resolver

INCLUDES = -I../include -Iadns -I../libratbox/include 
AM_CFLAGS=$(WARNFLAGS)
SUBDIRS=adns

resolver_SOURCES = resolver.c

resolver_LDADD = adns/libadns.a ../libratbox/src/libratbox.la



