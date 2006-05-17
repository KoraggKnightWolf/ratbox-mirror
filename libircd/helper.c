/*
 *  ircd-ratbox: A slightly useful ircd
 *  helper.c: Starts and deals with ircd helpers
 *  
 *  Copyright (C) 2006 Aaron Sethman <androsyn@ratbox.org>
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

#include "ircd_lib.h"

/*
 * start_fork_helper
 * starts a new ircd helper
 * note that this function doesn't start doing reading..thats the job of the caller
 */

ircd_helper *
ircd_helper_start(const char *name, const char *fullpath, ircd_helper_cb *read_cb, ircd_helper_cb *restart_cb)
{
	ircd_helper *helper;
	const char *parv[2];
	char buf[128];
	char fx[16], fy[16];
	int ifd[2];
	int ofd[2];
	pid_t pid;
			
	if(access(fullpath, X_OK) == -1)
		return NULL;
	
	helper = ircd_malloc(sizeof(ircd_helper));

	ircd_snprintf(buf, sizeof(buf), "%s helper - read", name);
	if(ircd_pipe(ifd, buf) < 0) 
	{
		ircd_free(helper);
		return NULL;
	}
	ircd_snprintf(buf, sizeof(buf), "%s helper - write", name);
	if(ircd_pipe(ofd, buf) < 0)
	{
		ircd_free(helper);
		return NULL;
	}
	
	ircd_snprintf(fx, sizeof(fx), "%d", ifd[1]);
	ircd_snprintf(fy, sizeof(fy), "%d", ofd[0]);
	
	ircd_set_nb(ifd[0]);
	ircd_set_nb(ifd[1]);
	ircd_set_nb(ofd[0]);
	ircd_set_nb(ofd[1]);
	
	setenv("IFD", fy, 1);
	setenv("OFD", fx, 1);
	setenv("MAXFD", "256", 1);
	
	ircd_snprintf(buf, sizeof(buf), "-ircd %s daemon", name);
	parv[0] = buf;
	parv[1] = NULL;

#ifdef _WIN32      
	SetHandleInformation((HANDLE)ifd[1], HANDLE_FLAG_INHERIT, 1);
	SetHandleInformation((HANDLE)ofd[0], HANDLE_FLAG_INHERIT, 1);
#endif
                
	pid = ircd_spawn_process(fullpath, (const char **)parv);
                        
	if(pid == -1)
	{
		ircd_close(ifd[0]);
		ircd_close(ifd[1]);
		ircd_close(ofd[0]);
		ircd_close(ofd[1]);
		ircd_free(helper);
		return NULL;
	}

	ircd_close(ifd[1]);
	ircd_close(ofd[0]);
	
	helper->ifd = ifd[0];
	helper->ofd = ofd[1];
	helper->read_cb = read_cb;
	helper->restart_cb = restart_cb;	
	helper->fork_count = 0;
	helper->pid = pid;

	return helper;
}


void
ircd_helper_restart(ircd_helper *helper)
{
	helper->restart_cb(helper);
}


static void
ircd_helper_write_sendq(int fd, void *helper_ptr)
{
	ircd_helper *helper = helper_ptr;
	int retlen;
	
	if(ircd_linebuf_len(&helper->sendq) > 0)
	{
		while((retlen = ircd_linebuf_flush(fd, &helper->sendq)) > 0)
			;;
		if(retlen == 0 || (retlen < 0 && !ignoreErrno(errno)))
			ircd_helper_restart(helper);
		
	}
	if(helper->ofd < 0)
		return;

	if(ircd_linebuf_len(&helper->sendq) > 0)
		ircd_setselect(helper->ofd, IRCD_SELECT_WRITE, ircd_helper_write_sendq, &helper->sendq);
}

void
ircd_helper_write(ircd_helper *helper, const char *format, ...)
{
	va_list ap;
	if(helper->ifd < 0 || helper->ofd < 0) 
		return; /* XXX error */
	
	va_start(ap, format);
	ircd_linebuf_putmsg(&helper->sendq, format, &ap, NULL);
	ircd_helper_write_sendq(helper->ofd, helper);	
	va_end(ap);
}

void
ircd_helper_read(int fd, void *helper_ptr)
{
	ircd_helper *helper = helper_ptr;
	char buf[READBUF_SIZE];
	int length;
	
	while((length = ircd_read(helper->ifd, buf, sizeof(buf))) > 0)
	{
		ircd_linebuf_parse(&helper->recvq, buf, length, 0);
		helper->read_cb(helper);
	}

	if(length == 0 || (length < 0 && !ignoreErrno(errno)))
		ircd_helper_restart(helper);
	
	if(helper->ifd < 0)
		return;
	
	ircd_setselect(helper->ifd, IRCD_SELECT_READ, ircd_helper_read, helper);
}

void
ircd_helper_close(ircd_helper *helper)
{
	if(helper == NULL)
		return;
		
	ircd_close(helper->ifd);
	ircd_close(helper->ofd);
	ircd_free(helper);	
}

