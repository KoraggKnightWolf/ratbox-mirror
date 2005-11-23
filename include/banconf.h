#ifndef INCLUDED_banconf_h
#define INCLUDED_banconf_h

#define SAFE_PRINT(x) (EmptyString((x)) ? "" : (x))

void banconf_parse(void);

void banconf_parse_kline(char *line, int perm);
void banconf_parse_dline(char *line, int perm);
void banconf_parse_xline(char *line, int perm);
void banconf_parse_resv(char *line, int perm);

void banconf_write(void);

#endif
