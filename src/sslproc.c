/*
 *  sslproc.c: An interface to ssld
 *  Copyright (C) 2007 Aaron Sethman <androsyn@ratbox.org>
 *  Copyright (C) 2007 ircd-ratbox development team
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

#include <ratbox_lib.h>
#include "stdinc.h"
#include "s_conf.h"
#include "s_log.h"
#include "listener.h"
#include "struct.h"
#include "sslproc.h"
#include "s_serv.h"
#include "ircd.h"
#include "hash.h"
#include "client.h"
#include "send.h"


#define ZIPSTATS_TIME           60

static void collect_zipstats(void *unused);
static void ssl_read_ctl(rb_fde_t *F, void *data);
static int ssld_count;

#define MAXPASSFD 4
#define READSIZE 1024
typedef struct _ssl_ctl_buf
{
	rb_dlink_node node;
	char *buf;
	size_t buflen;
	rb_fde_t *F[MAXPASSFD];
	int nfds;
} ssl_ctl_buf_t;


struct _ssl_ctl
{
	rb_dlink_node node;
	int cli_count;
	rb_fde_t *F;
	rb_fde_t *P;
	pid_t pid;
	rb_dlink_list readq;
	rb_dlink_list writeq;
	rb_uint8_t dead;
};

static void send_new_ssl_certs_one(ssl_ctl_t *ctl, const char *ssl_cert, const char *ssl_private_key, const char *ssl_dh_params);

static rb_dlink_list ssl_daemons;

static ssl_ctl_t *
allocate_ssl_daemon(rb_fde_t *F, rb_fde_t *P, int pid)
{
	ssl_ctl_t *ctl;
	
	if(F == NULL || pid < 0)
		return NULL;
	ctl = rb_malloc(sizeof(ssl_ctl_t));	
	ctl->F = F;
	ctl->P = P;
	ctl->pid = pid;
	ssld_count++;
	rb_dlinkAdd(ctl, &ctl->node, &ssl_daemons);
	return ctl;
}

static void
free_ssl_daemon(ssl_ctl_t *ctl)
{
	rb_dlink_node *ptr;
	ssl_ctl_buf_t *ctl_buf;
	int x;

	if(ctl->cli_count)
		return;
	
	RB_DLINK_FOREACH(ptr, ctl->readq.head)
	{
		ctl_buf = ptr->data;
		for(x = 0; x < ctl_buf->nfds; x++)
			rb_close(ctl_buf->F[x]);	

		rb_free(ctl_buf->buf);
		rb_free(ctl_buf);	
	}

	RB_DLINK_FOREACH(ptr, ctl->writeq.head)
	{
		ctl_buf = ptr->data;
		for(x = 0; x < ctl_buf->nfds; x++)
			rb_close(ctl_buf->F[x]);

		rb_free(ctl_buf->buf);
		rb_free(ctl_buf);
	}
	rb_close(ctl->F);
	rb_close(ctl->P);
	rb_dlinkDelete(&ctl->node, &ssl_daemons);
	rb_free(ctl);
}

static char *ssld_path;

static int ssld_spin_count = 0;
static time_t last_spin;
static int ssld_wait = 0;
static void
ssl_dead(ssl_ctl_t *ctl)
{
	ctl->dead = 1;
	ssld_count--;
	kill(ctl->pid, SIGKILL); /* make sure the process is really gone */
	ilog(L_MAIN, "ssld helper died - attempting to restart");
	sendto_realops_flags(UMODE_ALL, L_ALL, "ssld helper died - attempting to restart");
	start_ssldaemon(1, ServerInfo.ssl_cert, ServerInfo.ssl_private_key, ServerInfo.ssl_dh_params);
}

static void
ssl_do_pipe(rb_fde_t *F, void *data)
{
	int retlen;
	ssl_ctl_t *ctl = data;
	retlen = rb_write(F, "0", 1);
	if(retlen == 0 || (retlen < 0 && !rb_ignore_errno(errno)))
	{
		ssl_dead(ctl);
		return;
	}
	rb_setselect(F, RB_SELECT_READ, ssl_do_pipe, data);
}

