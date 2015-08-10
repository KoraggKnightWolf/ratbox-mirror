#include "stdinc.h"
#include "ratbox_lib.h"
#include "struct.h"
#include "modules.h"
#include "parse.h"
#include "client.h"
#include "ircd.h"
#include "send.h"

#include <malloc.h>

static int moper_mallinfo(struct Client *client_p, struct Client *source_p, int parc,
		      const char *parv[]);


struct Message mallinfo_msgtab = {
	.cmd = "MALLINFO",			
	.handlers[OPER_HANDLER] = 		{ .handler = moper_mallinfo,	.min_para = 0 },
};

mapi_clist_av1 mallinfo_clist[] = { &mallinfo_msgtab, NULL };


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
			  mallinfo,
			  /* The second argument is the function to call on load */
			  modinit,
			  /* And the function to call on unload */
			  moddeinit,
			  /* Then the MAPI command list */
			  mallinfo_clist,
			  /* Next the hook list, if we have one. */
			  NULL,
			  /* Then the hook function list, if we have one */
			  NULL,
			  /* And finally the version number of this module. */
			  "$Revision: 27947 $");

static int
moper_mallinfo(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{

	struct mallinfo mi;
	
	mi = mallinfo();

	sendto_one_notice(source_p, ":Total non-mmapped bytes (arena):       %d\n", mi.arena);
	sendto_one_notice(source_p, ":# of free chunks (ordblks):            %d\n", mi.ordblks);
	sendto_one_notice(source_p, ":# of free fastbin blocks (smblks):     %d\n", mi.smblks);
	sendto_one_notice(source_p, ":# of mapped regions (hblks):           %d\n", mi.hblks);
	sendto_one_notice(source_p, ":Bytes in mapped regions (hblkhd):      %d\n", mi.hblkhd);
	sendto_one_notice(source_p, ":Max. total allocated space (usmblks):  %d\n", mi.usmblks);
	sendto_one_notice(source_p, ":Free bytes held in fastbins (fsmblks): %d\n", mi.fsmblks);
	sendto_one_notice(source_p, ":Total allocated space (uordblks):      %d\n", mi.uordblks);
	sendto_one_notice(source_p, ":Total free space (fordblks):           %d\n", mi.fordblks);
	sendto_one_notice(source_p, ":Topmost releasable block (keepcost):   %d\n", mi.keepcost);
	return 0;
}
