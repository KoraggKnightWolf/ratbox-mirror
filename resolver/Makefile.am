#
# $Id: Makefile.am 24471 2007-11-07 02:25:56Z androsyn $
#
libexec_PROGRAMS = resolver

INCLUDES = -I../include -I. -I../libratbox/include 
AM_CFLAGS=$(WARNFLAGS)

resolver_SOURCES = resolver.c res.c reslib.c

resolver_LDADD = ../libratbox/src/libratbox.la



