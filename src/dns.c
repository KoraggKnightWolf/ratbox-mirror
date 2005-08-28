/*
 *  dns.c: An interface to the resolver daemon
 *  Copyright (C) 2005 Aaron Sethman <androsyn@ratbox.org>
 *  Copyright (C) 2005 ircd-ratbox development team
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



#include "stdinc.h"
#include "ircd_lib.h"
#include "struct.h"
#include "ircd_defs.h"
#include "parse.h"
#include "res.h"
#include "irc_string.h"
#include "s_log.h"
#include "s_conf.h"
#include "client.h"
#include "send.h"

#define IDTABLE 0xffff

#define DNS_HOST 	((char)'H')
#define DNS_REVERSE 	((char)'I')

static void submit_dns(const char, int id, int aftype, const char *addr);
static void fork_resolver(void);

static char dnsBuf[READBUF_SIZE];
static buf_head_t dns_sendq;
static buf_head_t dns_recvq;

static pid_t res_pid;
static int need_restart = 0;

struct dnsreq
{
	DNSCB *callback;
	void *data;
};

static struct dnsreq querytable[IDTABLE];
static u_int16_t id = 1;
static int dns_ifd = -1;
static int dns_ofd = -1;

static u_int16_t 
assign_id(void)
{
	if(id < IDTABLE-1)
		id++;
	else
		id = 1;
	return(id);	
}


void
cancel_lookup(u_int16_t xid)
{
	struct dnsreq *req;
	req = &querytable[xid];
	req->callback = NULL;
	req->data = NULL;
}

u_int16_t
lookup_hostname(const char *hostname, int aftype, DNSCB *callback, void *data)
{
	struct dnsreq *req;
	int aft;
	u_int16_t nid;
	
	nid = assign_id();
	req = &querytable[nid];

	req->callback = callback;
	req->data = data;
	
#ifdef IPV6
	if(aftype == AF_INET6)
		aft = 6;
	else
#endif
		aft = 4;	
	
	submit_dns(DNS_HOST, nid, aft, hostname); 		
	return(id);
}

u_int16_t
lookup_ip(const char *addr, int aftype, DNSCB *callback, void *data)
{
	struct dnsreq *req;
	int aft;
	u_int16_t nid;
	
	nid = assign_id();
	req = &querytable[nid];

	req->callback = callback;
	req->data = data;
	
#ifdef IPV6
	if(aftype == AF_INET6)
	{
		if(ConfigFileEntry.fallback_to_ip6_int)
			aft = 5;
		else
			aft = 6;
	}
	else
#endif
		aft = 4;	
	
	submit_dns(DNS_REVERSE, nid, aft, addr); 		
	return(nid);
}


static void
results_callback(const char *callid, const char *status, const char *aftype, const char *results)
{
	struct dnsreq *req;
	u_int16_t nid;
	int st;
	int aft;
	nid = strtol(callid, NULL, 16);
	req = &querytable[nid];
	st = atoi(status);
	aft = atoi(aftype);
	if(req->callback == NULL)
	{
		/* got cancelled..oh well */
		req->data = NULL;
		return;
	}
#ifdef IPV6
	if(aft == 6 || aft == 5)
		aft = AF_INET6;
	else
#endif
		aft = AF_INET;
		
	req->callback(results, st, aft, req->data);
	req->callback = NULL;
	req->data = NULL;
}

static int fork_count = 0;
static int spin_restart = 0;
static void
restart_spinning_resolver(void *unused)
{
	if(spin_restart > 10)
	{
		ilog(L_MAIN, "Tried to wait and restart the resolver %d times, giving up", spin_restart);
		sendto_realops_flags(UMODE_ALL, L_ALL, "Tried to wait and restart the resolver %d times, giving up", spin_restart);
		sendto_realops_flags(UMODE_ALL, L_ALL, "Try a manual restart with /rehash dns");
		spin_restart = 0;
		fork_count = 0;
		return;
	}
	fork_count = 0; /* reset the fork_count to 0 to let it try again */
	spin_restart++;
	fork_resolver();
}

