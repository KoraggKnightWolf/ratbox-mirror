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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 *  USA
 *
 *  $Id$
 */
#define __EXTENSIONS__ 1


#include "stdinc.h"
#include "ircd_lib.h"
#include "struct.h"
#include "ircd_defs.h"
#include "parse.h"
#include "dns.h"
#include "match.h"
#include "s_log.h"
#include "s_conf.h"
#include "client.h"
#include "send.h"

#define IDTABLE 0xffff

#define DNS_HOST 	((char)'H')
#define DNS_REVERSE 	((char)'I')

static void submit_dns(const char, int id, int aftype, const char *addr);
static int start_resolver(void);
static void parse_dns_reply(ircd_helper *helper);
static void restart_resolver_cb(ircd_helper *helper);

static ircd_helper *dns_helper;

struct dnsreq
{
	DNSCB *callback;
	void *data;
};

static struct dnsreq querytable[IDTABLE];
static uint16_t id = 1;

static inline uint16_t 
assign_dns_id(void)
{
	if(id < IDTABLE-1)
		id++;
	else
		id = 1;
	return(id);	
}

static inline void
check_resolver(void)
{
	if(dns_helper == NULL || dns_helper->ifd < 0 || dns_helper->ofd < 0)
		restart_resolver();
}

static void
failed_resolver(uint16_t xid)
{
	struct dnsreq *req;

	req = &querytable[xid];
	if(req->callback == NULL)
		return;

	req->callback("FAILED", 0, 0, req->data);
	req->callback = NULL;
	req->data = NULL;
}

void
cancel_lookup(uint16_t xid)
{
	querytable[xid].callback = NULL;
	querytable[xid].data = NULL;
}

uint16_t
lookup_hostname(const char *hostname, int aftype, DNSCB *callback, void *data)
{
	struct dnsreq *req;
	int aft;
	uint16_t nid;
	check_resolver();	
	nid = assign_dns_id();
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

uint16_t
lookup_ip(const char *addr, int aftype, DNSCB *callback, void *data)
{
	struct dnsreq *req;
	int aft;
	uint16_t nid;
	check_resolver();
	
	nid = assign_dns_id();
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
	uint16_t nid;
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


static char *resolver_path;

static int
start_resolver(void)
{
	char fullpath [PATH_MAX + 1];
#ifdef _WIN32
	const char *suffix = ".exe";
#else
	const char *suffix = "";
#endif
	if(resolver_path == NULL)
	{	
		ircd_snprintf(fullpath, sizeof(fullpath), "%s/resolver%s", BINPATH, suffix);
	
		if(access(fullpath, X_OK) == -1)
		{
			ircd_snprintf(fullpath, sizeof(fullpath), "%s/bin/resolver%s", ConfigFileEntry.dpath, suffix);
			if(access(fullpath, X_OK) == -1)
			{
				ilog(L_MAIN, "Unable to execute resolver in %s or %s/bin", BINPATH, ConfigFileEntry.dpath);
				return 1;
			}
		
		} 

		resolver_path = ircd_strdup(fullpath);
	}

	dns_helper = ircd_helper_start("resolver", resolver_path, parse_dns_reply, restart_resolver_cb);

	if(dns_helper == NULL)
	{
		ilog(L_MAIN, "ircd_spawn_process failed: %s", strerror(errno));
		return 1;
	}

	ircd_helper_read(dns_helper->ifd, dns_helper);
	return 0;
}

static void
parse_dns_reply(ircd_helper *helper)
{
	int len, parc;
	static char dnsBuf[READBUF_SIZE];
	
	char *parv[MAXPARA+1];
	while((len = ircd_helper_readline(helper, dnsBuf, sizeof(dnsBuf))) > 0)
	{
		parc = string_to_array(dnsBuf, parv); /* we shouldn't be using this here, but oh well */

		if(parc != 5)
		{
			ilog(L_MAIN, "Resolver sent a result with wrong number of arguments");
			restart_resolver();
			return;
		}
		results_callback(parv[1], parv[2], parv[3], parv[4]);
	}
}

static void 
submit_dns(char type, int nid, int aftype, const char *addr)
{
	if(dns_helper == NULL)
	{
		failed_resolver(nid);
		return;
	}	                        
	ircd_helper_write(dns_helper, "%c %x %d %s", type, nid, aftype, addr);
}

void
init_resolver(void)
{
	if(start_resolver())
	{
		ilog(L_MAIN, "Unable to fork resolver: %s", strerror(errno));		
		exit(0);
	}
}


static void 
restart_resolver_cb(ircd_helper *helper)
{
	if(helper != NULL) 
	{
		ircd_helper_close(helper);	
		dns_helper = NULL;
	}
	start_resolver();
}

void
restart_resolver(void)
{
	restart_resolver_cb(dns_helper);
}
