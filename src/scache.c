/*
 *  ircd-ratbox: A slightly useful ircd.
 *  scache.c: Server names cache.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002-2012 ircd-ratbox development team
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 *  USA
 *
 *  $Id$
 */
#include <stdinc.h>
#include <ratbox_lib.h>
#include <match.h>
#include <ircd.h>
#include <numeric.h>
#include <send.h>
#include <hash.h>
#include <scache.h>

/*
 * this code intentionally leaks a little bit of memory, unless you're on a network
 * where you've got somebody screwing around and bursting a *lot* of servers, it shouldn't
 * be an issue...
 */

struct scache_entry
{
	rb_dlink_node node;
	char *server_name;
};

const char *
scache_add(const char *name)
{
	struct scache_entry *sc;
	hash_node *hnode;

	if(EmptyString(name))
		return NULL;

	if((hnode = hash_find(HASH_SCACHE, name)) != NULL)
	{
		sc = hnode->data;
		return sc->server_name;
	}

	sc = rb_malloc(sizeof(struct scache_entry));
	sc->server_name = rb_strdup(name);
	hash_add(HASH_SCACHE, sc->server_name, sc);
	return sc->server_name;
}

void
count_scache(size_t * number, size_t * mem)
{

	*number = 0;
	*mem = 0;
}
