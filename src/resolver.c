/*
 * resolver.c: dns resolving daemon for ircd-ratbox
 * Based on many things ripped from ratbox-services
 * and ircd-ratbox itself and who knows what else
 *
 * Copyright (C) 2003-2005 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2005 ircd-ratbox development team
 * Copyright (C) 2005 Aaron Sethman <androsyn@ratbox.org>
 *
 * $Id$
 */

#define READBUF_SIZE    16384

#include "ircd_lib.h"
#include "internal.h"

/* data fd from ircd */
int ifd = -1;
/* data to ircd */
int ofd = -1; 

#define MAXPARA 10
#define REQIDLEN 10

#define REQREV 0
#define REQFWD 1

#define REVIPV4 0
#define REVIPV6 1
#define REVIPV6INT 2
#define REVIPV6FALLBACK 3
#define FWDHOST 4

#define EmptyString(x) (!(x) || (*(x) == '\0'))

static void dns_readable(int fd, void *ptr);
static void dns_writeable(int fd, void *ptr);
static void process_adns_incoming(void);

static char readBuf[READBUF_SIZE];
static void resolve_ip(char **parv);
static void resolve_host(char **parv);
static int io_to_array(char *string, char **parv);

buf_head_t sendq;
buf_head_t recvq;

struct dns_request
{
	char reqid[REQIDLEN];
	int reqtype;
	int revfwd;
	adns_query query;
	union {
#ifdef IPV6
		struct sockaddr_in6 in6;
#endif
		struct sockaddr_in in;
	} sins;
#ifdef IPV6
	int fallback;
#endif
};

fd_set readfds;
fd_set writefds;
fd_set exceptfds;

adns_state dns_state;

/* void dns_select(void)
 * Input: None.
 * Output: None
 * Side effects: Re-register ADNS fds with the fd system. Also calls the
 *               callbacks into core ircd.
 */
static void
dns_select(void)
{   
	struct adns_pollfd pollfds[MAXFD_POLL];
	int npollfds, i, fd;
	npollfds = adns__pollfds(dns_state, pollfds);
	for (i = 0; i < npollfds; i++)
	{
		fd = pollfds[i].fd;
		if(pollfds[i].events & ADNS_POLLIN)
			comm_setselect(fd, COMM_SELECT_READ, dns_readable, NULL, 0);
		if(pollfds[i].events & ADNS_POLLOUT)
			comm_setselect(fd, COMM_SELECT_WRITE,
				       dns_writeable, NULL, 0);
	}
}

/* void dns_readable(int fd, void *ptr)
 * Input: An fd which has become readable, ptr not used.
 * Output: None.
 * Side effects: Read DNS responses from DNS servers.
 * Note: Called by the fd system.
 */
static void
dns_readable(int fd, void *ptr)
{
	adns_processreadable(dns_state, fd, ircd_current_time_tv());
	process_adns_incoming();
	dns_select();
}   

/* void dns_writeable(int fd, void *ptr)
 * Input: An fd which has become writeable, ptr not used.
 * Output: None.
 * Side effects: Write any queued buffers out.
 * Note: Called by the fd system.
 */
static void
dns_writeable(int fd, void *ptr)
{
	adns_processwriteable(dns_state, fd, ircd_current_time_tv() );
	process_adns_incoming();
	dns_select();
}

static void
restart_resolver(int sig)
{
	/* Rehash dns configuration */
	adns__rereadconfig(dns_state);
}

static void
setup_signals(void)
{
#ifndef __MINGW32__
	struct sigaction act;
	act.sa_flags = 0;
	act.sa_handler = SIG_IGN;
	sigemptyset(&act.sa_mask);
	act.sa_handler = restart_resolver;
	sigaddset(&act.sa_mask, SIGHUP);
	sigaction(SIGHUP, &act, 0);
#endif
}

static void
write_sendq(int fd, void *unused)
{
	int retlen;
	if(linebuf_len(&sendq) > 0)
	{
		while((retlen = linebuf_flush(fd, &sendq)) > 0);
		if(retlen == 0 || (retlen < 0 && !ignoreErrno(errno)))
		{
			exit(1);
		}
	}
	if(linebuf_len(&sendq) > 0)
		comm_setselect(fd, COMM_SELECT_WRITE, write_sendq, NULL, 0);
}

/*
request protocol:

INPUTS:

IPTYPE:    4, 5,  6, ipv4, ipv6.int/arpa, ipv6 respectively
requestid: identifier of the request
 

RESIP  requestid IPTYPE IP 
RESHST requestid IPTYPE hostname

OUTPUTS:
ERR error string = daemon failed and is going to shutdown
otherwise

FWD requestid PASS/FAIL hostname or reason for failure
REV requestid PASS/FAIL IP or reason

*/


