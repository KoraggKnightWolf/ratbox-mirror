#
# $Id: Makefile.am 24471 2007-11-07 02:25:56Z androsyn $
#
bin_PROGRAMS = resolver

INCLUDES = -I../include -Iadns -I../libratbox/include 

SUBDIRS=adns

resolver_SOURCES = resolver.c

resolver_LDADD = adns/libadns.a ../libratbox/src/libratbox.la