static void
restart_ssld_event(void *unused)
{
	ssld_spin_count = 0;
	last_spin = 0;
	ssld_wait = 0;
	if(ServerInfo.ssld_count > get_ssld_count())
	{
		int start = ServerInfo.ssld_count - get_ssld_count();
		ilog(L_MAIN, "Attempting to restart ssld processes");
		sendto_realops_flags(UMODE_ALL, L_ALL, "Attempt to restart ssld processes");
		start_ssldaemon(start, ServerInfo.ssl_cert, ServerInfo.ssl_private_key, ServerInfo.ssl_dh_params);
	}
}

int
start_ssldaemon(int count, const char *ssl_cert, const char *ssl_private_key, const char *ssl_dh_params)
{
	rb_fde_t *F1, *F2;
	rb_fde_t *P1, *P2;
	char fullpath[PATH_MAX + 1];
	char fdarg[6];
	const char *parv[2];
	char buf[128];
	pid_t pid;
	int started = 0, i;

	if(ssld_wait)
		return 0;

	if(ssld_spin_count > 20 && (rb_current_time() - last_spin < 5))
	{
		ilog(L_MAIN, "ssld helper is spinning - will attempt to restart in 5 minutes");
		sendto_realops_flags(UMODE_ALL, L_ALL, "ssld helper is spinning - will attempt to restart in 1 minute");
		rb_event_add("restart_ssld_event", restart_ssld_event, NULL, 60);
		ssld_wait = 1;
		return 0;
	}

	ssld_spin_count++;
	last_spin = rb_current_time();
	if(ssld_path == NULL)
	{
		rb_snprintf(fullpath, sizeof(fullpath), "%s/ssld", BINPATH);
		
		if(access(fullpath, X_OK) == -1)
		{
			rb_snprintf(fullpath, sizeof(fullpath), "%s/bin/ssld", ConfigFileEntry.dpath);
			if(access(fullpath, X_OK) == -1)
			{
				ilog(L_MAIN, "Unable to execute ssld in %s/bin or %s", ConfigFileEntry.dpath, BINPATH);
				return 0 ;
			}
		}
		ssld_path = rb_strdup(fullpath);
	}

	rb_strlcpy(buf, "-ircd ssld daemon helper", sizeof(buf));
	parv[0] = buf;
	parv[1] = NULL;

	for(i = 0; i < count; i++)
	{
		ssl_ctl_t *ctl;
		rb_socketpair(AF_UNIX, SOCK_DGRAM, 0, &F1, &F2, "SSL/TLS handle passing socket");
		rb_set_buffers(F1, READBUF_SIZE);
		rb_set_buffers(F2, READBUF_SIZE);
		rb_snprintf(fdarg, sizeof(fdarg), "%d", rb_get_fd(F2));
		setenv("CTL_FD", fdarg, 1);
		rb_pipe(&P1, &P2, "SSL/TLS pipe");
		rb_snprintf(fdarg, sizeof(fdarg), "%d", rb_get_fd(P1));
		setenv("CTL_PIPE", fdarg, 1);
		
		pid = rb_spawn_process(ssld_path, (const char **)parv);
		if(pid == -1)
		{
			ilog(L_MAIN, "Unable to create ssld: %s\n", strerror(errno));
			rb_close(F1);
			rb_close(F2);
			rb_close(P1);
			rb_close(P2);
			return started;
		}
		started++;
		rb_close(F2);
		rb_close(P1);
		ctl = allocate_ssl_daemon(F1, P2, pid);
		if(ssl_ok && ssl_cert != NULL && ssl_private_key != NULL)
			send_new_ssl_certs_one(ctl, ssl_cert, ssl_private_key, ssl_dh_params != NULL ? ssl_dh_params : "");
		ssl_read_ctl(ctl->F, ctl);
		ssl_do_pipe(P2, ctl);
	}
	return started;	
}

