/*
 * ident.c: identd check daemon for ircd-ratbox
 * Based on many things ripped from ratbox-services
 * and ircd-ratbox itself and who knows what else
 *
 * Strangely this looks like resolver.c but I can't 
 * seem to figure out why...
 *
 * Copyright (C) 2003-2005 Lee Hardy <leeh@leeh.co.uk>
 * Copyright (C) 2003-2005 ircd-ratbox development team
 * Copyright (C) 2005 Aaron Sethman <androsyn@ratbox.org>
 *
 * $Id$
 */

#include "setup.h"
#include "ircd_lib.h"
#define USERLEN 10

/* data fd from ircd */
int irc_ifd;
/* control fd from ircd */
int irc_ofd; 

int ident_timeout;

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

struct auth_request
{
	struct irc_sockaddr_storage bindaddr;
	struct irc_sockaddr_storage destaddr;
	int srcport;
	int dstport;
	char reqid[REQIDLEN];
	int authfd;
};

static BlockHeap *authheap;

static char buf[512]; /* scratch buffer */
static char readBuf[READBUF_SIZE];

static buf_head_t recvq;
static buf_head_t sendq;

static int
send_sprintf(int fd, const char *format, ...)
{
	va_list args; 
	va_start(args, format);
	ircd_vsprintf(buf, format, args);
	va_end(args); 
	return(ircd_write(fd, buf, strlen(buf)));
}



/* io_to_array()
 *   Changes a given buffer into an array of parameters.
 *   Taken from ircd-ratbox.
 *
 * inputs	- string to parse, array to put in
 * outputs	- number of parameters
 */
static inline int
io_to_array(char *string, char *parv[MAXPARA])
{
	char *p, *xbuf = string;
	int x = 0;

	parv[x] = NULL;

	if(EmptyString(string))
		return x;

	while (*xbuf == ' ')	/* skip leading spaces */
		xbuf++;
	if(*xbuf == '\0')	/* ignore all-space args */
		return x;

	do
	{
		if(*xbuf == ':')	/* Last parameter */
		{
			xbuf++;
			parv[x++] = xbuf;
			parv[x] = NULL;
			return x;
		}
		else
		{
			parv[x++] = xbuf;
			parv[x] = NULL;
			if((p = strchr(xbuf, ' ')) != NULL)
			{
				*p++ = '\0';
				xbuf = p;
			}
			else
				return x;
		}
		while (*xbuf == ' ')
			xbuf++;
		if(*xbuf == '\0')
			return x;
	}
	while (x < MAXPARA - 1);

	if(*p == ':')
		p++;

	parv[x++] = p;
	parv[x] = NULL;
	return x;
}


/*
request protocol:

INPUTS:

ID bindhost 4/6 destaddr srcport dstport

Note that the srcport/dsport parameters are for the identd request

OUTPUTS:

ID success/failure username

*/

static void
write_sendq(int fd, void *unused)
{
	int retlen;
	if(ircd_linebuf_len(&sendq) > 0)
	{
		while((retlen = ircd_linebuf_flush(irc_ofd, &sendq)) > 0);
		if(retlen == 0 || (retlen < 0 && !ignoreErrno(errno)))
		{
			exit(1);
		}
	}
	 
	if(ircd_linebuf_len(&sendq) > 0)
	{
		ircd_setselect(irc_ofd, IRCD_SELECT_WRITE,
			       write_sendq, NULL, 0);
	}
}
 


static void
read_auth_timeout(int fd, void *data)
{
	struct auth_request *auth = data;
	ircd_linebuf_put(&sendq, "%s 0", auth->reqid);
	write_sendq(irc_ofd, NULL);
	BlockHeapFree(authheap, auth);
	ircd_close(fd);
}


static char *
GetValidIdent(char *xbuf)
{
	int remp = 0;
	int locp = 0;     
	char *colon1Ptr;
	char *colon2Ptr;
	char *colon3Ptr;
	char *commaPtr;
	char *remotePortString;

	/* All this to get rid of a sscanf() fun. */
	remotePortString = xbuf;

	colon1Ptr = strchr(remotePortString, ':');
	if(!colon1Ptr)   
		return NULL;

	*colon1Ptr = '\0';
	colon1Ptr++;
	colon2Ptr = strchr(colon1Ptr, ':');
	if(!colon2Ptr)   
		return NULL;

	*colon2Ptr = '\0';
	colon2Ptr++;
	commaPtr = strchr(remotePortString, ',');

	if(!commaPtr)
		return NULL;

	*commaPtr = '\0';
	commaPtr++;

	remp = atoi(remotePortString);
	if(!remp)
		return NULL;

	locp = atoi(commaPtr);
	if(!locp)
		return NULL;

	/* look for USERID bordered by first pair of colons */
	if(!strstr(colon1Ptr, "USERID"))
		return NULL;

	colon3Ptr = strchr(colon2Ptr, ':');
	if(!colon3Ptr)
		return NULL;

	*colon3Ptr = '\0';
	colon3Ptr++;
	return (colon3Ptr);
}


static void
read_auth(int fd, void *data)
{
	struct auth_request *auth = data;
	char username[USERLEN], *s, *t;
	int len, count;

	len = ircd_read(fd, buf, sizeof(buf));

	if(len < 0 && ignoreErrno(errno))
	{
		ircd_settimeout(fd, 30, read_auth_timeout, auth);
		ircd_setselect(fd, IRCD_SELECT_READ, read_auth, auth, 30);
		return;
	} else {
		buf[len] = '\0';
		if((s = GetValidIdent(buf)))
		{
			t = username;
			while(*s == '~' || *s == '^')
				s++;
			for(count = USERLEN; *s && count; s++)
			{
				if(*s == '@')
					break;
				if(!isspace(*s) && *s != ':' && *s != '[')
				{
					*t++ = *s;
					count--;
				}
			}
			*t = '\0';
			ircd_linebuf_put(&sendq, "%s %s", auth->reqid, username);
		} else
			ircd_linebuf_put(&sendq, "%s 0", auth->reqid);
		write_sendq(irc_ofd, NULL);
		ircd_close(fd);
		BlockHeapFree(authheap, auth);
	}
}

