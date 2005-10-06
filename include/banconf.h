#ifndef INCLUDED_banconf_h
#define INCLUDED_banconf_h

void banconf_parse(void);

void banconf_parse_kline(char *line);
void banconf_parse_dline(char *line);
void banconf_parse_xline(char *line);
void banconf_parse_resv(char *line);

#endif