static void
ssl_process_zipstats(ssl_ctl_t *ctl, ssl_ctl_buf_t *ctl_buf)
{
	struct Client *server;
	struct ZipStats *zips;
	int parc;
	char *parv[6];
	parc = rb_string_to_array(ctl_buf->buf, parv, 6);		
	server = find_server(NULL, parv[1]);
	if(server == NULL || server->localClient == NULL || !IsCapable(server, CAP_ZIP))
		return;
	if(server->localClient->zipstats == NULL)
		server->localClient->zipstats = rb_malloc(sizeof(struct ZipStats));
	
	zips = server->localClient->zipstats;

	zips->in += strtoul(parv[2], NULL, 10);
	zips->in_wire += strtoul(parv[3], NULL, 10);
	zips->out += strtoul(parv[4], NULL, 10);
	zips->out_wire += strtoul(parv[5], NULL, 10);
	
	zips->inK += zips->in >> 10;
	zips->in &= 0x03ff;
	
	zips->inK_wire += zips->in_wire >> 10;
	zips->in_wire &= 0x03ff;
	
	zips->outK += zips->out >> 10;
	zips->out &= 0x03ff;
	
	zips->outK_wire += zips->out_wire >> 10;
	zips->out_wire &= 0x03ff;
	
	if(zips->inK > 0)
		zips->in_ratio = (((double) (zips->inK - zips->inK_wire) / (double) zips->inK) * 100.00);
	else
		zips->in_ratio = 0;
		
	if(zips->outK > 0)
		zips->out_ratio = (((double) (zips->outK - zips->outK_wire) / (double) zips->outK) * 100.00);
	else
		zips->out_ratio = 0;
}

static void
ssl_process_dead_fd(ssl_ctl_t *ctl, ssl_ctl_buf_t *ctl_buf)
{
	struct Client *client_p;
	char reason[256];
	rb_int32_t fd;
	char *buf = ctl_buf->buf;

	if(ctl_buf->buflen < 6)
		return; /* bogus message..drop it.. XXX should warn here */
	
	fd = buf[1] << 24;
	fd |= buf[2] << 16;
	fd |= buf[3] << 8;
	fd |= buf[4];
	rb_strlcpy(reason, &buf[5], sizeof(reason));
	client_p = find_cli_fd_hash(fd);
	if(client_p == NULL)
		return;
	if(IsServer(client_p))
		sendto_realops_flags(UMODE_ALL, L_ALL, "ssld error for %s: %s", client_p->name, reason);
	exit_client(client_p, client_p, &me, reason);
}

static void
ssl_process_cmd_recv(ssl_ctl_t *ctl)
{
	rb_dlink_node *ptr, *next;	
	ssl_ctl_buf_t *ctl_buf;
	if(ctl->dead)
		return;
	RB_DLINK_FOREACH_SAFE(ptr, next, ctl->readq.head)
	{
		ctl_buf = ptr->data;		
		switch(*ctl_buf->buf)
		{
			case 'D':
				ssl_process_dead_fd(ctl, ctl_buf);
				break;				
			case 'S':
				ssl_process_zipstats(ctl, ctl_buf);
				break;
			default:
				ilog(L_MAIN, "Received invalid command from ssld: %s", ctl_buf->buf);
				sendto_realops_flags(UMODE_ALL, L_ALL, "Received invalid command from ssld");
				break;
		}
		rb_dlinkDelete(ptr, &ctl->readq);
		rb_free(ctl_buf->buf);
		rb_free(ctl_buf);
	}

}


