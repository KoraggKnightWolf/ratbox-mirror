/*
 *  ircd-ratbox: A slightly useful ircd.
 *  s_serv.c: Server related functions.
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

#include "stdinc.h"

#ifdef HAVE_LIBCRYPTO
#include <openssl/rsa.h>
#endif

#include "struct.h"
#include "s_serv.h"
#include "class.h"
#include "hash.h"
#include "match.h"
#include "ircd.h"
#include "numeric.h"
#include "packet.h"
#include "s_conf.h"
#include "s_newconf.h"
#include "s_log.h"
#include "s_stats.h"
#include "s_user.h"
#include "scache.h"
#include "send.h"
#include "client.h"
#include "channel.h"		/* chcap_usage_counts stuff... */
#include "hook.h"
#include "parse.h"

extern char *crypt();

#define MIN_CONN_FREQ 300

#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned int) 0xffffffff)
#endif

int MaxConnectionCount = 1;
int MaxClientCount = 1;
int refresh_user_links = 0;

static char buf[BUFSIZE];

static SlinkRplHnd slink_error;
static SlinkRplHnd slink_zipstats;
/*
 * list of recognized server capabilities.  "TS" is not on the list
 * because all servers that we talk to already do TS, and the kludged
 * extra argument to "PASS" takes care of checking that.  -orabidoo
 */
struct Capability captab[] = {
/*  name     cap     */
	{ "QS",		CAP_QS },
	{ "EX",		CAP_EX },
	{ "CHW",	CAP_CHW},
	{ "IE", 	CAP_IE},
	{ "GLN",	CAP_GLN},
	{ "KNOCK",	CAP_KNOCK},
	{ "ZIP",	CAP_ZIP},
	{ "TB",		CAP_TB},
	{ "ENCAP",	CAP_ENCAP },
#ifdef ENABLE_SERVICES
	{ "SERVICES",	CAP_SERVICE },
	{ "RSFNC",	CAP_RSFNC },
#endif
	{0, 0}
};

struct SlinkRplDef slinkrpltab[] = {
	{SLINKRPL_ERROR, slink_error, SLINKRPL_FLAG_DATA},
	{SLINKRPL_ZIPSTATS, slink_zipstats, SLINKRPL_FLAG_DATA},
	{0, 0, 0},
};

static CNCB serv_connect_callback;

void
slink_error(unsigned int UNUSED(rpl), unsigned int len, unsigned char *data, struct Client *server_p)
{
	s_assert(rpl == SLINKRPL_ERROR);

	s_assert(len < 256);
	data[len - 1] = '\0';

	sendto_realops_flags(UMODE_ALL, L_ALL, "SlinkError for %s: %s", server_p->name, data);
	exit_client(server_p, server_p, &me, "servlink error -- terminating link");
}

