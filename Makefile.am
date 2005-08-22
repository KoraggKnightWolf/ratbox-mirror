# $Id$
AUTOMAKE_OPTIONS = foreign

if WANT_SERVLINK
	SERVLINK = servlink
endif

if !MINGW
	ADNS = adns
endif

SUBDIRS = libircd $(ADNS) libltdl src modules resolver ident $(SERVLINK) tools doc help