static void
ssl_read_ctl(rb_fde_t *F, void *data)
{
	ssl_ctl_buf_t *ctl_buf;
	ssl_ctl_t *ctl = data;
	int retlen;

	if(ctl->dead)
		return;
	do
	{
		ctl_buf = rb_malloc(sizeof(ssl_ctl_buf_t));
		ctl_buf->buf = rb_malloc(READSIZE);
		retlen = rb_recv_fd_buf(ctl->F, ctl_buf->buf, READSIZE, ctl_buf->F, 4);
		ctl_buf->buflen = retlen;
		if(retlen <= 0) {
			rb_free(ctl_buf->buf);
			rb_free(ctl_buf);
		}
		else
			rb_dlinkAddTail(ctl_buf, &ctl_buf->node, &ctl->readq);
	} while(retlen > 0);	
	
	if(retlen == 0 || (retlen < 0 && !rb_ignore_errno(errno)))
	{
		ssl_dead(ctl);
		return;
	} 
	ssl_process_cmd_recv(ctl);
	rb_setselect(ctl->F, RB_SELECT_READ, ssl_read_ctl, ctl);
}

static ssl_ctl_t *
which_ssld(void)
{
	ssl_ctl_t *ctl, *lowest = NULL;
	rb_dlink_node *ptr;
	
	RB_DLINK_FOREACH(ptr, ssl_daemons.head)
	{
		ctl = ptr->data;
		if(ctl->dead)
			continue;
		if(lowest == NULL) {
			lowest = ctl;
			continue;
		}
		if(ctl->cli_count < lowest->cli_count)
			lowest = ctl;
	}
	return(lowest);
}

static void
ssl_write_ctl(rb_fde_t *F, void *data)
{
	ssl_ctl_t *ctl = data;
	ssl_ctl_buf_t *ctl_buf;
	rb_dlink_node *ptr, *next;
	int retlen, x;

	if(ctl->dead)
		return;

	RB_DLINK_FOREACH_SAFE(ptr, next, ctl->writeq.head)
	{
		ctl_buf = ptr->data;
		/* in theory unix sock_dgram shouldn't ever short write this.. */
		retlen = rb_send_fd_buf(ctl->F, ctl_buf->F, ctl_buf->nfds,  ctl_buf->buf, ctl_buf->buflen);
		if(retlen > 0)
		{
			rb_dlinkDelete(ptr, &ctl->writeq);
			for(x = 0; x < ctl_buf->nfds; x++)
				rb_close(ctl_buf->F[x]);
			rb_free(ctl_buf->buf);
			rb_free(ctl_buf);
			
		}
		if(retlen == 0 || (retlen < 0 && !rb_ignore_errno(errno)))
		{
			ssl_dead(ctl);
			return;
		} else  {
			rb_setselect(ctl->F, RB_SELECT_WRITE, ssl_write_ctl, ctl);
		}
	}
}

static void
ssl_cmd_write_queue(ssl_ctl_t *ctl, rb_fde_t **F, int count, const void *buf, size_t buflen)
{
	ssl_ctl_buf_t *ctl_buf;
	int x; 

	/* don't bother */
	if(ctl->dead)
		return;
		
	ctl_buf = rb_malloc(sizeof(ssl_ctl_buf_t));
	ctl_buf->buf = rb_malloc(buflen);
	memcpy(ctl_buf->buf, buf, buflen);
	ctl_buf->buflen = buflen;
		
	for(x = 0; x < count && x < MAXPASSFD; x++)
	{
		ctl_buf->F[x] = F[x];	
	}
	ctl_buf->nfds = count;
	rb_dlinkAddTail(ctl_buf, &ctl_buf->node, &ctl->writeq);
	ssl_write_ctl(ctl->F, ctl);
}


static void
send_new_ssl_certs_one(ssl_ctl_t *ctl, const char *ssl_cert, const char *ssl_private_key, const char *ssl_dh_params)
{
	static char buf[READBUF_SIZE];	
	static char nul = '\0';
	int len;
	len = rb_snprintf(buf, sizeof(buf), "K%c%s%c%s%c%s%c", nul, ssl_cert, nul, ssl_private_key, nul, ssl_dh_params, nul);
	ssl_cmd_write_queue(ctl, NULL, 0, buf, len);
}