static void
parse_request(void)
{
	int len;  
	static char *parv[MAXPARA + 1];
	int parc;  
	while((len = linebuf_get(&recvq, readBuf, sizeof(readBuf),
				 LINEBUF_COMPLETE, LINEBUF_PARSED)) > 0)
	{
		parc = io_to_array(readBuf, parv);
		if(parc != 4)
			exit(1);
		switch(*parv[0])
		{
			case 'I':
				resolve_ip(parv);
				break;
			case 'H':
				resolve_host(parv);
				break;
			default:
				break;
		}
	}
	

}

static void                       
read_request(int fd, void *unusued)
{
	int length;

	while((length = comm_read(fd, readBuf, sizeof(readBuf))) > 0)
	{
		linebuf_parse(&recvq, readBuf, length, 0);
		parse_request();
	}
	 
	if(length == 0)
		exit(1);

	if(length == -1 && !ignoreErrno(errno))
		exit(1);
	comm_setselect(fd, COMM_SELECT_READ, read_request, NULL, 0);
}


static void send_answer(struct dns_request *req, adns_answer *reply)
{
	char response[64];
	int result = 0;
	int aftype = 0;
	if(reply && reply->status == adns_s_ok)
	{
		switch(req->revfwd)
		{
			case REQREV:
			{
				if(strlen(*reply->rrs.str) < 63)
				{
					strcpy(response, *reply->rrs.str);
					result = 1;
				} else {
					strcpy(response, "HOSTTOOLONG");
					result = 0;
				}
				break;
			}

			case REQFWD:
			{
				switch(reply->type)
				{
#ifdef IPV6
					case adns_r_addr6:
					{
						char tmpres[65];
						inetntop(AF_INET6, &reply->rrs.addr->addr.inet6.sin6_addr, tmpres, sizeof(tmpres)-1);
						aftype = 6;
						if(*tmpres == ':')
						{
							strcpy(response, "0");
							strcat(response, tmpres);
						} else
							strcpy(response, tmpres);
						result = 1;
						break;
					}
#endif
					case adns_r_addr:
					{
						result = 1;
						aftype = 4;
						inetntop(AF_INET, &reply->rrs.addr->addr.inet.sin_addr, response, sizeof(response));
						break;
					} 
					default:
					{
						strcpy(response, "FAILED");
						result = 0;
						aftype = 0;
						break;
					}						
				}
				break;
			}
			default:
			{
				exit(1);
			}				
		}

	} 
	else
	{
#ifdef IPV6
		if(req->revfwd == REQREV && req->reqtype == REVIPV6FALLBACK && req->fallback == 0)
		{
			req->fallback = 1;
			result = adns_submit_reverse(dns_state,
				    (struct sockaddr *) &req->sins.in6,
				    adns_r_ptr_ip6_old,
				    adns_qf_owner | adns_qf_cname_loose |
				    adns_qf_quoteok_anshost, req, &req->query);
			MyFree(reply);
			if(result != 0)
			{
				linebuf_put(&sendq, "%s 0 FAILED", req->reqid);
				write_sendq(ofd, NULL);
				MyFree(reply);
				MyFree(req);

			}							
			return;
		}
#endif
		strcpy(response, "FAILED");
		result = 0;
	}
	linebuf_put(&sendq, "%s %d %d %s\n", req->reqid, result, aftype, response);
	write_sendq(ofd, NULL);
	MyFree(reply);
	MyFree(req);
}


static void process_adns_incoming(void)
{
	adns_query q, r;
	adns_answer *answer;
	struct dns_request *req;
		
	adns_forallqueries_begin(dns_state);
	while(   (q = adns_forallqueries_next(dns_state, (void *)&r)) != NULL)
	{
		switch(adns_check(dns_state, &q, &answer, (void **)&req))
		{
			case EAGAIN:
				continue;
			case 0:
				send_answer(req, answer);
				continue;
			default:
				if(answer != NULL && answer->status == adns_s_systemfail)
					exit(2);
				send_answer(req, NULL);
				break;
		}

	}
}


/* read_io()
 *   The main IO loop for reading/writing data.
 *
 * inputs	-
 * outputs	-
 */
static void
read_io(void)
{
	read_request(ifd, NULL);
	while(1)
	{
		dns_select();
		ircd_set_time();
		eventRun();
		comm_select(250);
	}
}


/* io_to_array()
 *   Changes a given buffer into an array of parameters.
 *   Taken from ircd-ratbox.
 *
 * inputs	- string to parse, array to put in
 * outputs	- number of parameters
 */