void
slink_zipstats(unsigned int UNUSED(rpl), unsigned int UNUSED(len), unsigned char *data, struct Client *server_p)
{
	struct ZipStats zipstats;
	u_int32_t in = 0, in_wire = 0, out = 0, out_wire = 0;
	int i = 0;

	s_assert(rpl == SLINKRPL_ZIPSTATS);
	s_assert(len == 16);
	s_assert(IsCapable(server_p, CAP_ZIP));

	/* Yes, it needs to be done this way, no we cannot let the compiler
	 * work with the pointer to the structure.  This works around a GCC
	 * bug on SPARC that affects all versions at the time of this writing.
	 * I will feed you to the creatures living in RMS's beard if you do
	 * not leave this as is, without being sure that you are not causing
	 * regression for most of our installed SPARC base.
	 * -jmallett, 04/27/2002
	 */
	memcpy(&zipstats, &server_p->localClient->zipstats, sizeof(struct ZipStats));

	in |= (data[i++] << 24);
	in |= (data[i++] << 16);
	in |= (data[i++] << 8);
	in |= (data[i++]);

	in_wire |= (data[i++] << 24);
	in_wire |= (data[i++] << 16);
	in_wire |= (data[i++] << 8);
	in_wire |= (data[i++]);

	out |= (data[i++] << 24);
	out |= (data[i++] << 16);
	out |= (data[i++] << 8);
	out |= (data[i++]);

	out_wire |= (data[i++] << 24);
	out_wire |= (data[i++] << 16);
	out_wire |= (data[i++] << 8);
	out_wire |= (data[i++]);

	zipstats.in += in;
	zipstats.inK += zipstats.in >> 10;
	zipstats.in &= 0x03ff;

	zipstats.in_wire += in_wire;
	zipstats.inK_wire += zipstats.in_wire >> 10;
	zipstats.in_wire &= 0x03ff;

	zipstats.out += out;
	zipstats.outK += zipstats.out >> 10;
	zipstats.out &= 0x03ff;

	zipstats.out_wire += out_wire;
	zipstats.outK_wire += zipstats.out_wire >> 10;
	zipstats.out_wire &= 0x03ff;

	if(zipstats.inK > 0)
		zipstats.in_ratio =
			(((double) (zipstats.inK - zipstats.inK_wire) /
			  (double) zipstats.inK) * 100.00);
	else
		zipstats.in_ratio = 0;

	if(zipstats.outK > 0)
		zipstats.out_ratio =
			(((double) (zipstats.outK - zipstats.outK_wire) /
			  (double) zipstats.outK) * 100.00);
	else
		zipstats.out_ratio = 0;

	memcpy(&server_p->localClient->zipstats, &zipstats, sizeof(struct ZipStats));
}

void
collect_zipstats(void * UNUSED(unused))
{
	dlink_node *ptr;
	struct Client *target_p;

	DLINK_FOREACH(ptr, serv_list.head)
	{
		target_p = ptr->data;
		if(IsCapable(target_p, CAP_ZIP))
		{
			/* only bother if we haven't already got something queued... */
			if(!target_p->localClient->slinkq)
			{
				target_p->localClient->slinkq = ircd_malloc(1);	/* sigh.. */
				target_p->localClient->slinkq[0] = SLINKCMD_ZIPSTATS;
				target_p->localClient->slinkq_ofs = 0;
				target_p->localClient->slinkq_len = 1;
				send_queued_slink_write(target_p->localClient->ctrlfd, target_p);
			}
		}
	}
}

/*
 * hunt_server - Do the basic thing in delivering the message (command)
 *      across the relays to the specific server (server) for
 *      actions.
 *
 *      Note:   The command is a format string and *MUST* be
 *              of prefixed style (e.g. ":%s COMMAND %s ...").
 *              Command can have only max 8 parameters.
 *
 *      server  parv[server] is the parameter identifying the
 *              target server.
 *
 *      *WARNING*
 *              parv[server] is replaced with the pointer to the
 *              real servername from the matched client (I'm lazy
 *              now --msa).
 *
 *      returns: (see #defines)
 */
