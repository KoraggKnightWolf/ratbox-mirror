# $Id$

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I. -I../include

if MINGW
OS_FILES=win32.c
else 
OS_FILES=unix.c
endif

libircd_la_SOURCES = $(OS_FILES)	\
	balloc.c			\
	commio.c			\
	crypt.c				\
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