void
send_new_ssl_certs(const char *ssl_cert, const char *ssl_private_key, const char *ssl_dh_params)
{
	rb_dlink_node *ptr;
	if(ssl_cert == NULL || ssl_private_key == NULL || ssl_dh_params == NULL)
	{
		ssl_ok = 0;
		return;
	}
	RB_DLINK_FOREACH(ptr, ssl_daemons.head)
	{
		ssl_ctl_t *ctl = ptr->data;
		send_new_ssl_certs_one(ctl, ssl_cert, ssl_private_key, ssl_dh_params);	
	}
}


ssl_ctl_t * 
start_ssld_accept(rb_fde_t *sslF, rb_fde_t *plainF, rb_int32_t id)
{
	rb_fde_t *F[2];
	ssl_ctl_t *ctl;
	rb_uint8_t buf[5];
	F[0] = sslF;
	F[1] = plainF;

	buf[0] = 'A';
	buf[1] = (id >> 24) & 0xFF;
	buf[2] = (id >> 16) & 0xFF;
	buf[3] = (id >> 8) & 0xFF;
	buf[4] = id & 0xFF;
	ctl = which_ssld();
	ctl->cli_count++;
	ssl_cmd_write_queue(ctl, F, 2, buf, sizeof(buf));
	return ctl;
}

ssl_ctl_t *
start_ssld_connect(rb_fde_t *sslF, rb_fde_t *plainF, rb_int32_t id)
{
	rb_fde_t *F[2];
	ssl_ctl_t *ctl;
	rb_uint8_t buf[5];
	F[0] = sslF;
	F[1] = plainF;

	buf[0] = 'C';
	buf[1] = (id >> 24) & 0xFF;
	buf[2] = (id >> 16) & 0xFF;
	buf[3] = (id >> 8) & 0xFF;
	buf[4] = id & 0xFF;
	
	ctl = which_ssld();
	ctl->cli_count++;
	ssl_cmd_write_queue(ctl, F, 2, buf, sizeof(buf));
	return ctl; 
}

void 
ssld_decrement_clicount(ssl_ctl_t *ctl)
{
	if(ctl == NULL)
		return;

	ctl->cli_count--;
	if(ctl->dead && !ctl->cli_count)
	{
		free_ssl_daemon(ctl);
	}
}

/* 
 * what we end up sending to the ssld process for ziplinks is the following
 * Z[ourfd][level][RECVQ]
 * Z = ziplinks command
 * ourfd = Our end of the socketpair
 * recvq = any data we read prior to starting ziplinks
 */