int
hunt_server(struct Client * UNUSED(client_p), struct Client *source_p,
	    const char *command, int server, int parc, const char *parv[])
{
	struct Client *target_p;
	int wilds;
	dlink_node *ptr;
	const char *old;
	char *new;

	/*
	 * Assume it's me, if no server
	 */
	if(parc <= server || EmptyString(parv[server]) ||
	   match(me.name, parv[server]) || match(parv[server], me.name) ||
	   (strcmp(parv[server], me.id) == 0))
		return (HUNTED_ISME);
	
	new = LOCAL_COPY(parv[server]);

	/*
	 * These are to pickup matches that would cause the following
	 * message to go in the wrong direction while doing quick fast
	 * non-matching lookups.
	 */
	if(MyClient(source_p))
		target_p = find_named_client(new);
	else
		target_p = find_client(new);

	if(target_p)
		if(target_p->from == source_p->from && !MyConnect(target_p))
			target_p = NULL;

	if(target_p == NULL && (target_p = find_server(source_p, new)))
		if(target_p->from == source_p->from && !MyConnect(target_p))
			target_p = NULL;

	collapse(new);

	wilds = (strpbrk(new, "?*") != NULL);
	/*
	 * Again, if there are no wild cards involved in the server
	 * name, use the hash lookup
	 */
	if(!target_p)
	{
		if(!wilds)
		{
			if(MyClient(source_p) || !IsDigit(parv[server][0]))
				sendto_one_numeric(source_p, POP_QUEUE, ERR_NOSUCHSERVER,
						   form_str(ERR_NOSUCHSERVER),
						   parv[server]);
			return (HUNTED_NOSUCH);
		}
		else
		{
			target_p = NULL;

			DLINK_FOREACH(ptr, global_client_list.head)
			{
				if(match(new, ((struct Client *) (ptr->data))->name))
				{
					target_p = ptr->data;
					break;
				}
			}
		}
	}

	if(target_p)
	{
		if(!IsRegistered(target_p))
		{
			sendto_one_numeric(source_p, POP_QUEUE, ERR_NOSUCHSERVER,
					   form_str(ERR_NOSUCHSERVER),
					   parv[server]);
			return HUNTED_NOSUCH;
		}

		if(IsMe(target_p) || MyClient(target_p))
			return HUNTED_ISME;

		old = parv[server];
		parv[server] = get_id(target_p, target_p);

		sendto_one(target_p, POP_QUEUE, command, get_id(source_p, target_p),
			   parv[1], parv[2], parv[3], parv[4], parv[5], parv[6], parv[7], parv[8]);
		parv[server] = old;
		return (HUNTED_PASS);
	}

	if(!IsDigit(parv[server][0]))
		sendto_one_numeric(source_p, POP_QUEUE, ERR_NOSUCHSERVER,
				   form_str(ERR_NOSUCHSERVER), parv[server]);
	return (HUNTED_NOSUCH);
}

/*
 * try_connections - scan through configuration and try new connections.
 * Returns the calendar time when the next call to this
 * function should be made latest. (No harm done if this
 * is called earlier or later...)
 */
void
try_connections(void * UNUSED(unused))
{
	struct Client *client_p;
	struct server_conf *server_p = NULL;
	struct server_conf *tmp_p;
	struct Class *cltmp;
	dlink_node *ptr;
	int connecting = FALSE;
	int confrq;
	time_t next = 0;

	DLINK_FOREACH(ptr, server_conf_list.head)
	{
		tmp_p = ptr->data;

		if(ServerConfIllegal(tmp_p) || !ServerConfAutoconn(tmp_p))
			continue;

		cltmp = tmp_p->class;

		/*
		 * Skip this entry if the use of it is still on hold until
		 * future. Otherwise handle this entry (and set it on hold
		 * until next time). Will reset only hold times, if already
		 * made one successfull connection... [this algorithm is
		 * a bit fuzzy... -- msa >;) ]
		 */
		if(tmp_p->hold > ircd_currenttime)
		{
			if(next > tmp_p->hold || next == 0)
				next = tmp_p->hold;
			continue;
		}

		if((confrq = get_con_freq(cltmp)) < MIN_CONN_FREQ)
			confrq = MIN_CONN_FREQ;

		tmp_p->hold = ircd_currenttime + confrq;
		/*
		 * Found a CONNECT config with port specified, scan clients
		 * and see if this server is already connected?
		 */
		client_p = find_server(NULL, tmp_p->name);

		if(!client_p && (CurrUsers(cltmp) < MaxUsers(cltmp)) && !connecting)
		{
			server_p = tmp_p;

			/* We connect only one at time... */
			connecting = TRUE;
		}

		if((next > tmp_p->hold) || (next == 0))
			next = tmp_p->hold;
	}

	/* TODO: change this to set active flag to 0 when added to event! --Habeeb */
	if(GlobalSetOptions.autoconn == 0)
		return;

	if(!connecting)
		return;

	/* move this connect entry to end.. */
	ircd_dlinkDelete(&server_p->node, &server_conf_list);
	ircd_dlinkAddTail(server_p, &server_p->node, &server_conf_list);

	/*
	 * We used to only print this if serv_connect() actually
	 * suceeded, but since ircd_tcp_connect() can call the callback
	 * immediately if there is an error, we were getting error messages
	 * in the wrong order. SO, we just print out the activated line,
	 * and let serv_connect() / serv_connect_callback() print an
	 * error afterwards if it fails.
	 *   -- adrian
	 */
#ifndef HIDE_SERVERS_IPS
	sendto_realops_flags(UMODE_ALL, L_ALL,
			"Connection to %s[%s] activated.",
			server_p->name, server_p->host);
#else
	sendto_realops_flags(UMODE_ALL, L_ALL,
			"Connection to %s activated",
			server_p->name);
#endif

	serv_connect(server_p, 0);
}