static int
io_to_array(char *string, char **parv)
{
	char *p, *buf = string;
	int x = 0;

	parv[x] = NULL;

	if(EmptyString(string))
		return x;

	while (*buf == ' ')	/* skip leading spaces */
		buf++;
	if(*buf == '\0')	/* ignore all-space args */
		return x;

	do
	{
		if(*buf == ':')	/* Last parameter */
		{
			buf++;
			parv[x++] = buf;
			parv[x] = NULL;
			return x;
		}
		else
		{
			parv[x++] = buf;
			parv[x] = NULL;
			if((p = strchr(buf, ' ')) != NULL)
			{
				*p++ = '\0';
				buf = p;
			}
			else
				return x;
		}
		while (*buf == ' ')
			buf++;
		if(*buf == '\0')
			return x;
	}
	while (x < MAXPARA - 1);

	if(*p == ':')
		p++;

	parv[x++] = p;
	parv[x] = NULL;
	return x;
}




static void
resolve_host(char **parv)
{
	struct dns_request *req;
	char *requestid = parv[1];
	char *iptype = parv[2];
	char *rec = parv[3];
	int result;
	int flags;
	req = MyMalloc(sizeof(struct dns_request));
	strcpy(req->reqid, requestid);

	req->revfwd = REQFWD;
	req->reqtype = FWDHOST; 
	switch(*iptype)
	{
#ifdef IPV6
		case '5': /* I'm not sure why somebody would pass a 5 here, but okay */
		case '6':
			flags = adns_r_addr6;
			break;
#endif
		default:
			flags = adns_r_addr;		
			break;
	}
	result = adns_submit(dns_state, rec, flags, adns_qf_owner, req, &req->query);
	if(result != 0)
	{
		/* Failed to even submit */
		send_answer(req, NULL);
	}

}


static void
resolve_ip(char **parv)
{
	char *requestid = parv[1];
	char *iptype = parv[2];
	char *rec = parv[3];			
	struct dns_request *req;

	int result; 
	int flags = adns_r_ptr;


	if(strlen(requestid) >= REQIDLEN)
	{
		exit(3);
	}
	req = MyMalloc(sizeof(struct dns_request));
	req->revfwd = REQREV;
	strcpy(req->reqid, requestid);
	switch(*iptype)
	{
		case '4':
			flags = adns_r_ptr;
			req->reqtype = REVIPV4;
			if(!inetpton(AF_INET, rec, &req->sins.in.sin_addr))
				exit(6);
			req->sins.in.sin_family = AF_INET;

			break;
#ifdef IPV6
		case '5': /* This is the case of having to fall back to "ip6.int" */
			req->reqtype = REVIPV6FALLBACK;
			flags = adns_r_ptr_ip6;
			if(!inetpton(AF_INET6, rec, &req->sins.in6.sin6_addr))
				exit(6);
			req->sins.in6.sin6_family = AF_INET6;
			req->fallback = 0;
			break;
		case '6':
			req->reqtype = REVIPV6;
			flags = adns_r_ptr_ip6;
			if(!inetpton(AF_INET6, rec, &req->sins.in6.sin6_addr))
				exit(6);
			req->sins.in6.sin6_family = AF_INET6;
			break;
#endif
		default:
			exit(7);
	}

	result = adns_submit_reverse(dns_state,
				    (struct sockaddr *) &req->sins,
				    flags,
				    adns_qf_owner | adns_qf_cname_loose |
				    adns_qf_quoteok_anshost, req, &req->query);
		
	if(result != 0)
	{
		send_answer(req, NULL);		
	}
	
}


int main(int argc, char **argv)
{
	int i, x, maxfd;
	char *tifd;
	char *tofd;
	char *tmaxfd;
	
	tifd = getenv("IFD");
	tofd = getenv("OFD");
	tmaxfd = getenv("MAXFD");
	if(tifd == NULL || tofd == NULL || tmaxfd == NULL)
	{
		fprintf(stderr, "This is ircd-ratbox resolver.  You know you aren't supposed to run me directly?\n");
		fprintf(stderr, "You get an Id tag for this: $Id$\n");
		fprintf(stderr, "Have a nice life\n");
		exit(1);
	}
	ifd = (int)strtol(tifd, NULL, 10);
	ofd = (int)strtol(tofd, NULL, 10);
	maxfd = (int)strtol(tmaxfd, NULL, 10);

#ifndef __MINGW32__
	for(x = 0; x < maxfd; x++)
	{
		if(x != ifd && x != ofd)
			close(x);
	}
#endif
	ircd_lib(NULL, NULL, NULL, 0, 256, 1024, 256); /* XXX fix me */

	linebuf_newbuf(&sendq);
	linebuf_newbuf(&recvq);

	comm_open(ifd, FD_SOCKET, "incoming pipe");
	comm_open(ofd, FD_SOCKET, "outgoing pipe");
	comm_set_nb(ifd);
	comm_set_nb(ofd);
	adns_init(&dns_state, adns_if_noautosys, 0);
	setup_signals();
	read_io();	
	return 1;
}


