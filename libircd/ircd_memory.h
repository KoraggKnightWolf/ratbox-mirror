/*
 *  ircd-ratbox: A slightly useful ircd.
 *  memory.h: A header for the memory functions.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002-2005 ircd-ratbox development team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 *
 *  $Id$
 */

#ifndef IRCD_LIB_H
#error "Do not use ircd_memory.h directly"
#endif

#ifndef _I_MEMORY_H
#define _I_MEMORY_H


extern void ircd_outofmemory(void);

extern void *ircd_malloc(size_t size);
extern void *ircd_realloc(void *x, size_t y);

/* forte (and maybe others) dont like double declarations, 
 * so we dont declare the inlines unless GNUC
 */
/* darwin doesnt like these.. */
#ifndef __APPLE__

#ifdef __GNUC__
extern inline void *
ircd_malloc(size_t size)
{
	void *ret = calloc(1, size);
	if(unlikely(ret == NULL))
		ircd_outofmemory();
	return (ret);
}

extern inline void *
ircd_realloc(void *x, size_t y)
{
	void *ret = realloc(x, y);

	if(unlikely(ret == NULL))
		ircd_outofmemory();
	return (ret);
}

#endif /* __GNUC__ */
#endif /* __APPLE__ */

#define ircd_free(x) do { if(likely(x != NULL)) free(x); } while (0)

#endif /* _I_MEMORY_H */
