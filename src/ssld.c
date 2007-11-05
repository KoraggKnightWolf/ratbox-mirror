#include "stdinc.h"

#define MAXPASSFD 4

static void conn_ssl_write_sendq(rb_fde_t *, void *data);
static void conn_plain_write_sendq(rb_fde_t *, void *data);

typedef struct _ssl_ctl_buf
{
        rb_dlink_node node;
        char buf[512];
        rb_fde_t *F[MAXPASSFD];
        int nfds;
} ssl_ctl_buf_t; 


typedef struct _ssl_ctl
{
        rb_dlink_node node;
        int cli_count;
        rb_fde_t *F;  
        rb_fde_t *F_pipe;
        rb_dlink_list readq;
        rb_dlink_list writeq;
} ssl_ctl_t;

typedef struct _conn
{
        rb_dlink_node node;
        rawbuf_head_t *sslbuf_out;
        rawbuf_head_t *plainbuf_out;

        rb_fde_t *ssl_fd;
        rb_fde_t *plain_fd;
        unsigned long ssl_out;
        unsigned long ssl_in;
        unsigned long plain_in;
        unsigned long plain_out;
        int dead;
} conn_t;

static void
close_conn(conn_t * conn)
{
        rb_rawbuf_flush(conn->sslbuf_out, conn->ssl_fd);
        rb_rawbuf_flush(conn->plainbuf_out, conn->plain_fd);
        rb_close(conn->ssl_fd); 
        rb_close(conn->plain_fd);

        rb_free_rawbuffer(conn->sslbuf_out);
        rb_free_rawbuffer(conn->plainbuf_out);
        memset(conn, 0, sizeof(conn_t));
        rb_free(conn);
}

static conn_t *
make_conn(rb_fde_t *ssl_fd, rb_fde_t *plain_fd)
{
        conn_t *conn = rb_malloc(sizeof(conn_t));
        conn->sslbuf_out = rb_new_rawbuffer();
        conn->plainbuf_out = rb_new_rawbuffer();
        conn->ssl_fd = ssl_fd;
        conn->plain_fd = plain_fd;
        return conn;
}

static void
conn_ssl_write_sendq(rb_fde_t *fd, void *data)
{
        conn_t *conn = data;
        int retlen;
        while ((retlen = rb_rawbuf_flush(conn->sslbuf_out, fd)) > 0)
        {
                conn->ssl_out += retlen;
        }
        if(retlen == 0 || (retlen < 0 && !rb_ignore_errno(errno)))
        {
                close_conn(data);
                return;
        }
        if(rb_rawbuf_length(conn->sslbuf_out) > 0) {
                rb_setselect(conn->ssl_fd, RB_SELECT_WRITE, conn_ssl_write_sendq, conn);
        }
        else {
                rb_setselect(conn->ssl_fd, RB_SELECT_WRITE, NULL, NULL);
        }
}
 
static void
conn_ssl_write(conn_t * conn, void *data, size_t len)
{
        rb_rawbuf_append(conn->sslbuf_out, data, len);
}
 
static void
conn_plain_write(conn_t * conn, void *data, size_t len)
{
        rb_rawbuf_append(conn->plainbuf_out, data, len);
}

static void
conn_plain_read_cb(rb_fde_t *fd, void *data)
{
        conn_t *conn = data;
        int length = 0;
        char buf[1024];
        if(conn == NULL)
                return; 

        while ((length = rb_read(conn->plain_fd, buf, sizeof(buf))) > 0)
        {
                conn->plain_in += length;
                conn_ssl_write(conn, buf, length);
        }
        if(length == 0 || (length < 0 && !rb_ignore_errno(errno)))
        {
                close_conn(conn);
                return;
        }

        rb_setselect(conn->plain_fd, RB_SELECT_READ, conn_plain_read_cb, conn);
        conn_ssl_write_sendq(conn->ssl_fd, conn);

}

static void
conn_ssl_read_cb(rb_fde_t *fd, void *data)
{
        conn_t *conn = data;
        int length;
        char buf[1024];
        if(conn == NULL)
                return; 
        while ((length = rb_read(conn->ssl_fd, buf, sizeof(buf))) > 0)
        {
                conn->ssl_in += length;
                conn_plain_write(conn, buf, length);
        }
        if(length == 0 || (length < 0 && !rb_ignore_errno(errno)))
        {
                close_conn(conn);
                return;
        }
        rb_setselect(conn->ssl_fd, RB_SELECT_READ, conn_ssl_read_cb, conn);
        conn_plain_write_sendq(conn->plain_fd, conn);



}