void
start_zlib_session(struct Client *server)
{
	void *xbuf;
	rb_fde_t *F[2];
	rb_fde_t *xF1, *xF2;
	rb_uint8_t *buf;
	rb_int32_t id;
	rb_uint8_t level;
	size_t hdr = (sizeof(rb_uint8_t) * 2) + sizeof(rb_int32_t);
	size_t len;
	int cpylen, left;
	
	len = rb_linebuf_len(&server->localClient->buf_recvq);
	len += hdr;	
	buf = rb_malloc(len);
	id = rb_get_fd(server->localClient->F);
	level = ConfigFileEntry.compression_level;
	buf[1] = (id >> 24) & 0xFF;
	buf[2] = (id >> 16) & 0xFF;
	buf[3] = (id >> 8) & 0xFF;
	buf[4] = id & 0xFF;
	buf[5] = level;

	server->localClient->zipstats = rb_malloc(sizeof(struct ZipStats));

	if(server->localClient->ssl_ctl != NULL)
	{
		*buf = 'Y'; 	
		rb_linebuf_get(&server->localClient->buf_recvq, (char *)(buf+hdr), len - hdr, LINEBUF_PARTIAL, LINEBUF_RAW);		
		ssl_cmd_write_queue(server->localClient->ssl_ctl, NULL, 0, buf, len);
		rb_free(buf);
		return;
	} 
	
	if(len > READBUF_SIZE)
	{
		rb_free(buf);
		sendto_realops_flags(UMODE_ALL, L_ALL, "ssld - attempted to pass message of %ld len, max len %d, giving up", (long)len, READBUF_SIZE);
		ilog(L_MAIN, "ssld - attempted to pass message of %ld len, max len %d, giving up", (long)len, READBUF_SIZE);
		exit_client(server, server, server, "ssld readbuf exceeded");
		return;
	}
	
	*buf = 'Z';
	xbuf = buf + hdr;
	left = len - hdr;
	do
	{
		cpylen = rb_linebuf_get(&server->localClient->buf_recvq, xbuf, left, LINEBUF_PARTIAL, LINEBUF_RAW);
		left -= cpylen;
		xbuf += cpylen;
	} while(cpylen > 0);
	
		
	rb_socketpair(AF_UNIX, SOCK_STREAM, 0, &xF1, &xF2, "Initial zlib socketpairs");
	F[0] = server->localClient->F; 
	F[1] = xF1;
	del_from_cli_fd_hash(server);	
	server->localClient->F = xF2;
	/* need to redo as what we did before isn't valid now */
	id = rb_get_fd(server->localClient->F);
	buf[1] = (id >> 24) & 0xFF;
	buf[2] = (id >> 16) & 0xFF;
	buf[3] = (id >> 8) & 0xFF;
	buf[4] = id & 0xFF;
	add_to_cli_fd_hash(server);
	server->localClient->ssl_ctl = which_ssld();
	server->localClient->ssl_ctl->cli_count++;
	ssl_cmd_write_queue(server->localClient->ssl_ctl, F, 2, buf, len);
	rb_free(buf);
}

static void
collect_zipstats(void *unused)
{
	rb_dlink_node *ptr;
	struct Client *target_p;
	rb_uint8_t buf[sizeof(rb_uint8_t) + sizeof(rb_int32_t) + HOSTLEN];
//	char buf[1+4+HOSTLEN]; /* S[id]HOSTLEN\0 */
	void *odata;
	size_t len;
	rb_int32_t id;

	buf[0] = 'S';
	odata = buf + sizeof(rb_uint8_t) + sizeof(rb_int32_t);

	RB_DLINK_FOREACH(ptr, serv_list.head)
	{
		target_p = ptr->data;
		if(IsCapable(target_p, CAP_ZIP))
		{
			len = sizeof(rb_uint8_t) + sizeof(rb_uint32_t);

			id = rb_get_fd(target_p->localClient->F);
			buf[1] = (id >> 24) & 0xFF;
			buf[2] = (id >> 16) & 0xFF;
			buf[3] = (id >> 8) & 0xFF;
			buf[4] = id & 0xFF;
			rb_strlcpy(odata, target_p->name, (sizeof(buf)-len));
			len += strlen(odata) + 1; /* Get the \0 as well */
			ssl_cmd_write_queue(target_p->localClient->ssl_ctl, NULL, 0, buf, len); 
		}
	}
}

static void
cleanup_dead_ssl(void *unused)
{
	rb_dlink_node *ptr, *next;
	ssl_ctl_t *ctl;
	RB_DLINK_FOREACH_SAFE(ptr, next, ssl_daemons.head)
	{
		ctl = ptr->data;
		if(ctl->dead && !ctl->cli_count)
		{
			free_ssl_daemon(ctl);			
		}
	}
}

int
get_ssld_count(void)
{
	return ssld_count;
}

void init_ssld(void)
{
	rb_event_addish("collect_zipstats", collect_zipstats, NULL, ZIPSTATS_TIME);
	rb_event_addish("cleanup_dead_ssld", cleanup_dead_ssl, NULL, 1200);
}

