#ifndef INCLUDED_banconf_h
#define INCLUDED_banconf_h

typedef enum
{
	TRANS_KLINE,
	TRANS_DLINE,
	TRANS_XLINE,
	TRANS_RESV
} banconf_type;

void banconf_add_write(banconf_type type, struct Client *source_p, const char *mask,
			const char *mask2, const char *reason, const char *oper_reason);
void banconf_del_write(banconf_type type, const char *mask, const char *mask2);

#endif
