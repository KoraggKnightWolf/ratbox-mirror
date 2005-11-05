/************************************************************************
 *   IRC - Internet Relay Chat, servlink/servlink.c
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 1, or (at your option)
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
 *
 *   $Id$
 */

#include "setup.h"

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef HAVE_LIBZ
#include <zlib.h>
#endif

#include "servlink.h"
#include "io.h"
#include "control.h"

static void usage(void);

struct slink_state in_state;
struct slink_state out_state;

struct fd_table fds[3] = {
	{0, read_ctrl, NULL}, 	/* ctrl */
	{0, NULL, NULL}, 	/* data */
	{0, NULL, NULL},	/* net */
};

/* usage();
 *
 * Display usage message
 */
static void
usage(void)
{
	fprintf(stderr, "ircd-ratbox server link v1.3\n");
	fprintf(stderr, "2005-11-04\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "This program is called by the ircd-ratbox ircd.\n");
	fprintf(stderr, "It cannot be used on its own.\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	int max_fd = 0;
	int i, x;
	int maxconnects;
	char *tmaxconnects, *ctrlfd, *datafd, *netfd;
#ifdef SERVLINK_DEBUG
	int GDBAttached = 0;

	while (!GDBAttached)
		sleep(1);
#endif
	tmaxconnects = getenv("MAXFD");
	ctrlfd = getenv("CTRLFD");
	datafd = getenv("DATAFD");
	netfd = getenv("NETFD");

	/* Make sure we are running under ircd.. */
	
	if(argc != 2 || strcmp(argv[0], "-ircd servlink") || 
		tmaxconnects == NULL || ctrlfd == NULL || datafd == NULL || netfd == NULL)
		usage();	/* exits */

	maxconnects = atoi(tmaxconnects);
	fds[0].fd = atoi(ctrlfd);
	fds[1].fd = atoi(datafd);
	fds[2].fd = atoi(netfd);
	
	for(i = 0; i < maxconnects; i++)
	{
		if(i != fds[0].fd && i != fds[1].fd && i != fds[2].fd)
			close(i);
	}

	for (i = 0; i < 3; i++)
	{		
		/* XXX: Hack alert...we need to do dup2() here for some dumb
		 * platforms (Solaris) that don't like select using fds > 255
		 */

		if(fds[i].fd >= 255)
		{
			for(x = 0; x < 255; x++)
			{
				if(x != fds[0].fd && x != fds[1].fd && x != fds[2].fd)
				{
					if(dup2(fds[i].fd, x) < 0)
						exit(1);
					close(fds[i].fd);
					fds[i].fd = x;
					break;
				}
			}
		}		
		fcntl(fds[i].fd, F_SETFL, O_NONBLOCK);
		if(fds[i].fd > max_fd)
			max_fd = fds[i].fd;
	}
	
	/* enter io loop */
	io_loop(max_fd + 1);

	/* NOTREACHED */
	return (0);
}				/* main() */
