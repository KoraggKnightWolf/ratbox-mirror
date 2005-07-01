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
#include <stdlib.h>    
#include <stdio.h>     
#include <string.h>    
#include <signal.h>    
#include <stdarg.h>    
#include <unistd.h>    
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h> 
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>

#define READBUF_SIZE    16384

#include "setup.h"     
#include "ircd_lib.h"
#include "adns.h"


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


static void
restart_resolver(int sig)
{
        /* Rehash dns configuration */
        adns__rereadconfig(dns_state);
}

static void
setup_signals(void)
{
        struct sigaction act;
        act.sa_flags = 0;
        act.sa_handler = SIG_IGN;
        sigemptyset(&act.sa_mask);
        act.sa_handler = restart_resolver;
        sigaddset(&act.sa_mask, SIGHUP);
        sigaction(SIGHUP, &act, 0);
}


static void
write_sendq(void)
{
        int retlen;
        if(linebuf_len(&sendq) > 0)
        {
                while((retlen = linebuf_flush(ofd, &sendq)) > 0);
                if(retlen == 0 || (retlen < 0 && !ignoreErrno(errno)))
                {
                        exit(1);
                }
        }
        if(linebuf_len(&sendq) > 0)
		FD_SET(ofd, &writefds);
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
read_request(void)
{
        int length;

        while((length = read(ifd, readBuf, sizeof(readBuf))) > 0)
        {
                linebuf_parse(&recvq, readBuf, length, 0);
                parse_request();
        }
         
        if(length == 0)
                exit(1);

        if(length == -1 && !ignoreErrno(errno))
                exit(1);
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
						inet_ntop(AF_INET6, &reply->rrs.addr->addr.inet6.sin6_addr, tmpres, sizeof(tmpres)-1);
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
						strcpy(response, inet_ntoa(reply->rrs.addr->addr.inet.sin_addr));
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
				write_sendq();
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
	write_sendq();
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
	struct timeval *tv = NULL, tvbuf, now, tvx;
	int select_result;
	int maxfd = -1;

	while(1)
	{
		FD_ZERO(&readfds);
		FD_ZERO(&writefds);
		FD_ZERO(&exceptfds);

		FD_SET(ifd, &readfds);
		
		if(ifd > ofd)
			maxfd = ifd+1;
		else
			maxfd = ofd+1;

		set_time();

		eventRun();
                                                        
		adns_beforeselect(dns_state, &maxfd, &readfds, &writefds, &exceptfds, &tv, &tvbuf, &now);
		tvx.tv_sec = 1;
		tvx.tv_usec = 0;
		select_result = select(maxfd, &readfds, &writefds, &exceptfds, &tvx);
		adns_afterselect(dns_state, maxfd, &readfds,&writefds,&exceptfds, &now);
		process_adns_incoming();

		if(select_result == 0)
			continue;

		/* have data to parse */
		if(select_result > 0)
		{
			if(FD_ISSET(ifd, &readfds))
			{
				read_request();
				/* incoming requests */
			}
			if(FD_ISSET(ofd, &writefds))
			{
				write_sendq();
			}
		}
		
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
        int i, res;
        char *tifd;
        char *tofd;

        tifd = getenv("IFD");
        tofd = getenv("OFD");
        if(tifd == NULL || tofd == NULL)
                exit(1);
        ifd = atoi(tifd);
        ofd = atoi(tofd);

        ircd_lib(NULL, NULL, NULL); /* XXX fix me */

        linebuf_newbuf(&sendq);
        linebuf_newbuf(&recvq);

	if(ifd > 255)
	{
		for(i = 3; i < 255; i++)
		{
			if(i != ofd) 
			{
				if(dup2(ifd, i) < 0)
					exit(1);
				close(ifd);
				ifd = i;
				break;
			}
		}
	}

	if(ofd > 255)
	{
		for(i = 3; i < 255; i++)
		{
			if(i != ifd) 
			{
				if(dup2(ofd, i) < 0)
					exit(1);
				close(ofd);
				ofd = i;
				break;
			}
		}
	}
	
	res = fcntl(ifd, F_GETFL, 0);
	fcntl(ifd, F_SETFL, res | O_NONBLOCK);

	res = fcntl(ofd, F_GETFL, 0);
	fcntl(ofd, F_SETFL, res | O_NONBLOCK);
	
	adns_init(&dns_state, adns_if_noautosys, 0);
	setup_signals();
	read_io();	
	return 1;
}


