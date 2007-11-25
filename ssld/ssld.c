/*
 *  ssld.c: The ircd-ratbox ssl/zlib helper daemon thingy
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


#include "stdinc.h"

#ifdef HAVE_LIBZ
#include <zlib.h>
#endif

#define MAXPASSFD 4
#ifndef READBUF_SIZE
#define READBUF_SIZE 16384
#endif

static void conn_mod_write_sendq(rb_fde_t *, void *data);
static void conn_plain_write_sendq(rb_fde_t *, void *data);
static void mod_write_ctl(rb_fde_t *, void *data);

static char inbuf[READBUF_SIZE];
static char outbuf[READBUF_SIZE];

typedef struct _mod_ctl_buf
{
	rb_dlink_node node;
	char *buf;
	size_t buflen;
	rb_fde_t *F[MAXPASSFD];
	int nfds;
} mod_ctl_buf_t;

typedef struct _mod_ctl
{
	rb_dlink_node node;
	int cli_count;
	rb_fde_t *F;
	rb_fde_t *F_pipe;
	rb_dlink_list readq;
	rb_dlink_list writeq;
} mod_ctl_t;

mod_ctl_t *g_ctl;


typedef struct _conn
{
	rb_dlink_node node;
	rb_dlink_node hash_node;
	rawbuf_head_t *modbuf_out;
	rawbuf_head_t *plainbuf_out;

	rb_uint16_t id;

	rb_fde_t *mod_fd;
	rb_fde_t *plain_fd;
	unsigned long mod_out;
	unsigned long mod_in;
	unsigned long plain_in;
	unsigned long plain_out;
	rb_uint8_t is_ssl;
#ifdef HAVE_LIBZ
	rb_uint8_t is_zlib;
	z_stream instream;
	z_stream outstream;
#endif
} conn_t;

#define CONN_HASH_SIZE 2000
#define connid_hash(x)	(&connid_hash_table[(x % CONN_HASH_SIZE)])

static rb_dlink_list connid_hash_table[CONN_HASH_SIZE];



static conn_t *
conn_find_by_id(rb_uint16_t id)
{
	rb_dlink_node *ptr;
	conn_t *conn;

	RB_DLINK_FOREACH(ptr, (connid_hash(id))->head)
	{
		conn = ptr->data;
		if(conn->id == id)
			return conn;
	}
	return NULL;
}

static void
conn_add_id_hash(conn_t * conn, rb_uint16_t id)
{
	conn->id = id;
	rb_dlinkAdd(conn, &conn->node, connid_hash(id));
}


static void
close_conn(conn_t * conn)
{
	rb_rawbuf_flush(conn->modbuf_out, conn->mod_fd);
	rb_rawbuf_flush(conn->plainbuf_out, conn->plain_fd);
	rb_close(conn->mod_fd);
	rb_close(conn->plain_fd);

	rb_free_rawbuffer(conn->modbuf_out);
	rb_free_rawbuffer(conn->plainbuf_out);
	if(conn->id != 0)
		rb_dlinkDelete(&conn->node, connid_hash(conn->id));
	memset(conn, 0, sizeof(conn_t));
	rb_free(conn);
}

static conn_t *
make_conn(rb_fde_t * mod_fd, rb_fde_t * plain_fd)
{
	conn_t *conn = rb_malloc(sizeof(conn_t));
	conn->modbuf_out = rb_new_rawbuffer();
	conn->plainbuf_out = rb_new_rawbuffer();
	conn->mod_fd = mod_fd;
	conn->plain_fd = plain_fd;
	return conn;
}




static void
conn_mod_write_sendq(rb_fde_t * fd, void *data)
{
	conn_t *conn = data;
	int retlen;
	while ((retlen = rb_rawbuf_flush(conn->modbuf_out, fd)) > 0)
	{
		conn->mod_out += retlen;
	}
	if(retlen == 0 || (retlen < 0 && !rb_ignore_errno(errno)))
	{
		close_conn(data);
		return;
	}
	if(rb_rawbuf_length(conn->modbuf_out) > 0)
	{
		rb_setselect(conn->mod_fd, RB_SELECT_WRITE, conn_mod_write_sendq, conn);
	}
	else
	{
		rb_setselect(conn->mod_fd, RB_SELECT_WRITE, NULL, NULL);
	}
}

static void
conn_mod_write(conn_t * conn, void *data, size_t len)
{
	rb_rawbuf_append(conn->modbuf_out, data, len);
}

static void
conn_plain_write(conn_t * conn, void *data, size_t len)
{
	rb_rawbuf_append(conn->plainbuf_out, data, len);
}

static void
mod_cmd_write_queue(mod_ctl_t * ctl, void *data, size_t len)
{
	mod_ctl_buf_t *ctl_buf;
	ctl_buf = rb_malloc(sizeof(mod_ctl_buf_t));
	ctl_buf->buf = rb_malloc(len);
	ctl_buf->buflen = len;
	memcpy(ctl_buf->buf, data, len);
	ctl_buf->nfds = 0;
	rb_dlinkAddTail(ctl_buf, &ctl_buf->node, &ctl->writeq);
	mod_write_ctl(ctl->F, ctl);
}


#ifdef HAVE_LIBZ
static void
common_zlib_deflate(conn_t * conn, void *buf, size_t len)
{
	int ret, have;
	conn->outstream.next_in = buf;
	conn->outstream.avail_in = len;
	conn->outstream.next_out = (Bytef *) outbuf;
	conn->outstream.avail_out = sizeof(outbuf);

	ret = deflate(&conn->outstream, Z_SYNC_FLUSH);
	if(ret != Z_OK)
	{
		/* XXX deflate error */
	}
	if(conn->outstream.avail_out == 0)
	{
		/* XXX deal with avail_out being empty */
	}
	if(conn->outstream.avail_in != 0)
	{
		/* XXX deal with avail_in not being empty */
	}
	have = sizeof(outbuf) - conn->outstream.avail_out;
	conn_mod_write(conn, outbuf, have);
}

