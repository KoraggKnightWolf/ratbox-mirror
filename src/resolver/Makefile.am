# $Id$

AUTOMAKE_OPTIONS = foreign

bin_PROGRAMS = resolver

resolver_CFLAGS = -I../../adns -I../../include -I../../libircd
# we recycle a lot of code from ircd itself..
# perhaps we should move this stuff into a lib someday
# oh well...
resolver_SOURCES = resolver.c 
resolver_LDADD = -L../../adns -ladns -L../../libircd -lircd
