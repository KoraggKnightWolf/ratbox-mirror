# $Id$

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I. -I../include

if !MINGW
OS_FILES=unix.c
endif

if NEED_CRYPT
CRYPT=crypt.c
endif

libircd_la_SOURCES = $(OS_FILES) $(CRYPT)	\
	balloc.c			\
	commio.c			\
	event.c				\
	ircd_lib.c			\
	ircd_memory.c			\
	linebuf.c			\
	snprintf.c			\
	tools.c				\
	helper.c			\
	@SELECT_TYPE@.c		

libircd_la_LDFLAGS = -avoid-version -no-undefined
lib_LTLIBRARIES = libircd.la

