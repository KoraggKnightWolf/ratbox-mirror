/*
 *  ircd-ratbox: A slightly useful ircd.
 *  event.h: The ircd event header.
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
# error "Do not use event.h directly"                                   
#endif

#ifndef INCLUDED_event_h
#define INCLUDED_event_h

#if (defined(USE_SIGIO) || (USE_PORTS))  && defined(HAVE_TIMER_CREATE) && defined(_POSIX_TIMERS)
#define USE_POSIX_TIMERS 1
#endif

/*
 * How many event entries we need to allocate at a time in the block
 * allocator. 16 should be plenty at a time.
 */
#define	MAX_EVENTS	50


typedef void EVH(void *);
struct timer_data; 

/* The list of event processes */
struct ev_entry
{
	EVH *func;
	void *arg;
	const char *name;
	time_t frequency;
	time_t when;
	int active;
#ifdef USE_POSIX_TIMERS
	struct timer_data * ircd_id;
#endif
};

extern void eventAdd(const char *name, EVH * func, void *arg, time_t when);
extern void eventAddOnce(const char *name, EVH * func, void *arg, time_t when);
extern void eventAddIsh(const char *name, EVH * func, void *arg, time_t delta_ish);
extern void eventRun(void);
extern void eventInit(void);
extern void eventDelete(EVH * func, void *);
void eventUpdate(const char *name, time_t freq);
extern int eventFind(EVH * func, void *);
extern void set_back_events(time_t);
int dump_events(void (*func)(char *, void *), void *ptr);


#endif /* INCLUDED_event_h */