/*
 * send_capabilities
 *
 * inputs	- Client pointer to send to
 *		- int flag of capabilities that this server has
 * output	- NONE
 * side effects	- send the CAPAB line to a server  -orabidoo
 *
 */
void
send_capabilities(struct Client *client_p, int cap_can_send)
{
	struct Capability *cap;
	char msgbuf[BUFSIZE];
	char *t;
	int tl;

	t = msgbuf;

	for (cap = captab; cap->name; ++cap)
	{
		if(cap->cap & cap_can_send)
		{
			tl = ircd_sprintf(t, "%s ", cap->name);
			t += tl;
		}
	}

	t--;
	*t = '\0';

	sendto_one(client_p, POP_QUEUE, "CAPAB :%s", msgbuf);
}

/*
 * show_capabilities - show current server capabilities
 *
 * inputs       - pointer to an struct Client
 * output       - pointer to static string
 * side effects - build up string representing capabilities of server listed
 */
const char *
show_capabilities(struct Client *target_p)
{
	static char msgbuf[BUFSIZE];
	struct Capability *cap;
	char *t;
	int tl;

	t = msgbuf;
	tl = ircd_sprintf(msgbuf, "TS ");
	t += tl;

	if(!IsServer(target_p) || !target_p->serv->caps)	/* short circuit if no caps */
	{
		msgbuf[2] = '\0';
		return msgbuf;
	}

	for (cap = captab; cap->cap; ++cap)
	{
		if(cap->cap & target_p->serv->caps)
		{
			tl = ircd_sprintf(t, "%s ", cap->name);
			t += tl;
		}
	}

	t--;
	*t = '\0';

	return msgbuf;
}

/*
 * New server connection code
 * Based upon the stuff floating about in s_bsd.c
 *   -- adrian
 */

/*
 * serv_connect() - initiate a server connection
 *
 * inputs	- pointer to conf 
 *		- pointer to client doing the connet
 * output	-
 * side effects	-
 *
 * This code initiates a connection to a server. It first checks to make
 * sure the given server exists. If this is the case, it creates a socket,
 * creates a client, saves the socket information in the client, and
 * initiates a connection to the server through ircd_connect_tcp(). The
 * completion of this goes through serv_completed_connection().
 *
 * We return 1 if the connection is attempted, since we don't know whether
 * it suceeded or not, and 0 if it fails in here somewhere.
 */