static void
conn_plain_write_sendq(rb_fde_t *fd, void *data)
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
ssld_main_loop(void)
{
	while(1)
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
ssl_process_accept_cb(rb_fde_t *F, int status, struct sockaddr *addr, rb_socklen_t len, void *data)
{
	conn_t *conn = data;
	if(status == RB_OK)
	{
		conn_ssl_read_cb(conn->ssl_fd, conn);
		conn_plain_read_cb(conn->plain_fd, conn);
		return;
	}
	close_conn(conn);
}

static void
ssl_process_accept(ssl_ctl_buf_t *ctlb)
{
	conn_t *conn;
	conn = make_conn(ctlb->F[0], ctlb->F[1]);
	rb_ssl_start_accepted(ctlb->F[0], ssl_process_accept_cb, conn);
	return;
}

static void
ssl_process_connect(ssl_ctl_buf_t *ctlb)
{
	/* XXX write me */
	return;
}

static void
ssl_process_cmd_recv(ssl_ctl_t *ctl)
{
        rb_dlink_node *ptr, *next;      
        ssl_ctl_buf_t *ctl_buf;                 
        int parc;
        char *parv[16];
        RB_DLINK_FOREACH_SAFE(ptr, next, ctl->readq.head)
        {
                ctl_buf = ptr->data;
                parc = rb_string_to_array(ctl_buf->buf, parv, 15);

                switch(parv[0][0])
                {
                        case 'A':
                        {
				ssl_process_accept(ctl_buf);
                                break;
                        }
			case 'C':
			{
				ssl_process_connect(ctl_buf);
				break;
			}
                        default:
                                break;   
                                /* Log unknown commands */
                }                               
                rb_dlinkDelete(ptr, &ctl->readq);
//                rb_free(ctl_buf->buf);
                rb_free(ctl_buf);
        }

}



static void
ssl_read_ctl(rb_fde_t *F, void *data)  
{
        ssl_ctl_buf_t *ctl_buf;
        ssl_ctl_t *ctl = data;
        int retlen;

        do
        {
                ctl_buf = rb_malloc(sizeof(ssl_ctl_buf_t));

                retlen = rb_recv_fd_buf(ctl->F, ctl_buf->buf, sizeof(ctl_buf->buf), ctl_buf->F, MAXPASSFD);
                if(retlen <= 0) 
                        rb_free(ctl_buf);
                else
                        rb_dlinkAddTail(ctl_buf, &ctl_buf->node, &ctl->readq);
        } while(retlen > 0);    

        if(retlen == 0 || (retlen < 0 && !rb_ignore_errno(errno)))
        {
		exit(0);        	
        }
        ssl_process_cmd_recv(ctl);
        rb_setselect(ctl->F, RB_SELECT_READ, ssl_read_ctl, ctl);
}

static void
ssl_write_ctl(rb_fde_t *F, void *data)
{
        ssl_ctl_t *ctl = data;
        ssl_ctl_buf_t *ctl_buf;
        rb_dlink_node *ptr, *next;
        int retlen, x;

        RB_DLINK_FOREACH_SAFE(ptr, next, ctl->writeq.head)
        {
                ctl_buf = ptr->data;

                retlen = rb_send_fd_buf(ctl->F, ctl_buf->F, ctl_buf->nfds,  ctl_buf->buf, strlen(ctl_buf->buf));
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
                        /* deal with failure here */
                } else  {
                        rb_setselect(ctl->F, RB_SELECT_WRITE, ssl_write_ctl, ctl);
                }
        }
}
#if 0
static void
ssl_cmd_write_queue(ssl_ctl_t *ctl, rb_fde_t **F, int count, const char *buf)
{
        ssl_ctl_buf_t *ctl_buf;
        int x;  
        ctl_buf = rb_malloc(sizeof(ssl_ctl_buf_t));
        rb_strlcpy(ctl_buf->buf, buf, sizeof(ctl_buf->buf));

        for(x = 0; x < count && x < MAXPASSFD; x++)
        {
                ctl_buf->F[x] = F[x];
        }
        rb_dlinkAddTail(ctl_buf, &ctl_buf->node, &ctl->readq);

}
#endif

static void
read_pipe_ctl(rb_fde_t *F, void *data)
{
	char buf[512];
	int retlen;
	while((retlen = rb_read(F, buf, sizeof(buf))) > 0)
	{
		;; /* we don't do anything with the pipe really, just care if the other process dies.. */
	}
	if(retlen == 0 || (retlen < 0 && !rb_ignore_errno(errno)))
		exit(0);
	rb_setselect(F, RB_SELECT_READ, read_pipe_ctl, NULL); 
	
}

int main(int argc, char **argv)
{
	ssl_ctl_t *ctl;
	const char *s_ctlfd, *s_pipe;
	const char *ssl_cert, *ssl_private_key, *ssl_dh_params;
	int ctlfd, pipefd, x, maxfd;

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
	ssl_cert = getenv("SSL_CERT");
	ssl_private_key = getenv("SSL_PRIVATE_KEY");
	ssl_dh_params = getenv("SSL_DH_PARAMS");
		
	for(x = 0; x < maxfd; x++)
	{
		if(x != ctlfd && x != pipefd && x > 2)
			close(x);
	}
	rb_lib_init(NULL, NULL, NULL, 0, maxfd, 1024, 1024, 4096);
	rb_init_rawbuffers(1024);
	rb_setup_ssl_server(ssl_cert, ssl_private_key, ssl_dh_params);
	ctl = rb_malloc(sizeof(ssl_ctl_t));
	ctl->F = rb_open(ctlfd, RB_FD_SOCKET, "ircd control socket");
	ctl->F_pipe = rb_open(pipefd, RB_FD_PIPE, "ircd pipe");	
	read_pipe_ctl(ctl->F_pipe, NULL);
	ssl_read_ctl(ctl->F, ctl);
	ssld_main_loop();	
	return 0;
} 