static void
common_zlib_inflate(conn_t * conn, void *buf, size_t len)
{
	int ret, have;
	conn->instream.next_in = buf;
	conn->instream.avail_in = len;
	conn->instream.next_out = (Bytef *) outbuf;
	conn->instream.avail_out = sizeof(outbuf);

	while (conn->instream.avail_in)
	{
		ret = inflate(&conn->instream, Z_NO_FLUSH);
		if(ret != Z_OK)
		{
			if(!strncmp("ERROR ", buf, 6))
			{
				/* XXX deal with error */
			}
			/* other error */
			close_conn(conn);
			return;
		}
		have = sizeof(outbuf) - conn->instream.avail_out;

		if(conn->instream.avail_in)
		{
			conn_plain_write(conn, outbuf, have);
			have = 0;
			conn->instream.next_out = (Bytef *) outbuf;
			conn->instream.avail_out = sizeof(outbuf);
		}
	}
	if(have == 0)
		return;

	conn_plain_write(conn, outbuf, have);
}
#endif

static void
conn_plain_read_cb(rb_fde_t * fd, void *data)
{
	conn_t *conn = data;
	int length = 0;
	if(conn == NULL)
		return;

	while ((length = rb_read(conn->plain_fd, inbuf, sizeof(inbuf))) > 0)
	{
		conn->plain_in += length;
#ifdef HAVE_LIBZ
		if(conn->is_zlib)
			common_zlib_deflate(conn, inbuf, length);
		else
#endif
			conn_mod_write(conn, inbuf, length);
	}
	if(length == 0 || (length < 0 && !rb_ignore_errno(errno)))
	{
		close_conn(conn);
		return;
	}

	rb_setselect(conn->plain_fd, RB_SELECT_READ, conn_plain_read_cb, conn);
	conn_mod_write_sendq(conn->mod_fd, conn);

}

static void
conn_mod_read_cb(rb_fde_t * fd, void *data)
{
	conn_t *conn = data;
	int length;
	if(conn == NULL)
		return;
	while ((length = rb_read(conn->mod_fd, inbuf, sizeof(inbuf))) > 0)
	{
		conn->mod_in += length;
#ifdef HAVE_LIBZ
		if(conn->is_zlib)
			common_zlib_inflate(conn, inbuf, length);
		else
#endif
			conn_plain_write(conn, inbuf, length);
	}
	if(length == 0 || (length < 0 && !rb_ignore_errno(errno)))
	{
		close_conn(conn);
		return;
	}
	rb_setselect(conn->mod_fd, RB_SELECT_READ, conn_mod_read_cb, conn);
	conn_plain_write_sendq(conn->plain_fd, conn);
}

