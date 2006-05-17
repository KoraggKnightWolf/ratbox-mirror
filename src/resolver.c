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

static ircd_helper *res_helper;

static void dns_readable(int fd, void *ptr);
static void dns_writeable(int fd, void *ptr);
static void process_adns_incoming(void);

static char readBuf[READBUF_SIZE];
static void resolve_ip(char **parv);
static void resolve_host(char **parv);


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

static adns_state dns_state;

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
			ircd_setselect(fd, IRCD_SELECT_READ, dns_readable, NULL);
		if(pollfds[i].events & ADNS_POLLOUT)
			ircd_setselect(fd, IRCD_SELECT_WRITE,
				       dns_writeable, NULL);
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
#ifndef _WIN32
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
error_cb(ircd_helper *helper)
{
	exit(1);
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
parse_request(ircd_helper *helper)
{
	int len;  
	static char *parv[MAXPARA + 1];
	int parc;  
	while((len = ircd_linebuf_get(&helper->recvq, readBuf, sizeof(readBuf),
				 LINEBUF_COMPLETE, LINEBUF_PARSED)) > 0)
	{
		parc = ircd_string_to_array(readBuf, parv, MAXPARA);
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
						ircd_inet_ntop(AF_INET6, &reply->rrs.addr->addr.inet6.sin6_addr, tmpres, sizeof(tmpres)-1);
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
						ircd_inet_ntop(AF_INET, &reply->rrs.addr->addr.inet.sin_addr, response, sizeof(response));
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
			ircd_free(reply);
			if(result != 0)
			{
				ircd_helper_write(res_helper, "%s 0 FAILED", req->reqid);
				ircd_free(reply);
				ircd_free(req);

			}							
			return;
		}
#endif
		strcpy(response, "FAILED");
		result = 0;
	}
	ircd_helper_write(res_helper, "%s %d %d %s\n", req->reqid, result, aftype, response);
	ircd_free(reply);
	ircd_free(req);
}


static void process_adns_incoming(void)
{
	adns_query q, r;
	adns_answer *answer;
	struct dns_request *req;
		
	adns_forallqueries_begin(dns_state);
	while(   (q = adns_forallqueries_next(dns_state, (void *)&r)) != NULL)
	{
		switch(adns_check(dns_state, &q, &answer, (void *)&req))
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
	ircd_helper_read(res_helper->ifd, res_helper);
	while(1)
	{
		dns_select();
		ircd_event_run();
		ircd_select(1000);
	}
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
	req = ircd_malloc(sizeof(struct dns_request));
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
	req = ircd_malloc(sizeof(struct dns_request));
	req->revfwd = REQREV;
	strcpy(req->reqid, requestid);
	switch(*iptype)
	{
		case '4':
			flags = adns_r_ptr;
			req->reqtype = REVIPV4;
			if(!ircd_inet_pton(AF_INET, rec, &req->sins.in.sin_addr))
				exit(6);
			req->sins.in.sin_family = AF_INET;

			break;
#ifdef IPV6
		case '5': /* This is the case of having to fall back to "ip6.int" */
			req->reqtype = REVIPV6FALLBACK;
			flags = adns_r_ptr_ip6;
			if(!ircd_inet_pton(AF_INET6, rec, &req->sins.in6.sin6_addr))
				exit(6);
			req->sins.in6.sin6_family = AF_INET6;
			req->fallback = 0;
			break;
		case '6':
			req->reqtype = REVIPV6;
			flags = adns_r_ptr_ip6;
			if(!ircd_inet_pton(AF_INET6, rec, &req->sins.in6.sin6_addr))
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
	res_helper = ircd_helper_child(parse_request, error_cb, NULL, NULL, NULL, 256, 1024, 256); /* XXX fix me */

	if(res_helper == NULL)
	{
		fprintf(stderr, "This is ircd-ratbox resolver.  You know you aren't supposed to run me directly?\n");
		fprintf(stderr, "You get an Id tag for this: $Id$\n");
		fprintf(stderr, "Have a nice life\n");
		exit(1);
	}

	adns_init(&dns_state, adns_if_noautosys, 0);
	setup_signals();
	read_io();	
	return 1;
}


