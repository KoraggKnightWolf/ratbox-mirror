# $Id$

AUTOMAKE_OPTIONS = foreign

bin_PROGRAMS = ident

# we recycle a lot of code from ircd itself..
# perhaps we should move this stuff into a lib someday
# oh well...
ident_CFLAGS = -I../../libircd
ident_SOURCES = ident.c 		
ident_LDADD = -L../../libircd -lircd

