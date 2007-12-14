# $Id$ 

bin_PROGRAMS = mkpasswd

INCLUDES = $(DEFAULT_INCLUDES) -I../libratbox/include -I.


mkpasswd_SOURCES = mkpasswd.c

mkpasswd_LDADD = ../libratbox/src/libratbox.la



install-exec-hook:
	$(CP) bantool.pl $(DESTDIR)$(bindir)