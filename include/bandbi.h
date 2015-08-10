/* 
 *  ircd-ratbox: A slightly useful ircd
 *  bandbi.h: bandb related stuff
 * 
 *  $Id$
 */

#ifndef INCLUDED_bandbi_h
#define INCLUDED_bandbi_h

#include "bandb_defs.h"

void init_bandb(void);

void bandb_add(bandb_type, struct Client *source_p, const char *mask1,
	       const char *mask2, const char *reason, const char *oper_reason, int perm);
void bandb_del(bandb_type, const char *mask1, const char *mask2);
void bandb_rehash_bans(void);
void bandb_restart(void);

#endif