int
serv_connect(struct server_conf *server_p, struct Client *by)
{
	struct Client *client_p;
	struct irc_sockaddr_storage myipnum; 
	int fd;

	s_assert(server_p != NULL);
	if(server_p == NULL)
		return 0;

	/* log */
	ircd_inet_ntop_sock((struct sockaddr *)&server_p->ipnum, buf, sizeof(buf));
	ilog(L_SERVER, "Connect to *[%s] @%s", server_p->name, buf);

	/*
	 * Make sure this server isn't already connected
	 */
	if((client_p = find_server(NULL, server_p->name)))
	{
		sendto_realops_flags(UMODE_ALL, L_ALL,
				     "Server %s already present from %s",
				     server_p->name, get_server_name(client_p, SHOW_IP));
		if(by && IsPerson(by) && !MyClient(by))
			sendto_one_notice(by, POP_QUEUE, ":Server %s already present from %s",
					  server_p->name, get_server_name(client_p, SHOW_IP));
		return 0;
	}

	/* create a socket for the server connection */
	if((fd = ircd_socket(server_p->ipnum.ss_family, SOCK_STREAM, 0, NULL)) < 0)
	{
		/* Eek, failure to create the socket */
		report_error("opening stream socket to %s: %s", 
			     server_p->name, server_p->name, errno);
		return 0;
	}

	/* servernames are always guaranteed under HOSTLEN chars */
	ircd_note(fd, "Server: %s", server_p->name);

	/* Create a local client */
	client_p = make_client(NULL);

	/* Copy in the server, hostname, fd */
	strlcpy(client_p->name, server_p->name, sizeof(client_p->name));
	strlcpy(client_p->host, server_p->host, sizeof(client_p->host));
	strlcpy(client_p->sockhost, buf, sizeof(client_p->sockhost));
	client_p->localClient->fd = fd;

	/* shove the port number into the sockaddr */
#ifdef IPV6
	if(server_p->ipnum.ss_family == AF_INET6)
		((struct sockaddr_in6 *)&server_p->ipnum)->sin6_port = htons(server_p->port);
	else
#endif
		((struct sockaddr_in *)&server_p->ipnum)->sin_port = htons(server_p->port);

	/*
	 * Set up the initial server evilness, ripped straight from
	 * connect_server(), so don't blame me for it being evil.
	 *   -- adrian
	 */

	if(!ircd_set_buffers(client_p->localClient->fd, READBUF_SIZE))
	{
		report_error("ircd_set_buffers failed for server %s:%s",
				get_server_name(client_p, SHOW_IP),
				log_client_name(client_p, SHOW_IP),
				errno);
	}

	/*
	 * Attach config entries to client here rather than in
	 * serv_connect_callback(). This to avoid null pointer references.
	 */
	attach_server_conf(client_p, server_p);

	/*
	 * at this point we have a connection in progress and C/N lines
	 * attached to the client, the socket info should be saved in the
	 * client and it should either be resolved or have a valid address.
	 *
	 * The socket has been connected or connect is in progress.
	 */
	make_server(client_p);
	if(by && IsPerson(by))
	{
		strcpy(client_p->serv->by, by->name);
		if(client_p->serv->user)
			free_user(client_p->serv->user, NULL);
		client_p->serv->user = by->user;
		by->user->refcnt++;
	}
	else
	{
		strcpy(client_p->serv->by, "AutoConn.");
		if(client_p->serv->user)
			free_user(client_p->serv->user, NULL);
		client_p->serv->user = NULL;
	}
	client_p->serv->up = me.name;
	client_p->serv->upid = me.id;
	SetConnecting(client_p);
	ircd_dlinkAddTail(client_p, &client_p->node, &global_client_list);

	if(ServerConfVhosted(server_p))
	{
		memcpy(&myipnum, &server_p->my_ipnum, sizeof(myipnum));
		((struct sockaddr_in *)&myipnum)->sin_port = 0;
		myipnum.ss_family = server_p->my_ipnum.ss_family;
				
	}
	else if(server_p->ipnum.ss_family == AF_INET && ServerInfo.specific_ipv4_vhost)
	{
		memcpy(&myipnum, &ServerInfo.ip, sizeof(myipnum));
		((struct sockaddr_in *)&myipnum)->sin_port = 0;
		myipnum.ss_family = AF_INET;
		SET_SS_LEN(myipnum, sizeof(struct sockaddr_in));
	}
	
#ifdef IPV6
	else if((server_p->ipnum.ss_family == AF_INET6) && ServerInfo.specific_ipv6_vhost)
	{
		memcpy(&myipnum, &ServerInfo.ip6, sizeof(myipnum));
		((struct sockaddr_in6 *)&myipnum)->sin6_port = 0;
		myipnum.ss_family = AF_INET6;
		SET_SS_LEN(myipnum, sizeof(struct sockaddr_in6));
	}
#endif
	else
	{
		ircd_connect_tcp(client_p->localClient->fd, (struct sockaddr *)&server_p->ipnum,
				 NULL, 0, serv_connect_callback, 
				 client_p, ConfigFileEntry.connect_timeout);
		 return 1;
	}

	ircd_connect_tcp(client_p->localClient->fd, (struct sockaddr *)&server_p->ipnum,
			 (struct sockaddr *) &myipnum,
			 GET_SS_LEN(myipnum), serv_connect_callback, client_p,
			 ConfigFileEntry.connect_timeout);

	return 1;
}