static void
conn_plain_write_sendq(rb_fde_t * fd, void *data)
{
	conn_t *conn = data;
	int retlen;
	while ((retlen = rb_rawbuf_flush(conn->plainbuf_out, fd)) > 0)
	{
		conn->plain_out += retlen;
	}
	if(retlen == 0 || (retlen < 0 && !rb_ignore_errno(errno)))
	{
		close_conn(data);
		return;
	}

	if(rb_rawbuf_length(conn->plainbuf_out) > 0)
		rb_setselect(conn->plain_fd, RB_SELECT_WRITE, conn_plain_write_sendq, conn);
	else
		rb_setselect(conn->plain_fd, RB_SELECT_WRITE, NULL, NULL);
}


static void
mod_main_loop(void)
{
	while (1)
	{
		rb_select(1000);
		rb_event_run();
	}


}


static int
maxconn(void)
{
#if defined(RLIMIT_NOFILE) && defined(HAVE_SYS_RESOURCE_H)
	struct rlimit limit;

	if(!getrlimit(RLIMIT_NOFILE, &limit))
	{
		return limit.rlim_cur;
	}
#endif /* RLIMIT_FD_MAX */
	return MAXCONNECTIONS;
}

static void
ssl_process_accept_cb(rb_fde_t * F, int status, struct sockaddr *addr, rb_socklen_t len, void *data)
{
	conn_t *conn = data;
	if(status == RB_OK)
	{
		conn_mod_read_cb(conn->mod_fd, conn);
		conn_plain_read_cb(conn->plain_fd, conn);
		return;
	}
	close_conn(conn);
	return;
}

static void
ssl_process_connect_cb(rb_fde_t * F, int status, void *data)
{
	conn_t *conn = data;
	if(status == RB_OK)
	{
		conn_mod_read_cb(conn->mod_fd, conn);
		conn_plain_read_cb(conn->plain_fd, conn);
		return;
	}
	close_conn(conn);
	return;
}


static void
ssl_process_accept(mod_ctl_t * ctl, mod_ctl_buf_t * ctlb)
{
	conn_t *conn;
	rb_uint16_t id;
	conn = make_conn(ctlb->F[0], ctlb->F[1]);

	memcpy(&id, &ctlb->buf[1], sizeof(id));

	conn_add_id_hash(conn, id);
	conn->is_ssl = 1;

	if(rb_get_type(conn->mod_fd) == RB_FD_UNKNOWN)
		rb_set_type(conn->mod_fd, RB_FD_SOCKET);

	if(rb_get_type(conn->mod_fd) == RB_FD_UNKNOWN)
		rb_set_type(conn->plain_fd, RB_FD_SOCKET);

	rb_ssl_start_accepted(ctlb->F[0], ssl_process_accept_cb, conn, 10);
}

static void
ssl_process_connect(mod_ctl_t * ctl, mod_ctl_buf_t * ctlb)
{
	conn_t *conn;
	rb_uint16_t id;
	conn = make_conn(ctlb->F[0], ctlb->F[1]);


	conn_add_id_hash(conn, id);
	conn->is_ssl = 1;

	if(rb_get_type(conn->mod_fd) == RB_FD_UNKNOWN)
		rb_set_type(conn->mod_fd, RB_FD_SOCKET);

	if(rb_get_type(conn->mod_fd) == RB_FD_UNKNOWN)
		rb_set_type(conn->plain_fd, RB_FD_SOCKET);


	rb_ssl_start_connected(ctlb->F[0], ssl_process_connect_cb, conn, 10);
}

static void
process_stats(mod_ctl_t * ctl, mod_ctl_buf_t * ctlb)
{
	char outstat[512];
	conn_t *conn;
	const char *odata;
	rb_uint16_t id;

	memcpy(&id, &ctlb->buf[1], sizeof(id));

	odata = &ctlb->buf[3];
	conn = conn_find_by_id(id);

	if(conn == NULL)
		return;

	rb_snprintf(outstat, sizeof(outstat), "S %s %ld %ld %ld %ld", odata,
		    conn->plain_out, conn->mod_in, conn->mod_out, conn->plain_in);
	conn->plain_out = 0;
	conn->plain_in = 0;
	conn->mod_in = 0;
	conn->mod_out = 0;
	mod_cmd_write_queue(ctl, outstat, strlen(outstat) + 1);	/* +1 is so we send the \0 as well */
}

