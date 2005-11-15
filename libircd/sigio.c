/*
 *  ircd-ratbox: A slightly useful ircd.
 *  sigio.c: Linux Realtime SIGIO compatible network routines.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2001 Adrian Chadd <adrian@creative.net.au>
 *  Copyright (C) 2002 Aaron Sethman <androsyn@ratbox.org>
 *  Copyright (C) 2002 ircd-ratbox development team
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1		/* Needed for F_SETSIG */
#endif

#include <signal.h>
#include <sys/poll.h>
#include "tools.h"
#include "commio.h"
#include "ircd_memory.h"


static struct pollfd *pfds;
static int used_count = 0;
static fde_t **index_to_fde;

static int sigio_signal;
static int sigio_is_screwed = 0;	/* We overflowed our sigio queue */
static sigset_t our_sigset;
static void handle_timer(struct siginfo *si);
static void poll_update_pollfds(int, short, PF *);

/* 
 * static void mask_our_signal(int s)
 *
 * Input: Signal to block
 * Output: None
 * Side Effects:  Block the said signal
 */
static void
mask_our_signal(int s)
{
	sigemptyset(&our_sigset);
	sigaddset(&our_sigset, s);
	sigaddset(&our_sigset, SIGIO);
	sigprocmask(SIG_BLOCK, &our_sigset, NULL);
}

static void
poll_update_pollfds(int fd, short event, PF * handler)
{
	fde_t *F = find_fd(fd);
	struct pollfd *pf;
	int ircd_index;

	if(F->ircd_index < 0)
	{
		used_count++;
		F->ircd_index = used_count - 1;
		index_to_fde[F->ircd_index] = F;
	}
	ircd_index = F->ircd_index;

	pf = &pfds[ircd_index];


	/* Update the events */
	if(handler != NULL)
	{
		pf->events |= event;
		pf->fd = fd;
	}
	else
	{
		if(ircd_index >= 0 && used_count > 0)
		{
			pf->events &= ~event;
			if(pf->events == 0)
			{
				pf->fd = -1;
				pf->revents = 0;
				lircd_assert(used_count > 0);
				if(F->ircd_index != used_count - 1)
				{
					index_to_fde[used_count - 1]->ircd_index = F->ircd_index;
					pfds[F->ircd_index] = pfds[used_count - 1];
				}
				pfds[used_count - 1].fd = -1;
				F->ircd_index = -1;
				used_count--;
			}
		}
	}
}


/* XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX */
/* Public functions */


/*
 * void setup_sigio_fd(int fd)
 * 
 * Input: File descriptor
 * Output: None
 * Side Effect: Sets the FD up for SIGIO
 */
int
ircd_setup_fd(int fd)
{
	fde_t *F = find_fd(fd);
	int flags = 0;
	flags = fcntl(fd, F_GETFL, 0);
	if(flags == -1)
		return 0;
	flags |= O_ASYNC | O_NONBLOCK;
	if(fcntl(fd, F_SETFL, flags) == -1)
		return 0;
	
	if(fcntl(fd, F_SETSIG, sigio_signal) == -1)
		return 0;

	if(fcntl(fd, F_SETOWN, getpid()) == -1)
		return 0;

	F->flags.nonblocking = 1;
	return 1;
}

/*
 * void init_netio(void)
 *
 * Input: None
 * Output: None
 * Side Effects: This is a needed exported function which will 
 *		 be called to initialise the network loop code.
 */
void
init_netio(void)
{
	sigio_signal = SIGRTMIN;
	pfds = ircd_malloc(maxconnections * sizeof(struct pollfd));
	index_to_fde = ircd_malloc(maxconnections * sizeof(fde_t *));
	sigio_is_screwed = 1; /* Start off with poll first.. */
	mask_our_signal(sigio_signal);
}

/*
 * ircd_setselect
 *
 * This is a needed exported function which will be called to register
 * and deregister interest in a pending IO state for a given FD.
 */
void
ircd_setselect(int fd, unsigned int type, PF * handler,
	       void *client_data)
{
	fde_t *F;
	lircd_assert(fd >= 0);
	F = find_fd(fd);
	lircd_assert(F->flags.open);

	if(type & IRCD_SELECT_READ)
	{
		F->read_handler = handler;
		F->read_data = client_data;
		poll_update_pollfds(F->fd, POLLIN, handler);
	}
	if(type & IRCD_SELECT_WRITE)
	{
		F->write_handler = handler;
		F->write_data = client_data;
		poll_update_pollfds(F->fd, POLLOUT, handler);
	}
}

/* int ircd_select(unsigned long delay)
 * Input: The maximum time to delay.
 * Output: Returns -1 on error, 0 on success.
 * Side-effects: Deregisters future interest in IO and calls the handlers
 *               if an event occurs for an FD.
 * Comments: Check all connections for new connections and input data
 * that is to be processed. Also check for connections with data queued
 * and whether we can write it out.
 * Called to do the new-style IO, courtesy of squid (like most of this
 * new IO code). This routine handles the stuff we've hidden in
 * ircd_setselect and fd_table[] and calls callbacks for IO ready
 * events.
 */
