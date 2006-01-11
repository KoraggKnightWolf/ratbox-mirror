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

#include "ircd_lib.h"

#include <signal.h>
#include <sys/poll.h>

/* I hate linux -- adrian */
#ifndef POLLRDNORM
#define POLLRDNORM POLLIN
#endif
#ifndef POLLWRNORM
#define POLLWRNORM POLLOUT
#endif

struct _pollfd_list
{
	struct pollfd *pollfds;
	int maxindex;		/* highest FD number */
};

typedef struct _pollfd_list pollfd_list_t;

pollfd_list_t pollfd_list;
static void handle_timer(struct siginfo *si);

static int sigio_signal;
static int sigio_is_screwed = 0;	/* We overflowed our sigio queue */
static sigset_t our_sigset;

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

/*
 * init_netio
 *
 * This is a needed exported function which will be called to initialise
 * the network loop code.
 */
void
init_netio(void)
{
	int fd;
        pollfd_list.pollfds = ircd_malloc(ircd_maxconnections * (sizeof(struct pollfd)));
        for (fd = 0; fd < ircd_maxconnections; fd++)
        {
        	pollfd_list.pollfds[fd].fd = -1;
	}

	pollfd_list.maxindex = 0;
                                                                
        sigio_signal = SIGRTMIN;
	sigio_is_screwed = 1; /* Start off with poll first.. */
	mask_our_signal(sigio_signal);
}


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
 * ircd_setselect
 *
 * This is a needed exported function which will be called to register
 * and deregister interest in a pending IO state for a given FD.
 */
void
ircd_setselect(int fd, unsigned int type, PF * handler,
               void *client_data)
{ 
        fde_t *F = find_fd(fd);
        int old_flags;
        
        if(F == NULL)
                return;

        old_flags = F->pflags;
 
        if(type & IRCD_SELECT_READ)
        {
                F->read_handler = handler;
                F->read_data = client_data;
                if(handler != NULL)
                        F->pflags |= POLLRDNORM;
                else
                        F->pflags &= ~POLLRDNORM;
        }
        if(type & IRCD_SELECT_WRITE)
        {
                F->write_handler = handler;
                F->write_data = client_data;
                if(handler != NULL)
                        F->pflags |= POLLWRNORM;
                else
                        F->pflags &= ~POLLWRNORM;
        }
  
        if(F->pflags <= 0)
        {
                pollfd_list.pollfds[fd].events = 0;
                pollfd_list.pollfds[fd].fd = -1;
                if(fd == pollfd_list.maxindex)
                {
                        while (pollfd_list.maxindex >= 0 && pollfd_list.pollfds[pollfd_list.maxindex].fd == -1)
                                pollfd_list.maxindex--;
                }
        } else {
                pollfd_list.pollfds[fd].events = F->pflags;
                pollfd_list.pollfds[fd].fd = fd;
                if(fd > pollfd_list.maxindex)
                        pollfd_list.maxindex = fd;
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
	PF *hdl;
	fde_t *F;
	void *data;
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
#else
			if((sig = sigwaitinfo(&our_sigset, &si)) > 0)
#endif
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
				pollfd_list.pollfds[fd].revents |= si.si_band;
				revents = pollfd_list.pollfds[fd].revents;
				num++;
				F = find_fd(fd);
				if(F == NULL)
					continue;

				if(revents & (POLLRDNORM | POLLIN | POLLHUP | POLLERR))
				{
					hdl = F->read_handler;
					data = F->read_data;
					F->read_handler = NULL;
					F->read_data = NULL;
					if(hdl)
						hdl(F->fd, data);
				}

				if(revents & (POLLWRNORM | POLLOUT | POLLHUP | POLLERR))
				{
					hdl = F->write_handler;
					data = F->write_data;
					F->write_handler = NULL;
					F->write_data = NULL;
					if(hdl)
						hdl(F->fd, data);
				}
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

	signal(sigio_signal, SIG_IGN);
	signal(sigio_signal, SIG_DFL);
	sigio_is_screwed = 0;

	for (;;)
	{
		/* XXX kill that +1 later ! -- adrian */
		num = poll(pollfd_list.pollfds, pollfd_list.maxindex + 1, delay);
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
	/* XXX we *could* optimise by falling out after doing num fds ... */
	for (ci = 0; ci < pollfd_list.maxindex + 1; ci++)
	{
		fde_t *F;
		int revents;
		if(((revents = pollfd_list.pollfds[ci].revents) == 0) ||
		   (pollfd_list.pollfds[ci].fd) == -1)
			continue;
		fd = pollfd_list.pollfds[ci].fd;
		F = find_fd(fd);
		if(F == NULL)
			continue;
		if(revents & (POLLRDNORM | POLLIN | POLLHUP | POLLERR))
		{
			hdl = F->read_handler;
			data = F->read_data;
			F->read_handler = NULL;
			F->read_data = NULL;
			if(hdl)
				hdl(fd, data);
		}

		if(revents & (POLLWRNORM | POLLOUT | POLLHUP | POLLERR))
		{
			hdl = F->write_handler;
			data = F->write_data;
			F->write_handler = NULL;
			F->write_data = NULL;
			if(hdl)
				hdl(fd, data);
		}
	}
	return 0;
}

static void 
handle_timer(struct siginfo *si)
{
	struct timer_data *tdata;
	tdata = si->si_ptr;
	tdata->td_cb(tdata->td_udata);
	if (!tdata->td_repeat)
		ircd_free(tdata);
}

ircd_event_id
ircd_schedule_event(time_t when, int repeat, ircd_event_cb_t cb, void *udata)
{
	timer_t	 	 id;
struct	timer_data	*tdata;
struct	sigevent	 ev;
struct	itimerspec	 ts;
	
	memset(&ev, 0, sizeof(ev));

	tdata = ircd_malloc(sizeof(struct timer_data));
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
	ircd_free(id);
}
