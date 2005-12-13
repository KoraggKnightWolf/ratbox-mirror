/*
 *  ircd-ratbox: A slightly useful ircd.
 *  struct.h: Contains various structures used throughout ircd
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
#ifndef INCLUDED_struct_h
#define INCLUDED_struct_h

#include "ircd_lib.h"

/*** client.h ***/

struct User
{
	dlink_list channel;	/* chain of channel pointer blocks */
	dlink_list invited;	/* chain of invite pointer blocks */
	char *away;		/* pointer to away message */
	char name[NICKLEN];
#ifdef ENABLE_SERVICES
	char suser[NICKLEN+1];
#endif

};

struct Server
{
	const char *name;
	char by[NICKLEN];
	dlink_list servers;
	dlink_list users;
	int caps;		/* capabilities bit-field */
	char *fullcaps;
};

struct ZipStats
{
	uint32_t in;
	uint32_t in_wire;
	uint32_t out;
	uint32_t out_wire;
	uint32_t inK;
	uint32_t inK_wire;
	uint32_t outK;
	uint32_t outK_wire;
	double in_ratio;
	double out_ratio;
};

struct servlink_data
{
	int command;
	int datalen;
	int gotdatalen;
	int readdata;
	unsigned char *data;
	unsigned char *slinkq;	/* sendq for control data */
	int slinkq_ofs;		/* ofset into slinkq */
	int slinkq_len;		/* length remaining after slinkq_ofs */
	int ctrlfd;		/* For servers:
				   control fd used for sending commands
				   to servlink */

	struct ZipStats zipstats;
};


struct Client
{
	dlink_node node;
	dlink_node lnode;
	struct User *user;	/* ...defined, if this is a User */
	struct Server *serv;	/* ...defined, if this is a server */
	struct Client *servptr;	/* Points to server this Client is on */
	struct Client *from;	/* == self, if Local Client, *NEVER* NULL! */

	struct Whowas *whowas;	/* Pointers to whowas structs */
	time_t tsinfo;		/* TS on the nick, SVINFO on server */
	unsigned int umodes;	/* opers, normal users subset */
	uint32_t flags;	/* client flags */
	uint32_t operflags;	/* ugh. overflow */

	int hopcount;		/* number of servers to this 0 = local */
	unsigned short status;	/* Client type */
	unsigned char handler;	/* Handler index */

	/* client->name is the unique name for a client nick or host */
	const char *name;

	/* 
	 * client->username is the username from ident or the USER message, 
	 * If the client is idented the USER message is ignored, otherwise 
	 * the username part of the USER message is put here prefixed with a 
	 * tilde depending on the I:line, Once a client has registered, this
	 * field should be considered read-only.
	 */
	char username[USERLEN + 1];	/* client's username */
	/*
	 * client->host contains the resolved name or ip address
	 * as a string for the user, it may be fiddled with for oper spoofing etc.
	 * once it's changed the *real* address goes away. This should be
	 * considered a read-only field after the client has registered.
	 */
	char host[HOSTLEN + 1];	/* client's hostname */
	char sockhost[HOSTIPLEN + 1]; /* clients ip */
	char info[REALLEN + 1];	/* Free form additional client info */

	char id[IDLEN + 1];	/* UID/SID, unique on the network */

	/* list of who has this client on their allow list, its counterpart
	 * is in LocalUser
	 */
	dlink_list on_allow_list;


	struct LocalUser *localClient;
};

struct LocalUser
{
	dlink_node tnode;	/* This is the node for the local list type the client is on*/
	/*
	 * The following fields are allocated only for local clients
	 * (directly connected to *this* server with a socket.
	 */
	/* Anti flooding part, all because of lamers... */
	time_t last_join_time;	/* when this client last 
				   joined a channel */
	time_t last_leave_time;	/* when this client last 
				 * left a channel */
	int join_leave_count;	/* count of JOIN/LEAVE in less than 
				   MIN_JOIN_LEAVE_TIME seconds */
	int oper_warn_count_down;	/* warn opers of this possible 
					   spambot every time this gets to 0 */
	time_t last_caller_id_time;
	time_t first_received_message_time;
	int received_number_of_privmsgs;
	int flood_noticed;

	time_t lasttime;	/* last time we parsed something */
	time_t firsttime;	/* time client was created */

	unsigned long serial;	/* used to enforce 1 send per nick */

	/* Send and receive linebuf queues .. */
	buf_head_t buf_sendq;
	buf_head_t buf_recvq;

	uint32_t sendM;	/* Statistics: protocol messages send */
	uint32_t sendK;	/* Statistics: total k-bytes send */
	uint32_t receiveM;	/* Statistics: protocol messages received */
	uint32_t receiveK;	/* Statistics: total k-bytes received */
	uint16_t sendB;	/* counters to count upto 1-k lots of bytes */
	uint16_t receiveB;	/* sent and received. */
	struct Listener *listener;	/* listener accepted from */
	struct ConfItem *att_conf;	/* attached conf */
	struct server_conf *att_sconf;

	struct irc_sockaddr_storage ip;
	time_t last_nick_change;
	int number_of_nick_changes;

	/*
	 * XXX - there is no reason to save this, it should be checked when it's
	 * received and not stored, this is not used after registration
	 */
	char *passwd;
	char *opername;
	char *fullcaps;

	int caps;		/* capabilities bit-field */
	int fd;			/* >= 0, for local clients */


	struct servlink_data *slink;	/* slink reply being parsed */

	time_t last;

	/* challenge stuff */
	time_t chal_time;
	
	/* clients allowed to talk through +g */
	dlink_list allow_list;

	/* nicknames theyre monitoring */
	dlink_list monitor_list;

	/*
	 * Anti-flood stuff. We track how many messages were parsed and how
	 * many we were allowed in the current second, and apply a simple decay
	 * to avoid flooding.
	 *   -- adrian
	 */
	int allow_read;		/* how many we're allowed to read in this second */
	int actually_read;	/* how many we've actually read in this second */
	int sent_parsed;	/* how many messages we've parsed in this second */
	time_t last_knock;	/* time of last knock */
	uint32_t random_ping;
	struct AuthRequest	*auth_request;

	/* target change stuff */
	void *targets[10];		/* targets were aware of */
	u_int8_t targinfo[2];	/* cyclic array, no in use */
	time_t target_last;		/* last time we cleared a slot */
};


#endif
