/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_challenge.c: Allows an IRC Operator to securely authenticate.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002-2005 ircd-ratbox development team
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 *
 *  $Id$
 */

#include "stdinc.h"

#ifdef HAVE_LIBCRYPTO
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/md5.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#endif

#include "struct.h"
#include "client.h"
#include "ircd.h"
#include "modules.h"
#include "numeric.h"
#include "send.h"
#include "s_conf.h"
#include "parse.h"
#include "irc_string.h"
#include "s_log.h"
#include "s_user.h"
#include "cache.h"
#include "s_newconf.h"

#ifndef HAVE_LIBCRYPTO
/* Maybe this should be an error or something?-davidt */
/* now it is	-larne	*/
static int	challenge_load(void)
{
#ifndef STATIC_MODULES
	sendto_realops_flags(UMODE_ALL, L_ALL, 
		"Challenge module not loaded because OpenSSL is not available.");
	ilog(L_MAIN, "Challenge module not loaded because OpenSSL is not available.");
	return -1;
#else
	return 0;
#endif
}

DECLARE_MODULE_AV1(challenge, challenge_load, NULL, NULL, NULL, NULL, "$Revision: 19295 $");
#else

static int m_challenge(struct Client *, struct Client *, int, const char **);

/* We have openssl support, so include /CHALLENGE */
struct Message challenge_msgtab = {
	"CHALLENGE", 0, 0, 0, MFLG_SLOW,
	{mg_unreg, {m_challenge, 2}, mg_ignore, mg_ignore, mg_ignore, {m_challenge, 2}}
};

mapi_clist_av1 challenge_clist[] = { &challenge_msgtab, NULL };
DECLARE_MODULE_AV1(challenge, NULL, NULL, challenge_clist, NULL, NULL, "$Revision: 19295 $");

static int generate_challenge(char **r_challenge, char **r_response, RSA * key);

/*
 * m_challenge - generate RSA challenge for wouldbe oper
 * parv[0] = sender prefix
 * parv[1] = operator to challenge for, or +response
 *
 */
static int
m_challenge(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct oper_conf *oper_p;
	char *challenge;

	/* if theyre an oper, reprint oper motd and ignore */
	if(IsOper(source_p))
	{
		sendto_one(source_p, POP_QUEUE, form_str(RPL_YOUREOPER), me.name, source_p->name);
		send_oper_motd(source_p);
		return 0;
	}

	if(*parv[1] == '+')
	{
		/* Ignore it if we aren't expecting this... -A1kmm */
		if(!source_p->localClient->response)
			return 0;

		if(irccmp(source_p->localClient->response, ++parv[1]))
		{
			sendto_one(source_p, POP_QUEUE, form_str(ERR_PASSWDMISMATCH), me.name, source_p->name);
			ilog(L_FOPER, "FAILED OPER (%s) by (%s!%s@%s)",
			     source_p->localClient->auth_oper, source_p->name,
			     source_p->username, source_p->host);

			if(ConfigFileEntry.failed_oper_notice)
				sendto_realops_flags(UMODE_ALL, L_ALL,
						     "Failed OPER attempt by %s (%s@%s)",
						     source_p->name, source_p->username,
						     source_p->host);
			return 0;
		}

		oper_p = find_oper_conf(source_p->username, source_p->host, 
					source_p->sockhost, 
					source_p->localClient->auth_oper);

		if(oper_p == NULL)
		{
			sendto_one(source_p, POP_QUEUE, form_str(ERR_NOOPERHOST), 
				   me.name, source_p->name);
			ilog(L_FOPER, "FAILED OPER (%s) by (%s!%s@%s)",
			     source_p->localClient->auth_oper, source_p->name,
			     source_p->username, source_p->host);

			if(ConfigFileEntry.failed_oper_notice)
				sendto_realops_flags(UMODE_ALL, L_ALL,
						     "Failed CHALLENGE attempt - host mismatch by %s (%s@%s)",
						     source_p->name, source_p->username,
						     source_p->host);
			return 0;
		}

		oper_up(source_p, oper_p);

		ilog(L_OPERED, "OPER %s by %s!%s@%s",
		     source_p->localClient->auth_oper, source_p->name, 
		     source_p->username, source_p->host);

		MyFree(source_p->localClient->response);
		MyFree(source_p->localClient->auth_oper);
		source_p->localClient->response = NULL;
		source_p->localClient->auth_oper = NULL;
		return 0;
	}

	MyFree(source_p->localClient->response);
	MyFree(source_p->localClient->auth_oper);
	source_p->localClient->response = NULL;
	source_p->localClient->auth_oper = NULL;

	oper_p = find_oper_conf(source_p->username, source_p->host, 
				source_p->sockhost, parv[1]);

	if(oper_p == NULL)
	{
		sendto_one(source_p, POP_QUEUE, form_str(ERR_NOOPERHOST), me.name, source_p->name);
		ilog(L_FOPER, "FAILED OPER (%s) by (%s!%s@%s)",
		     parv[1], source_p->name,
		     source_p->username, source_p->host);

		if(ConfigFileEntry.failed_oper_notice)
			sendto_realops_flags(UMODE_ALL, L_ALL,
					     "Failed CHALLENGE attempt - host mismatch by %s (%s@%s)",
					     source_p->name, source_p->username, source_p->host);
		return 0;
	}

	if(!oper_p->rsa_pubkey)
	{
		sendto_one(source_p, POP_QUEUE, ":%s NOTICE %s :I'm sorry, PK authentication "
			   "is not enabled for your oper{} block.", me.name, parv[0]);
		return 0;
	}

	if(!generate_challenge(&challenge, &(source_p->localClient->response), oper_p->rsa_pubkey))
	{
		sendto_one(source_p, POP_QUEUE, form_str(RPL_RSACHALLENGE), 
			   me.name, source_p->name, challenge);
	}

	DupString(source_p->localClient->auth_oper, oper_p->name);
	MyFree(challenge);
	return 0;
}

