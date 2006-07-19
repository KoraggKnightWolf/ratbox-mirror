/*
 *  ircd-ratbox: A slightly useful ircd.
 *  ircd_lib.c: libircd initialization functions at the like
 *
 *  Copyright (C) 2005 ircd-ratbox development team
 *  Copyright (C) 2005 Aaron Sethman <androsyn@ratbox.org>
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

#include "ircd_lib.h"

static log_cb *ircd_log;
static restart_cb *ircd_restart;
static die_cb *ircd_die;

static struct timeval *ircd_time;


static char errbuf[512];
char *
ircd_ctime(const time_t t, char *buf)
{
	char *p;
#ifdef HAVE_CTIME_R
	if(unlikely((p = ctime_r(&t, buf)) == NULL))
#else
	if(unlikely((p = ctime(&t)) == NULL))
#endif
		return NULL;
	{
		strcpy(buf, "");
		return buf;
	}
	strcpy(buf, p);

	p = strchr(buf, '\n');
	if(p != NULL)
		*p = '\0';

	return buf;
}

time_t
ircd_current_time(void)
{
	if(ircd_time == NULL)
		ircd_set_time();
	return ircd_time->tv_sec;
}

struct timeval *
ircd_current_time_tv(void)
{
	if(ircd_time == NULL)
		ircd_set_time();
	return ircd_time;	
}

void
ircd_lib_log(const char *format, ...)
{
	va_list args;
	if(ircd_log == NULL)
		return;
	va_start(args, format);
	ircd_vsnprintf(errbuf, sizeof(errbuf), format,  args);
	va_end(args);
	ircd_log(errbuf);
}

void
ircd_lib_die(const char *format, ...)
{
	va_list args;
	if(ircd_die == NULL)
		return;
	va_start(args, format);
	ircd_vsnprintf(errbuf, sizeof(errbuf), format,  args);
	va_end(args);
	ircd_die(errbuf);
}

void
ircd_lib_restart(const char *format, ...)
{
	va_list args;
	if(ircd_restart == NULL)
		return;
	va_start(args, format);
	ircd_vsnprintf(errbuf, sizeof(errbuf), format,  args);
	va_end(args);
	ircd_restart(errbuf);
}


void
ircd_set_time(void)
{
	struct timeval newtime;
	newtime.tv_sec = 0;
	newtime.tv_usec = 0;
	if(ircd_time == NULL)
	{
		ircd_time = ircd_malloc(sizeof(struct timeval));
	}
	if(ircd_gettimeofday(&newtime, NULL) == -1)
	{
		ircd_lib_log("Clock Failure (%s)", strerror(errno));
		ircd_lib_restart("Clock Failure");
	}

	if(newtime.tv_sec < ircd_time->tv_sec)
		ircd_set_back_events(ircd_time->tv_sec - newtime.tv_sec);

	memcpy(ircd_time, &newtime, sizeof(struct timeval));
}


void
ircd_lib(log_cb *ilog, restart_cb *irestart, die_cb *idie, int closeall, int maxcon, size_t lb_heap_size, size_t dh_size)
{
	ircd_log = ilog;
	ircd_restart = irestart;
	ircd_die = idie;
	ircd_event_init();
	ircd_init_bh();
	ircd_fdlist_init(closeall, maxcon);
	init_netio();
	init_dlink_nodes(dh_size);
	ircd_linebuf_init(lb_heap_size);
}


#ifndef HAVE_STRTOK_R
char *
strtok_r (char *s, const char *delim, char **save)
{
	char *token;

	if (s == NULL)
		s = *save;

	/* Scan leading delimiters.  */
	s += strspn(s, delim);

	if (*s == '\0')
	{
		*save = s;
		return NULL;
	}

	token = s;
	s = strpbrk(token, delim);
	
	if (s == NULL)  
		*save = (token + strlen(token));
	else
	{
		*s = '\0'; 
		*save = s + 1;
	}
	return token;
}
#endif


static const char base64_table[] =
	{ 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
	  'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
	  'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
	  'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
	  '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/', '\0'
	};

static const char base64_pad = '=';

static const short base64_reverse_table[256] = {
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
	52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
	-1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
	15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
	-1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
	41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

unsigned char *
ircd_base64_encode(const unsigned char *str, int length)
{
	const unsigned char *current = str;
	unsigned char *p;
	unsigned char *result;

	if ((length + 2) < 0 || ((length + 2) / 3) >= (1 << (sizeof(int) * 8 - 2))) {
		return NULL;
	}

	result = ircd_malloc(((length + 2) / 3) * 5);
	p = result;

	while (length > 2) 
	{ 
		*p++ = base64_table[current[0] >> 2];
		*p++ = base64_table[((current[0] & 0x03) << 4) + (current[1] >> 4)];
		*p++ = base64_table[((current[1] & 0x0f) << 2) + (current[2] >> 6)];
		*p++ = base64_table[current[2] & 0x3f];

		current += 3;
		length -= 3; 
	}

	if (length != 0) {
		*p++ = base64_table[current[0] >> 2];
		if (length > 1) {
			*p++ = base64_table[((current[0] & 0x03) << 4) + (current[1] >> 4)];
			*p++ = base64_table[(current[1] & 0x0f) << 2];
			*p++ = base64_pad;
		} else {
			*p++ = base64_table[(current[0] & 0x03) << 4];
			*p++ = base64_pad;
			*p++ = base64_pad;
		}
	}
	*p = '\0';
	return result;
}

unsigned char *
ircd_base64_decode(const unsigned char *str, int length, int *ret)
{
	const unsigned char *current = str;
	int ch, i = 0, j = 0, k;
	unsigned char *result;
	
	result = ircd_malloc(length + 1);

	while ((ch = *current++) != '\0' && length-- > 0) {
		if (ch == base64_pad) break;

		ch = base64_reverse_table[ch];
		if (ch < 0) continue;

		switch(i % 4) {
		case 0:
			result[j] = ch << 2;
			break;
		case 1:
			result[j++] |= ch >> 4;
			result[j] = (ch & 0x0f) << 4;
			break;
		case 2:
			result[j++] |= ch >>2;
			result[j] = (ch & 0x03) << 6;
			break;
		case 3:
			result[j++] |= ch;
			break;
		}
		i++;
	}

	k = j;

	if (ch == base64_pad) {
		switch(i % 4) {
		case 1:
			free(result);
			return NULL;
		case 2:
			k++;
		case 3:
			result[k++] = 0;
		}
	}
	result[j] = '\0';
	*ret = j;
	return result;
}


