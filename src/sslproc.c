#include <ratbox_lib.h>
#include "stdinc.h"
#include "s_conf.h"
#include "s_log.h"
#include "listener.h"
#include "struct.h"
#include "sslproc.h"

/* 

Spawns new process.
ircd binds to port, and passes fds to all children. 
child listeners do the accept
child creates a new socketpair and hands it back to the ircd


COMMANDS: 
# L - associated FD should be used to accept connections
A - start ssl accept on fd
C - start ssl connect on fd
	
 */

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


typedef struct _ssl_ctl
{
	rb_dlink_node node;
	int cli_count;
	rb_fde_t *F;
	int pid;
	rb_dlink_list readq;
	rb_dlink_list writeq;
} ssl_ctl_t;



static rb_dlink_list ssl_daemons;



static ssl_ctl_t *
allocate_ssl_daemon(rb_fde_t *F, int pid)
{
	ssl_ctl_t *ctl;
	
	if(F == NULL || pid < 0)
		return NULL;
	ctl = rb_malloc(sizeof(ssl_ctl_t));	
	ctl->F = F;
	ctl->pid = pid;
	rb_dlinkAdd(ctl, &ctl->node, &ssl_daemons);
	return ctl;
}

static char *ssld_path;

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
		rb_socketpair(AF_UNIX, SOCK_DGRAM, 0, &F1, &F2, "SSL/TLS handle passing socket");
		rb_set_buffers(F1, READBUF_SIZE);
		rb_set_buffers(F2, READBUF_SIZE);
		rb_snprintf(fdarg, sizeof(fdarg), "%d", rb_get_fd(F2));
		setenv("CTL_FD", fdarg, 1);
		rb_pipe(&P1, &P2, "SSL/TLS pipe");
		rb_snprintf(fdarg, sizeof(fdarg), "%d", rb_get_fd(P1));
		setenv("CTL_PIPE", fdarg, 1);
		setenv("SSL_CERT", ssl_cert, 1);
		setenv("SSL_PRIVATE_KEY", ssl_private_key, 1);
		if(ssl_dh_params != NULL)
			setenv("SSL_DH_PARAMS", ssl_dh_params, 1);

		
		pid = rb_spawn_process(fullpath, (const char **)parv);
		if(pid == -1)
		{
			ilog(L_MAIN, "Unable to create ssld: %s\n", strerror(errno));
			rb_close(F1);
			rb_close(F2);
			return started;
		}
		started++;
		rb_close(F2);
		rb_close(P1);
		allocate_ssl_daemon(F1, pid);
	}
	return started;	
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
		if(parc != 5)
		{
			/* xxx fail */
		}
		/*	N remote_ipaddress local_ipaddress remoteport local_port listenerid */

		switch(parv[0][0])
		{
			case 'N':
			{
//				ssl_process_incoming_conn(parv, parc, ctl_buf->F);
				break;
			}
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
ssl_read_ctl(rb_fde_t *F, void *data)
{
	ssl_ctl_buf_t *ctl_buf;
	ssl_ctl_t *ctl = data;
	int retlen;

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

	if(retlen == 0 || (retlen < 0 || !rb_ignore_errno(errno)))
	{
		/* deal with helper dying */
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
			/* deal with failure here */
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


void 
start_ssld_accept(rb_fde_t *sslF, rb_fde_t *plainF)
{
	rb_fde_t *F[2];
	static const char *cmd = "A";
	F[0] = sslF;
	F[1] = plainF;
	
	ssl_cmd_write_queue(which_ssld(), F, 2, cmd, strlen(cmd));
	return; 
}

void 
start_ssld_connect(rb_fde_t *sslF, rb_fde_t *plainF)
{
	rb_fde_t *F[2];
	static const char *cmd = "C";
	F[0] = sslF;
	F[1] = plainF;
	
	ssl_cmd_write_queue(which_ssld(), F, 2, cmd, strlen(cmd));
	return; 
}


/* 
 * what we end up sending to the ssld process for ziplinks is the following
 * Z[ourfd][RECVQ]
 * Z = ziplinks command
 * ourfd = Our end of the socketpair
 * recvq = any data we read prior to starting ziplinks
 */
void
start_zlib_session(struct Client *server)
{
	rb_fde_t *F[2];
	rb_fde_t *xF1, *xF2;
	rb_uint8_t *buf;
	rb_uint16_t *id;
	size_t hdr = sizeof(rb_uint8_t) + sizeof(rb_uint16_t);
	size_t len;

	len = rb_linebuf_len(&server->localClient->buf_recvq);
	fprintf(stderr, "len now: %d\n", len);
	len += hdr;	
	fprintf(stderr, "here %d\n", len);	
	if(len > READBUF_SIZE)
	{
		/* XXX deal with this */
		
	}
	buf = rb_malloc(len);
	*buf = 'Z';
	rb_linebuf_get(&server->localClient->buf_recvq, (char *)(buf+hdr), len - hdr, LINEBUF_PARTIAL, LINEBUF_RAW); 
	
	rb_socketpair(AF_UNIX, SOCK_STREAM, 0, &xF1, &xF2, "Initial zlib socketpairs");
	
	F[0] = server->localClient->F; 
	F[1] = xF1;
	server->localClient->F = xF2;
	id = (int *)&buf[1];
	*id = rb_get_fd(server->localClient->F);
	fprintf(stderr, "Sending ID: %d\n", *id);
	ssl_cmd_write_queue(which_ssld(), F, 2, buf, len);
	rb_free(buf);
}