#ifdef HAVE_LIBZ
/* starts zlib for an already established connection */
static void
zlib_process_ssl(mod_ctl_t * ctl, mod_ctl_buf_t * ctlb)
{
	rb_uint16_t id;
	rb_uint8_t level;
	size_t hdr = (sizeof(rb_uint8_t) * 2) + sizeof(rb_uint16_t);
	conn_t *conn;
	void *leftover;

	memcpy(&id, &ctlb->buf[1], sizeof(id));
	level = (rb_uint8_t) ctlb->buf[3];

	conn = conn_find_by_id(id);
	if(conn == NULL)
	{
		return;
	}
	conn->is_zlib = 1;

	conn->instream.total_in = 0;
	conn->instream.total_out = 0;
	conn->instream.zalloc = (alloc_func) 0;
	conn->instream.zfree = (free_func) 0;
	conn->instream.data_type = Z_ASCII;
	inflateInit(&conn->instream);

	conn->outstream.total_in = 0;
	conn->outstream.total_out = 0;
	conn->outstream.zalloc = (alloc_func) 0;
	conn->outstream.zfree = (free_func) 0;
	conn->outstream.data_type = Z_ASCII;

	if(level > 9)
		level = Z_DEFAULT_COMPRESSION;

	deflateInit(&conn->outstream, level);

	if(ctlb->buflen > hdr)
	{
		leftover = ctlb->buf + hdr;
		common_zlib_inflate(conn, leftover, ctlb->buflen - hdr);
	}
	conn_mod_read_cb(conn->mod_fd, conn);
	conn_plain_read_cb(conn->plain_fd, conn);
	return;
}

static void
zlib_process(mod_ctl_t * ctl, mod_ctl_buf_t * ctlb)
{
	rb_uint16_t id;
	rb_uint8_t level;
	size_t hdr = (sizeof(rb_uint8_t) * 2) + sizeof(rb_uint16_t);
	conn_t *conn;
	void *leftover;
	conn = make_conn(ctlb->F[0], ctlb->F[1]);
	if(rb_get_type(conn->mod_fd) == RB_FD_UNKNOWN)
		rb_set_type(conn->mod_fd, RB_FD_SOCKET);

	if(rb_get_type(conn->plain_fd) == RB_FD_UNKNOWN)
		rb_set_type(conn->plain_fd, RB_FD_SOCKET);


	conn->is_ssl = 0;
	conn->is_zlib = 1;

	memcpy(&id, &ctlb->buf[1], sizeof(id));
	level = (rb_uint8_t)ctlb->buf[3];

	conn_add_id_hash(conn, id);

	conn->instream.total_in = 0;
	conn->instream.total_out = 0;
	conn->instream.zalloc = (alloc_func) 0;
	conn->instream.zfree = (free_func) 0;
	conn->instream.data_type = Z_ASCII;
	inflateInit(&conn->instream);

	conn->outstream.total_in = 0;
	conn->outstream.total_out = 0;
	conn->outstream.zalloc = (alloc_func) 0;
	conn->outstream.zfree = (free_func) 0;
	conn->outstream.data_type = Z_ASCII;

	if(level > 9)
		level = Z_DEFAULT_COMPRESSION;

	deflateInit(&conn->outstream, level);

	if(ctlb->buflen > hdr)
	{
		leftover = ctlb->buf + hdr;
		common_zlib_inflate(conn, leftover, ctlb->buflen - hdr);
	}
	conn_mod_read_cb(conn->mod_fd, conn);
	conn_plain_read_cb(conn->plain_fd, conn);
	return;
}
#endif

static void
ssl_new_keys(mod_ctl_t * ctl, mod_ctl_buf_t * ctl_buf)
{
	char *buf;
	char *cert, *key, *dhparam;
	buf = &ctl_buf->buf[2];
	cert = buf;
	buf += strlen(cert) + 1;
	key = buf;
	buf += strlen(key) + 1;
	dhparam = buf;
	if(strlen(dhparam) == 0)
		dhparam = NULL;

	if(!rb_setup_ssl_server(cert, key, dhparam))
	{
		/* XXX handle errors */
	}
}

static void
mod_process_cmd_recv(mod_ctl_t * ctl)
{
	rb_dlink_node *ptr, *next;
	mod_ctl_buf_t *ctl_buf;

	RB_DLINK_FOREACH_SAFE(ptr, next, ctl->readq.head)
	{
		ctl_buf = ptr->data;

		switch (*ctl_buf->buf)
		{
		case 'A':
			{
				ssl_process_accept(ctl, ctl_buf);
				break;
			}
		case 'C':
			{
				ssl_process_connect(ctl, ctl_buf);
				break;
			}

		case 'K':
			{
				ssl_new_keys(ctl, ctl_buf);
				break;
			}
		case 'S':
			{
				process_stats(ctl, ctl_buf);
				break;
			}
#ifdef HAVE_LIBZ
		case 'Y':
			{
				zlib_process_ssl(ctl, ctl_buf);
				break;
			}
		case 'Z':
			{
				/* just zlib only */
				zlib_process(ctl, ctl_buf);
				break;
			}
#endif
		default:
			break;
			/* Log unknown commands */
		}
		rb_dlinkDelete(ptr, &ctl->readq);
		rb_free(ctl_buf->buf);
		rb_free(ctl_buf);
	}

}



