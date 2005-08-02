# $Id$

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I.

libircd_a_SOURCES =                     	\
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

noinst_LIBRARIES = libircd.a


