# $Id$ 

bin_PROGRAMS = mkpasswd
AM_CFLAGS=$(WARNFLAGS)

INCLUDES = $(DEFAULT_INCLUDES) -I../libratbox/include -I.


mkpasswd_SOURCES = mkpasswd.c

mkpasswd_LDADD = ../libratbox/src/libratbox.la


install-exec-hook:
	$(INSTALL) genssl.sh $(DESTDIR)$(bindir)