static void
connect_callback(int fd, int status, void *data)
{
	struct auth_request *auth = data;

	if(status == IRCD_OK)
	{
		/* one shot at the send, socket buffers should be able to handle it
		 * if not, oh well, you lose
		 */
		if(send_sprintf(fd, "%u , %u\r\n", auth->srcport, auth->dstport) <= 0)
		{
			ircd_linebuf_put(&sendq, "%s 0", auth->reqid);
			write_sendq(irc_ofd, NULL);
			ircd_close(fd);
			BlockHeapFree(authheap, auth);
			return;
		}
		read_auth(fd, auth);
	} else {
		ircd_linebuf_put(&sendq, "%s 0", auth->reqid);
		write_sendq(irc_ofd, NULL);
		ircd_close(fd);
		BlockHeapFree(authheap, auth);
	}
}

static void
check_identd(const char *id, const char *bindaddr, const char *destaddr, const char *srcport, const char *dstport)
{
	struct auth_request *auth;
	auth = BlockHeapAlloc(authheap);

	ircd_inet_pton_sock(bindaddr, (struct sockaddr *)&auth->bindaddr);
	ircd_inet_pton_sock(destaddr, (struct sockaddr *)&auth->destaddr);

#ifdef IPV6
	if(((struct sockaddr *)&auth->destaddr)->sa_family == AF_INET6)
		((struct sockaddr_in6 *)&auth->destaddr)->sin6_port = htons(113);
	else
#endif
		((struct sockaddr_in *)&auth->destaddr)->sin_port = htons(113);

	auth->srcport = atoi(srcport);
	auth->dstport = atoi(dstport);
	strcpy(auth->reqid, id);

	auth->authfd = ircd_socket(((struct sockaddr *)&auth->destaddr)->sa_family, SOCK_STREAM, 0, "auth fd");
	ircd_connect_tcp(auth->authfd, (struct sockaddr *)&auth->destaddr, 
		(struct sockaddr *)&auth->bindaddr, GET_SS_LEN(auth->destaddr), connect_callback, auth, ident_timeout);
}

static void
parse_request(void)
{
	int len;
	static char *parv[MAXPARA + 1];
	int parc;
	while((len = ircd_linebuf_get(&recvq, readBuf, sizeof(readBuf),
				 LINEBUF_COMPLETE, LINEBUF_PARSED)) > 0)
	{
		parc = io_to_array(readBuf, parv);
		switch(parc)
		{
			case 5:
				check_identd(parv[0], parv[1], parv[2], parv[3], parv[4]);
				break;
			case 2:
				if(*parv[0] == 'T')
				{
					ident_timeout = atoi(parv[1]);
					break;
				}
			default:
				exit(0);
		}
	}
}


static void
read_request(int fd, void *unusued)
{
        int length;
        while((length = ircd_read(fd, readBuf, sizeof(readBuf))) > 0)
        {
                ircd_linebuf_parse(&recvq, readBuf, length, 0);
                parse_request();
        }
         
        if(length == 0)
                exit(1);

        if(length == -1 && !ignoreErrno(errno))
                exit(1);

	ircd_setselect(irc_ifd, IRCD_SELECT_READ, read_request, NULL, 0);
}

static void
ilogcb(const char *str)
{
	return;
}
 
static void
restartcb(const char *str)
{
        exit(0);
}
 
static void
diecb(const char *str)
{
        exit(0);  
}


int main(int argc, char **argv)
{
	char *tifd, *tofd, *tmaxfd, *tident_timeout;
	int maxfd, x;

	tifd = getenv("IFD");
	tofd = getenv("OFD");
	tmaxfd = getenv("MAXFD");
	tident_timeout = getenv("IDENT_TIMEOUT");
	if(tifd == NULL || tofd == NULL || tmaxfd == NULL || tident_timeout == NULL)
	{
		fprintf(stderr, "This is ircd-ratbox ident.  You aren't supposed to run be directly.\n");
		fprintf(stderr, "However I will print my Id tag $Id$\n"); 
		fprintf(stderr, "Have a nice day\n");
		exit(1);
	}
	maxfd = atoi(tmaxfd);
	irc_ifd = atoi(tifd);
	irc_ofd = atoi(tofd);
	ident_timeout = atoi(tident_timeout);
	
#ifndef __MINGW32__
	for(x = 0; x < maxfd; x++)
	{
		if(x != irc_ifd && x != irc_ofd)
			close(x);
	}
#endif
	ircd_lib(ilogcb, restartcb, diecb, 0, 1024, 1024, 1024); /* XXX fix me */
	ircd_linebuf_newbuf(&sendq);
	ircd_linebuf_newbuf(&recvq);
	authheap = BlockHeapCreate(sizeof(struct auth_request), 2048);

	ircd_open(irc_ifd, FD_PIPE, "ircd ifd");
	ircd_open(irc_ofd, FD_PIPE, "ircd ofd");

	ircd_set_nb(irc_ifd);
	ircd_set_nb(irc_ofd);
	read_request(irc_ifd, NULL);
	

	while(1) {
		ircd_select(1000);
		ircd_checktimeouts(NULL);
		ircd_event_run();
	}
	return 0;
}