/*
 * serv_connect_callback() - complete a server connection.
 * 
 * This routine is called after the server connection attempt has
 * completed. If unsucessful, an error is sent to ops and the client
 * is closed. If sucessful, it goes through the initialisation/check
 * procedures, the capabilities are sent, and the socket is then
 * marked for reading.
 */
static void
serv_connect_callback(int fd, int status, void *data)
{
	struct Client *client_p = data;
	struct server_conf *server_p;
	/* First, make sure its a real client! */
	s_assert(client_p != NULL);
	s_assert(client_p->localClient->fd == fd);

	if(client_p == NULL)
		return;

	/* while we were waiting for the callback, its possible this already
	 * linked in.. --fl
	 */
	if(find_server(NULL, client_p->name) != NULL)
	{
		exit_client(client_p, client_p, &me, "Server Exists");
		return;
	}

	ircd_connect_sockaddr(fd, (struct sockaddr *)&client_p->localClient->ip, sizeof(client_p->localClient->ip));
	
	/* Check the status */
	if(status != IRCD_OK)
	{
		/* IRCD_ERR_TIMEOUT wont have an errno associated with it,
		 * the others will.. --fl
		 */
		if(status == IRCD_ERR_TIMEOUT)
			sendto_realops_flags(UMODE_ALL, L_ALL,
					"Error connecting to %s[%s]: %s",
					client_p->name, 
#ifdef HIDE_SERVERS_IPS
					"255.255.255.255",
#else
					client_p->host,
#endif
					ircd_errstr(status));
		else
			sendto_realops_flags(UMODE_ALL, L_ALL,
					"Error connecting to %s[%s]: %s (%s)",
					client_p->name,
#ifdef HIDE_SERVERS_IPS
					"255.255.255.255",
#else
					client_p->host,
#endif
					ircd_errstr(status), strerror(errno));

		exit_client(client_p, client_p, &me, ircd_errstr(status));
		return;
	}

	/* IRCD_OK, so continue the connection procedure */
	/* Get the C/N lines */
	if((server_p = client_p->localClient->att_sconf) == NULL)
	{
		sendto_realops_flags(UMODE_ALL, L_ALL, "Lost connect{} block for %s",
				get_server_name(client_p, HIDE_IP));
		exit_client(client_p, client_p, &me, "Lost connect{} block");
		return;
	}

	/* Next, send the initial handshake */
	SetHandshake(client_p);

	/* kludge, if we're not using TS6, dont ever send
	 * ourselves as being TS6 capable.
	 */
	if(!EmptyString(server_p->spasswd))
	{
		if(ServerInfo.use_ts6)
			sendto_one(client_p, POP_QUEUE, "PASS %s TS %d :%s", 
				   server_p->spasswd, TS_CURRENT, me.id);
		else
			sendto_one(client_p, POP_QUEUE, "PASS %s :TS",
				   server_p->spasswd);
	}

	/* pass my info to the new server */
	send_capabilities(client_p, default_server_capabs
			  | (ServerConfCompressed(server_p) ? CAP_ZIP_SUPPORTED : 0)
			  | (ServerConfTb(server_p) ? CAP_TB : 0));

	sendto_one(client_p, POP_QUEUE, "SERVER %s 1 :%s%s",
		   me.name,
		   ConfigServerHide.hidden ? "(H) " : "", me.info);

	/* 
	 * If we've been marked dead because a send failed, just exit
	 * here now and save everyone the trouble of us ever existing.
	 */
	if(IsAnyDead(client_p))
	{
		sendto_realops_flags(UMODE_ALL, L_ADMIN,
				     "%s[%s] went dead during handshake",
				     client_p->name, client_p->host);
		sendto_realops_flags(UMODE_ALL, L_OPER,
				     "%s went dead during handshake", client_p->name);
		exit_client(client_p, client_p, &me, "Went dead during handshake");
		return;
	}

	/* don't move to serv_list yet -- we haven't sent a burst! */

	/* If we get here, we're ok, so lets start reading some data */
	read_packet(fd, client_p);
}

