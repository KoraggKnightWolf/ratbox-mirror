# $Id$
# 
# okay so i've flattened out the build stuff here.
# this is really for the best since so many things depend
# on each other now..

AUTOMAKE_OPTIONS = foreign subdir-objects 

prefix		= @prefix@
libcoredir 	= @libdir@
modulesdir	= @moduledir@/autoload
coredir		= @moduledir@
servicesdir	= @moduledir@/autoload


AM_CFLAGS = $(WARNFLAGS)

ircd_SOURCES = src/main.c
bin_PROGRAMS = ircd

if MINGW
EXTRA_FLAGS = -no-undefined -Wl,--enable-runtime-pseudo-reloc -export-symbols-regex '*'
endif

if !STATIC_MODULES

SUBDIRS = libratbox libltdl src modules tools doc help @SQLITE_SUBDIR@ bandb ssld resolver ident
ircd_LDADD = libratbox/src/libratbox.la src/libcore.la $(LIBLTDL)
ircd_LDFLAGS = $(EXTRA_FLAGS) -dlopen self

else

SUBDIRS = libratbox libltdl modules src tools doc help @SQLITE_SUBDIR@ bandb ssld resolver ident
ircd_LDADD = libratbox/src/libratbox.la modules/libmodules.la src/libcore.la modules/static_modules.o $(LIBLTDL) $(DLOPEN)


endif