static void
fork_resolver(void)
{
	pid_t pid;
	int i;
	int ifd[2];
	int ofd[2];
	
	char fx[6];   
	char fy[6];
	char maxfd[6];

	if(fork_count > 10)
	{
		ilog(L_MAIN, "Resolver has forked %d times, waiting 15 seconds to restart it again", fork_count);
		ilog(L_MAIN, "DNS resolution will be unavailable during this time");
		sendto_realops_flags(UMODE_ALL, L_ALL, "Resolver has forked %d times waiting 30 seconds to restart it again", fork_count);
		sendto_realops_flags(UMODE_ALL, L_ALL, "DNS resolution will be unavailable during this time");
		eventAddOnce("restart_spinning_resolver", restart_spinning_resolver, NULL, 30);		
		return;
	}
	fork_count++;
	if(dns_ifd > 0)
		comm_close(dns_ifd);
	if(dns_ofd > 0)
		comm_close(dns_ofd);
#ifndef SIGKILL
#define SIGKILL SIGTERM
#endif
	if(res_pid > 0)
		kill(res_pid, SIGKILL);

	comm_pipe(ifd, "resolver daemon - read");
	comm_pipe(ofd, "resolver daemon - write");

	ircsnprintf(fx, sizeof(fx), "%d", ifd[1]); /*dns write*/
	ircsnprintf(fy, sizeof(fy), "%d", ofd[0]); /*dns read*/ 
	ircsnprintf(maxfd, sizeof(maxfd), "%d", HARD_FDLIMIT);
	comm_set_nb(ifd[0]);
	comm_set_nb(ifd[1]);
	comm_set_nb(ifd[0]);
	comm_set_nb(ofd[1]);

	setenv("IFD", fy, 1);
	setenv("OFD", fx, 1);
	setenv("MAXFD", maxfd, 1);
	if(!(pid = vfork()))
	{
		execl(BINPATH "/resolver", "-ircd dns resolver", NULL);
		_exit(1);
	} else
	if(pid == -1)
	{
		ilog(L_MAIN, "fork failed: %s", strerror(errno));
		comm_close(ifd[0]);
		comm_close(ifd[1]);
		comm_close(ofd[0]);
		comm_close(ofd[1]);
		return;
	}

	comm_close(ifd[1]);
	comm_close(ofd[0]);


	dns_ifd = ifd[0];
	dns_ofd = ofd[1];

	fork_count = 0;
	res_pid = pid;
	return;
}

static void
parse_dns_reply(void)
{
	int len, parc;
	char *parv[MAXPARA+1];
	while((len = linebuf_get(&dns_recvq, dnsBuf, sizeof(dnsBuf), 
				 LINEBUF_COMPLETE, LINEBUF_PARSED)) > 0)
	{
		parc = string_to_array(dnsBuf, parv); /* we shouldn't be using this here, but oh well */

		if(parc != 5)
		{
			ilog(L_MAIN, "Resolver sent a result with wrong number of arguments");
			fork_resolver();
			return;
		}
		results_callback(parv[1], parv[2], parv[3], parv[4]);
	}
}

static void
read_dns(int fd, void *data)
{
	int length;

	while((length = comm_read(dns_ifd, dnsBuf, sizeof(dnsBuf))) > 0)
	{
		linebuf_parse(&dns_recvq, dnsBuf, length, 0);
		parse_dns_reply();
	}
   
	if(length == 0)
		fork_resolver();
 
	if(length == -1 && !ignoreErrno(errno))
		fork_resolver(); 

	comm_setselect(dns_ifd, COMM_SELECT_READ, read_dns, NULL, 0);
}

static void
dns_write_sendq(int fd, void *unused)
{
	int retlen;
	if(linebuf_len(&dns_sendq) > 0)
	{
		while((retlen = linebuf_flush(dns_ofd, &dns_sendq)) > 0);
		if(retlen == 0 || (retlen < 0 && !ignoreErrno(errno)))
		{
			fork_resolver();
		}
	}
	 
	if(linebuf_len(&dns_sendq) > 0)
	{
		comm_setselect(dns_ofd, COMM_SELECT_WRITE,
			       dns_write_sendq, NULL, 0);
	}
}
 

void 
submit_dns(char type, int nid, int aftype, const char *addr)
{
	linebuf_put(&dns_sendq, "%c %x %d %s", type, nid, aftype, addr);
	dns_write_sendq(dns_ofd, NULL);
	read_dns(dns_ifd, NULL);
}

void
init_resolver(void)
{
	fork_resolver();
	linebuf_newbuf(&dns_sendq);
	linebuf_newbuf(&dns_recvq);
	
	if(res_pid < 0)
	{
		ilog(L_MAIN, "Unable to fork resolver: %s", strerror(errno));		
		exit(0);
	}
}

void 
restart_resolver(void)
{
	fork_resolver();
}

void
resolver_sigchld(void)
{
	int status;
#ifndef __MINGW32__
	if(waitpid(res_pid, &status, WNOHANG) == res_pid)
	{
		need_restart = 1;		
	}
#endif
}