static void
binary_to_hex(unsigned char *bin, char *hex, int length)
{
	static const char trans[] = "0123456789ABCDEF";
	int i;

	for (i = 0; i < length; i++)
	{
		hex[i << 1] = trans[bin[i] >> 4];
		hex[(i << 1) + 1] = trans[bin[i] & 0xf];
	}
	hex[i << 1] = '\0';
}

static int
get_randomness(unsigned char *buf, int length)
{
	/* Seed OpenSSL PRNG with EGD enthropy pool -kre */
	if(ConfigFileEntry.use_egd && (ConfigFileEntry.egdpool_path != NULL))
	{
		if(RAND_egd(ConfigFileEntry.egdpool_path) == -1)
			return -1;
	}

	if(RAND_status())
	{
		if(RAND_bytes(buf, length) > 0)
			return 1;
	}
	else {
		if(RAND_pseudo_bytes(buf, length) >= 0)
			return 1;
	}
	return 0;
}

int
generate_challenge(char **r_challenge, char **r_response, RSA * rsa)
{
	unsigned char secret[32], *tmp;
	unsigned long length;
	unsigned long e = 0;
	unsigned long cnt = 0;
	int ret;

	if(!rsa)
		return -1;
	if(get_randomness(secret, 32))
	{
		*r_response = MyMalloc(65);
		binary_to_hex(secret, *r_response, 32);

		length = RSA_size(rsa);
		tmp = MyMalloc(length);
		ret = RSA_public_encrypt(32, secret, tmp, rsa, RSA_PKCS1_PADDING);
	
		*r_challenge = MyMalloc((length << 1) + 1);
		binary_to_hex(tmp, *r_challenge, length);
		(*r_challenge)[length << 1] = 0;
		MyFree(tmp);
		if(ret >= 0)
			return 0;
	}

	ERR_load_crypto_strings();
	while ((cnt < 100) && (e = ERR_get_error()))
	{
		ilog(L_MAIN, "SSL error: %s", ERR_error_string(e, 0));
		cnt++;
	}

	return (-1);
}

#endif /* HAVE_LIBCRYPTO */
