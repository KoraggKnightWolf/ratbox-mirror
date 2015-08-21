#include <stdinc.h>
#include <ratbox_lib.h>
#include <struct.h>
#include <modules.h>
#include <parse.h>
#include <client.h>
#include <ircd.h>
#include <send.h>

static int moper_reportssl(struct Client *client_p, struct Client *source_p, int parc,
		      const char *parv[]);


struct Message reportssl_msgtab = {
	.cmd = "REPORTSSL",			
	.handlers[OPER_HANDLER] = 		{ .handler = moper_reportssl,	.min_para = 0 },
        .handlers[UNREGISTERED_HANDLER] =       { mm_ignore }, 
        .handlers[CLIENT_HANDLER] =             { mm_ignore },
        .handlers[RCLIENT_HANDLER] =            { mm_ignore },
        .handlers[SERVER_HANDLER] =             { mm_ignore },
        .handlers[ENCAP_HANDLER] =              { mm_ignore },
};

mapi_clist_av1 reportssl_clist[] = { &reportssl_msgtab, NULL };


static int
modinit(void)
{
	return 0;
}

/* here we tell it what to do when the module is unloaded */
static void
moddeinit(void)
{
}

/* DECLARE_MODULE_AV1() actually declare the MAPI header. */
DECLARE_MODULE_AV1(
			  /* The first argument is the name */
			  reportssl,
			  /* The second argument is the function to call on load */
			  modinit,
			  /* And the function to call on unload */
			  moddeinit,
			  /* Then the MAPI command list */
			  reportssl_clist,
			  /* Next the hook list, if we have one. */
			  NULL,
			  /* Then the hook function list, if we have one */
			  NULL,
			  /* And finally the version number of this module. */
			  "$Revision: 27947 $");

static int
moper_reportssl(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	rb_dlink_node *ptr, *next;
	RB_DLINK_FOREACH_SAFE(ptr, next, lclient_list.head)
	{
		struct Client *cliptr = (struct Client *)ptr->data;
		if(IsSSL(cliptr) && cliptr->localClient != NULL && cliptr->localClient->cipher_string != NULL)
		{
			sendto_one_notice(source_p, ":Name: %s Cipher: %s", cliptr->name, cliptr->localClient->cipher_string);
		}
	}
	return 0;
}