static void
mod_read_ctl(rb_fde_t * F, void *data)
{
	mod_ctl_buf_t *ctl_buf;
	mod_ctl_t *ctl = data;
	int retlen;

	do
	{
		ctl_buf = rb_malloc(sizeof(mod_ctl_buf_t));
		ctl_buf->buf = rb_malloc(READBUF_SIZE);
		ctl_buf->buflen = READBUF_SIZE;
		retlen = rb_recv_fd_buf(ctl->F, ctl_buf->buf, ctl_buf->buflen, ctl_buf->F,
					MAXPASSFD);
		if(retlen <= 0)
		{
			rb_free(ctl_buf->buf);
			rb_free(ctl_buf);
		}
		else
		{
			ctl_buf->buflen = retlen;
			rb_dlinkAddTail(ctl_buf, &ctl_buf->node, &ctl->readq);
		}
	}
	while (retlen > 0);

	if(retlen == 0 || (retlen < 0 && !rb_ignore_errno(errno)))
	{
		exit(0);
	}
	mod_process_cmd_recv(ctl);
	rb_setselect(ctl->F, RB_SELECT_READ, mod_read_ctl, ctl);
}

static void
mod_write_ctl(rb_fde_t * F, void *data)
{
	mod_ctl_t *ctl = data;
	mod_ctl_buf_t *ctl_buf;
	rb_dlink_node *ptr, *next;
	int retlen, x;

	RB_DLINK_FOREACH_SAFE(ptr, next, ctl->writeq.head)
	{
		ctl_buf = ptr->data;
		retlen = rb_send_fd_buf(ctl->F, ctl_buf->F, ctl_buf->nfds, ctl_buf->buf,
					ctl_buf->buflen);
		if(retlen > 0)
		{
			rb_dlinkDelete(ptr, &ctl->writeq);
			for (x = 0; x < ctl_buf->nfds; x++)
				rb_close(ctl_buf->F[x]);
			rb_free(ctl_buf->buf);
			rb_free(ctl_buf);

		}
		if(retlen == 0 || (retlen < 0 && !rb_ignore_errno(errno)))
		{
			/* deal with failure here */
		}
		else
		{
			rb_setselect(ctl->F, RB_SELECT_WRITE, mod_write_ctl, ctl);
		}
	}
}


static void
read_pipe_ctl(rb_fde_t * F, void *data)
{
	int retlen;
	while ((retlen = rb_read(F, inbuf, sizeof(inbuf))) > 0)
	{
		;;		/* we don't do anything with the pipe really, just care if the other process dies.. */
	}
	if(retlen == 0 || (retlen < 0 && !rb_ignore_errno(errno)))
		exit(0);
	rb_setselect(F, RB_SELECT_READ, read_pipe_ctl, NULL);

}

int
main(int argc, char **argv)
{
	const char *s_ctlfd, *s_pipe;
	int ctlfd, pipefd, x, maxfd;
	mod_ctl_t *ctl;
	maxfd = maxconn();
	s_ctlfd = getenv("CTL_FD");
	s_pipe = getenv("CTL_PIPE");

	if(s_ctlfd == NULL || s_pipe == NULL)
	{
		fprintf(stderr, "You aren't supposed to run me directly\n");
		exit(1);
		/* xxx fail */
	}

	ctlfd = atoi(s_ctlfd);
	pipefd = atoi(s_pipe);

	for (x = 0; x < maxfd; x++)
	{
		if(x != ctlfd && x != pipefd && x > 2)
			close(x);
	}
	rb_lib_init(NULL, NULL, NULL, 0, maxfd, 1024, 4096);
	rb_init_rawbuffers(1024);

	ctl = rb_malloc(sizeof(mod_ctl_t));
	ctl->F = rb_open(ctlfd, RB_FD_SOCKET, "ircd control socket");
	ctl->F_pipe = rb_open(pipefd, RB_FD_PIPE, "ircd pipe");
	read_pipe_ctl(ctl->F_pipe, NULL);
	mod_read_ctl(ctl->F, ctl);
	mod_main_loop();
	return 0;
}