int
ircd_select(unsigned long delay)
{
	int num = 0;
	int revents = 0;
	int sig;
	int fd;
	int ci;
	int rused;
	PF *hdl;
	fde_t *F;
	struct siginfo si;
#if 0
	struct timespec timeout;

	timeout.tv_sec = (delay / 1000);
	timeout.tv_nsec = (delay % 1000) * 1000000;
#endif
	for (;;)
	{
		if(!sigio_is_screwed)
		{
#if 0
			if((sig = sigtimedwait(&our_sigset, &si, &timeout)) > 0)
#endif
			if((sig = sigwaitinfo(&our_sigset, &si)) > 0)
			{
				if(sig == SIGIO)
				{
					ircd_lib_log("Kernel RT Signal queue overflowed.  Is /proc/sys/kernel/rtsig-max too small?");
					sigio_is_screwed = 1;
					break;
				}
				if(si.si_code == SI_TIMER)
				{
					handle_timer(&si);
					continue;					
				}
				
				fd = si.si_fd;
				pfds[fd].revents |= si.si_band;
				revents = pfds[fd].revents;
				num++;
				F = find_fd(fd);
				if(!F->flags.open || F->fd < 0)
					continue;

				ircd_set_time();
				if(F->flags.open
				   && (revents & (POLLRDNORM | POLLIN | POLLHUP | POLLERR)))
				{
					hdl = F->read_handler;
					F->read_handler = NULL;
					if(hdl)
						hdl(F->fd, F->read_data);
				}

				if(F->flags.open
				   && (revents & (POLLWRNORM | POLLOUT | POLLHUP | POLLERR)))
				{
					hdl = F->write_handler;
					F->write_handler = NULL;
					if(hdl)
						hdl(F->fd, F->write_data);
				}

				if(F->read_handler == NULL)
					poll_update_pollfds(fd, POLLIN, NULL);
				if(F->write_handler == NULL)
					poll_update_pollfds(fd, POLLOUT, NULL);

				F = NULL;
			}
			else
				break;

		}
		else
			break;
	}

	if(!sigio_is_screwed)	/* We don't need to proceed */
	{
		ircd_set_time();
		return 0;
	}
	for (;;)
	{
		if(sigio_is_screwed)
		{
			signal(sigio_signal, SIG_IGN);
			signal(sigio_signal, SIG_DFL);
			sigio_is_screwed = 0;
		}
		num = poll(pfds, used_count, 0);
		if(num >= 0)
			break;
		if(ignoreErrno(errno))
			continue;
		/* error! */
		ircd_set_time();
		return -1;
		/* NOTREACHED */
	}

	/* update current time again, eww.. */
	ircd_set_time();

	if(num == 0)
		return 0;
	rused = used_count;
	/* XXX we *could* optimise by falling out after doing num fds ... */
	for (ci = 0; ci < rused; ci++)
	{
		if(((revents = pfds[ci].revents) == 0) || (pfds[ci].fd) == -1)
			continue;
		fd = pfds[ci].fd;
		F = find_fd(fd);
		if(revents & (POLLRDNORM | POLLIN | POLLHUP | POLLERR))
		{
			hdl = F->read_handler;
			F->read_handler = NULL;
			if(hdl)
				hdl(F->fd, F->read_data);
		}

		if(F->flags.open == 0)
			continue;	/* Read handler closed us..go on */
		if(F->flags.open && (revents & (POLLWRNORM | POLLOUT | POLLHUP | POLLERR)))
		{
			hdl = F->write_handler;
			F->write_handler = NULL;
			if(hdl)
				hdl(F->fd, F->write_data);
		}

		if(F->read_handler == NULL)
			poll_update_pollfds(fd, POLLIN, NULL);
		if(F->write_handler == NULL)
			poll_update_pollfds(fd, POLLOUT, NULL);

	}
	mask_our_signal(sigio_signal);
	return 0;
}


static void 
handle_timer(struct siginfo *si)
{
	struct timer_data *tdata;
	tdata = si->si_ptr;
	tdata->td_cb(tdata->td_udata);
	if (!tdata->td_repeat)
		free(tdata);
}

ircd_event_id
ircd_schedule_event(time_t when, int repeat, ircd_event_cb_t cb, void *udata)
{
	timer_t	 	 id;
struct	timer_data	*tdata;
struct	sigevent	 ev;
struct	itimerspec	 ts;
	
	memset(&ev, 0, sizeof(ev));

	tdata = malloc(sizeof(struct timer_data));
	tdata->td_cb = cb;
	tdata->td_udata = udata;

	ev.sigev_notify = SIGEV_SIGNAL;
	ev.sigev_signo = SIGRTMIN;
	ev.sigev_value.sival_ptr = tdata;
	
	if (timer_create(CLOCK_REALTIME, &ev, &id) < 0)
		ircd_lib_log("timer_create: %s\n", strerror(errno));

	tdata->td_timer_id = id;

	memset(&ts, 0, sizeof(ts));
	ts.it_value.tv_sec = when;
	ts.it_value.tv_nsec = 0;

	if (repeat)
		ts.it_interval = ts.it_value;
	tdata->td_repeat = repeat;

	if (timer_settime(id, 0, &ts, NULL) < 0)
		ircd_lib_log("timer_settime: %s\n", strerror(errno));
	return tdata;
}

void
ircd_unschedule_event(ircd_event_id id)
{
	timer_delete(id->td_timer_id);
	free(id);
}
