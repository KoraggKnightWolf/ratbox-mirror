#ifndef INCLUDED_translog_h
#define INCLUDED_translog_h

typedef enum
{
	TRANS_KLINE,
	TRANS_DLINE,
	TRANS_XLINE,
	TRANS_RESV
} translog_type;

void translog_add_ban(translog_type type, struct Client *source_p, const char *mask,
			const char *mask2, const char *reason, const char *oper_reason);
void translog_del_ban(translog_type type, const char *mask, const char *mask2);

#endif
