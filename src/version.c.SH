#!/bin/sh

# $Id$ 

spitshell=cat
package=ircd-ratbox

echo "Extracting $package/src/version.c..."

if test -r version.c.last
then
   generation=`sed -n 's/^static const char \*generation = \"\(.*\)\";/\1/p' < version.c.last`
   if test ! "$generation" ; then generation=0; fi
else
   generation=0
fi

generation=`expr $generation + 1`

uname=`uname -a`

creation=`LC_ALL=C date | \
awk '{if (NF == 6) \
         { print $1 " "  $2 " " $3 " "  $6 " at " $4 " " $5 } \
else \
         { print $1 " "  $2 " " $3 " " $7 " at " $4 " " $5 " " $6 }}'`

$spitshell >version.c <<!SUB!THIS!
/*
 *   IRC - Internet Relay Chat, src/version.c
 *   Copyright (C) 1990 Chelsea Ashley Dyerman
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * This file is generated by version.c.SH. Any changes made will go away.
 */

#include <stdlib.h>
#include <version.h>
#include <serno.h>

static const char *generation = "$generation";
static const char *platform = "$uname";
static const char *ircd_version = PATCHLEVEL;
static const char *serno = SERIALNUM;
static const char *creation = "$creation";

static const char *infotext[] =
{
  "$package --",
  "Based on the original code written by Jarkko Oikarinen",
  "Copyright 1988, 1989, 1990, 1991 University of Oulu, Computing Center",
  "Copyright (c) 1996-2001 Hybrid Development Team", 
  "Copyright (c) 2002-2015 ircd-ratbox Development Team",
  "",
  "This program is free software; you can redistribute it and/or",
  "modify it under the terms of the GNU General Public License as",
  "published by the Free Software Foundation; either version 2, or",
  "(at your option) any later version.",
  "",
!SUB!THIS!

IFS='
'
for i in `grep -v '^$Id' ../CREDITS |tr -d '"'` ; do
echo "  \"$i\"," >> version.c
done
$spitshell >>version.c <<!SUB!THISTOO!
  "",
  NULL,
};

void 
ratbox_version(const char **pver, const char **pserno, const char **pgeneration, const char **pplatform, const char **pcreation)
{
	if(pver != NULL)
		*pver = ircd_version;
	if(pserno != NULL)
		*pserno = serno;
	if(pgeneration != NULL)
		*pgeneration = generation;
	if(pplatform != NULL)
		*pplatform = platform;
	if(pcreation != NULL)
		*pcreation = creation;
}

void
ratbox_infotext(const char ***infoptr)
{
	if(infoptr == NULL)
		return;
	*infoptr = infotext;
}




!SUB!THISTOO!
