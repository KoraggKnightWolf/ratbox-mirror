# $Id$

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I. -I../include

#if WS2
#WIN32=win32.c
#endif 

libircd_la_SOURCES = $(WIN32)                    	\
	balloc.c			\
	commio.c			\
	event.c				\
	ircd_lib.c			\
	ircd_memory.c			\
	linebuf.c			\
	snprintf.c			\
	tools.c				\
	@SELECT_TYPE@.c		

.c.lo:
	$(LIBTOOL) --mode=compile $(CC) -prefer-pic -DHAVE_CONFIG_H $(INCLUDES) $(AM_CPPFLAGS) $(CFLAGS) -c $< -o $@

.c.la:
	$(LIBTOOL) --mode=link  $(CC) -rpath $(moduledir) -avoid-version $(CFLAGS) -o $@ $< $(LIBS) $(LDFLAGS)

#noinst_LIBRARIES = libircd.a
libircd_la_LDFLAGS = -avoid-version -no-undefined
lib_LTLIBRARIES = libircd.la

