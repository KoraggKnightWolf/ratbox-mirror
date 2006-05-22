/*
 * snprintf.c: libircd's sprintf functions
 *
 * Copyright (C) 2006 Aaron Sethman <androsyn@ratbox.org>
 * Copyright (C) 2006 ircd-ratbox development team
 *
 * And the original copyright on the snprintf is below
 *
 * libString, Copyright (C) 1999 Patrick Alken
 *
 * This library comes with absolutely NO WARRANTY
 *
 * Should you choose to use and/or modify this source code, please
 * do so under the terms of the GNU General Public License under which
 * this library is distributed.
 *
 * $Id$
 */

#include "ircd_lib.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>


static const char HexLower[] = "0123456789abcdef"; 
static const char HexUpper[] = "0123456789ABCDEF"; 

/* large enough to hold a 64bit unsigned long...*/
#define TEMPBUF_MAX  20 

static char TempBuffer[TEMPBUF_MAX];

/* okay so these macros are code fragments that deal with the mangling
 * of numbers to characters, this does make sense I promise.. -aaron
 */  

#define HEX_TO_STRN(type, table) {				\
								\
	type num = va_arg(args, type);				\
	char *digitptr = TempBuffer;				\
	do 							\
	{							\
		*digitptr++ = table[num & 0xF];			\
		num >>= 4;					\
	} while(num != 0);					\
								\
	while (*(digitptr - 1) == '0') --digitptr;		\
								\
	while (digitptr != TempBuffer)				\
	{							\
		*dest++ = *--digitptr;				\
		if(++written >= maxbytes)			\
			break;					\
	}							\
	continue;						\
}		
		

#define HEX_TO_STR(type,table) {	 			\
								\
	type num = va_arg(args, type);				\
	char *digitptr = TempBuffer;				\
	do 							\
	{							\
		*digitptr++ = table[num & 0xF];			\
		num >>= 4;					\
	} while(num != 0);					\
								\
	while (*(digitptr - 1) == '0')				\
		--digitptr;					\
								\
	while (digitptr != TempBuffer)				\
	{							\
		*dest++ = *--digitptr;				\
		++written;					\
	}							\
	continue;						\
}		

#ifdef TO_CHAR
#undef TO_CHAR
#endif
#define TO_CHAR(n)      ((n) + '0')

#define TYPE_TO_STR(type) {	 				\
								\
	type num = va_arg(args, type);				\
	char *digitptr = TempBuffer;				\
	if(num < 10)						\
	{							\
		*dest++ = TO_CHAR(num);				\
		++written;					\
		continue;					\
	}							\
	do 							\
	{							\
		*digitptr++ = TO_CHAR(num % 10);		\
		num /= 10;					\
	} while(num != 0);					\
								\
	while (*(digitptr - 1) == '0')				\
		--digitptr;					\
								\
	while (digitptr != TempBuffer)				\
	{							\
		*dest++ = *--digitptr;				\
		++written;					\
	}							\
	continue;						\
}

#define TYPE_TO_STRN(type) {	 				\
								\
	type num = va_arg(args, type);				\
	char *digitptr = TempBuffer;				\
	if(num < 10)						\
	{							\
		*dest++ = TO_CHAR(num);				\
		++written;					\
		continue;					\
	}							\
	do 							\
	{							\
		*digitptr++ = TO_CHAR(num % 10);		\
		num /= 10;					\
	} while(num != 0);					\
								\
	while (*(digitptr - 1) == '0')				\
		--digitptr;					\
								\
	while (digitptr != TempBuffer)				\
	{							\
		*dest++ = *--digitptr;				\
		if(++written >= maxbytes)			\
			break;					\
	}							\
	continue;						\
}

#define TYPE_TO_STRN(type) {	 				\
								\
	type num = va_arg(args, type);				\
	char *digitptr = TempBuffer;				\
	if(num < 10)						\
	{							\
		*dest++ = TO_CHAR(num);				\
		++written;					\
		continue;					\
	}							\
	do 							\
	{							\
		*digitptr++ = TO_CHAR(num % 10);		\
		num /= 10;					\
	} while(num != 0);					\
								\
	while (*(digitptr - 1) == '0')				\
		--digitptr;					\
								\
	while (digitptr != TempBuffer)				\
	{							\
		*dest++ = *--digitptr;				\
		if(++written >= maxbytes)			\
			break;					\
	}							\
	continue;						\
}

#define TYPE_TO_STR_SIGNED(type) {	 			\
								\
	type num = va_arg(args, type);				\
	char *digitptr = TempBuffer;				\
	if(num < 0)						\
	{							\
		*dest++ = '-';					\
		++written;					\
		num = -num;					\
	}							\
	if(num < 10)						\
	{							\
		*dest++ = TO_CHAR(num);				\
		++written;					\
		continue;					\
	}							\
	do 							\
	{							\
		*digitptr++ = TO_CHAR(num % 10);		\
		num /= 10;					\
	} while(num != 0);					\
								\
	while (*(digitptr - 1) == '0')				\
		--digitptr;					\
								\
	while (digitptr != TempBuffer)				\
	{							\
		*dest++ = *--digitptr;				\
		++written;					\
	}							\
	continue;						\
}

#define TYPE_TO_STRN_SIGNED(type) {	 			\
								\
	type num = va_arg(args, type);				\
	char *digitptr = TempBuffer;				\
	if(num < 0)						\
	{							\
		*dest++ = '-';					\
		++written;					\
		num = -num;					\
	}							\
	if(num < 10)						\
	{							\
		*dest++ = TO_CHAR(num);				\
		++written;					\
		continue;					\
	}							\
	do 							\
	{							\
		*digitptr++ = TO_CHAR(num % 10);		\
		num /= 10;					\
	} while(num != 0);					\
								\
	while (*(digitptr - 1) == '0')				\
		--digitptr;					\
								\
	while (digitptr != TempBuffer)				\
	{							\
		*dest++ = *--digitptr;				\
		if(++written >= maxbytes)			\
			break;					\
	}							\
	continue;						\
}


/*
vSnprintf()
 Backend to Snprintf() - performs the construction of 'dest'
using the string 'format' and the given arguments. Also makes sure
not more than 'bytes' characters are copied to 'dest'

 We always allow room for a terminating \0 character, so at most,
bytes - 1 characters will be written to dest.

Return: Number of characters written, NOT including the terminating
        \0 character which is *always* placed at the end of the string

NOTE: This function handles the following flags only:
        %s %d %c %u %ld %lu %x %X %lx %lX
      In addition, this function performs *NO* precision, padding,
      or width formatting. If it receives an unknown % character,
      it will call vsprintf() to complete the remainder of the
      string.
*/

int
ircd_vsnprintf(char *dest, const size_t bytes, const char *format, va_list args)
{
	char ch;
	int written = 0;	/* bytes written so far */
	int maxbytes = bytes - 1;

	while ((ch = *format++) && (written < maxbytes))
	{
		if(ch == '%')
		{
			/*
			 * Advance past the %
			 */
			ch = *format++;

			/*
			 * Put the most common cases first - %s %d etc
			 */

			if(ch == 's')
			{
				const char *str = va_arg(args, const char *);

				while ((*dest = *str))
				{
					++dest;
					++str;

					if(++written >= maxbytes)
						break;
				}

				continue;
			}

			if(ch == 'd')
				TYPE_TO_STRN_SIGNED(int);

			if(ch == 'c')
			{
				*dest++ = va_arg(args, int);
				++written;
				continue;
			}	/* if (ch == 'c') */

			if(ch == 'u')
				TYPE_TO_STRN(unsigned int);

			if(ch == 'x')
				HEX_TO_STRN(unsigned int, HexLower);

			if(ch == 'X')
				HEX_TO_STRN(unsigned int, HexUpper);
                        
			if(ch == 'l')
			{
				if(*format == 'u') {
					format++;
					TYPE_TO_STRN(unsigned long);
		                }

				if(*format == 'x') 
				{
					format++;
					HEX_TO_STRN(unsigned long, HexLower);
				}

				if(*format == 'X') 
				{
					format++;
					HEX_TO_STRN(unsigned long, HexUpper);
				}
				
				if(*format == 'd') 
				{
					format++;
					TYPE_TO_STRN_SIGNED(long);
				}
			}	/* if (ch == 'l') */

			if(ch != '%')
			{
				int ret;

				/*
				 * The character might be invalid, or be a precision, 
				 * padding, or width specification - call vsprintf()
				 * to finish off the string
				 */

				format -= 2;
                                #ifdef HAVE_VSNPRINTF
                                	ret = vsnprintf(dest, maxbytes - written, format, args);
                                #else
                                	ret = vsprintf(dest, format, args);
				#endif
				dest += ret;
				written += ret;

				break;
			}	/* if (ch != '%') */
		}		/* if (ch == '%') */

		*dest++ = ch;
		++written;
	}			/* while ((ch = *format++) && (written < maxbytes)) */

	/*
	 * Terminate the destination buffer with a \0
	 */
	*dest = '\0';

	return (written);
}				/* vSnprintf() */

/*
ircd_vsprintf()
 Backend to Sprintf() - performs the construction of 'dest'
using the string 'format' and the given arguments.

 We always place a \0 character onto the end of 'dest'.

Return: Number of characters written, NOT including the terminating
        \0 character which is *always* placed at the end of the string

NOTE: This function handles the following flags only:
        %s %d %c %u %ld %lu %x %X %lx %lX
      In addition, this function performs *NO* precision, padding,
      or width formatting. If it receives an unknown % character,
      it will call vsprintf() to complete the remainder of the
      string.
*/

int
ircd_vsprintf(char *dest, const char *format, va_list args)
{
	char ch;
	int written = 0;	/* bytes written so far */

	while ((ch = *format++))
	{
		if(ch == '%')
		{
			/*
			 * Advance past the %
			 */
			ch = *format++;

			/*
			 * Put the most common cases first - %s %d etc
			 */

			if(ch == 's')
			{
				const char *str = va_arg(args, const char *);

				while ((*dest = *str))
				{
					++dest;
					++str;

					++written;
				}

				continue;
			}	/* if (ch == 's') */
			
			
			if(ch == 'd')
				TYPE_TO_STR_SIGNED(int);

			if(ch == 'c')
			{
				*dest++ = va_arg(args, int);

				++written;

				continue;
			}	/* if (ch == 'c') */

			if(ch == 'u')
				TYPE_TO_STR(unsigned int);

			if(ch == 'x')
				HEX_TO_STR(unsigned int, HexLower);
			
			if(ch == 'X')
				HEX_TO_STR(unsigned int, HexUpper);

			if(ch == 'l')
			{
				if(*format == 'u') {
					format++;
					TYPE_TO_STR(unsigned long);
				}


				if(*format == 'd')
				{
					format++;
					TYPE_TO_STR_SIGNED(long);
				}
				
				if(*format == 'x')
				{
					format++;
					HEX_TO_STR(unsigned long, HexLower);
				}	
				
				if(*format == 'X')
				{
					format++;
					HEX_TO_STR(unsigned long, HexUpper);
				}
				
			}	/* if (ch == 'l') */

			if(ch != '%')
			{
				int ret;

				format -= 2;
				ret = vsprintf(dest, format, args);
				dest += ret;
				written += ret;

				break;
			}	/* if (ch != '%') */
		}		/* if (ch == '%') */

		*dest++ = ch;
		++written;
	}			/* while ((ch = *format++)) */

	/*
	 * Terminate the destination buffer with a \0
	 */
	*dest = '\0';

	return (written);
}				/* vSprintf() */

/*
ircd_snprintf()
 Optimized version of snprintf().

Inputs: dest   - destination string
        bytes  - number of bytes to copy
        format - formatted string
        args   - args to 'format'

Return: number of characters copied, NOT including the terminating
        NULL which is always placed at the end of the string
*/

int
ircd_snprintf(char *dest, const size_t bytes, const char *format, ...)
{
	va_list args;
	int count;

	va_start(args, format);

	count = ircd_vsnprintf(dest, bytes, format, args);

	va_end(args);

	return (count);
}				/* Snprintf() */

/*
ircd_sprintf()
 Optimized version of sprintf()

Inputs: dest   - destination string
        format - formatted string
        args   - arguments to 'format'

Return: number of characters copied, NOT including the terminating
        NULL which is always placed at the end of the string
*/

int
ircd_sprintf(char *dest, const char *format, ...)
{
	va_list args;
	int count;

	va_start(args, format);

	count = ircd_vsprintf(dest, format, args);

	va_end(args);

	return (count);
}				/* Sprintf() */

/*
 * ircd_vsnprintf_append()
 * appends sprintf formatted string to the end of the buffer but not
 * exceeding len
 */

inline int
ircd_vsnprintf_append(char *str, size_t len, const char *format, va_list ap)
{
        size_t x = strlen(str);
        return(ircd_vsnprintf(str+x, len - x, format, ap) + x);
}

/*
 * ircd_vsprintf_append()
 * appends sprintf formatted string to the end of the buffer
 */
 
inline int
ircd_vsprintf_append(char *str, const char *format, va_list ap)
{
        size_t x = strlen(str);
        return(ircd_vsprintf(str+x, format, ap) + x);
}

/*
 * ircd_sprintf_append()
 * appends sprintf formatted string to the end of the buffer
 */
int
ircd_sprintf_append(char *str, const char *format, ...)
{
        int x;
        va_list ap;
        va_start(ap, format);
        x = ircd_vsprintf_append(str, format, ap);
        va_end(ap);
        return(x);
}

/*
 * ircd_snprintf_append()
 * appends snprintf formatted string to the end of the buffer but not
 * exceeding len
 */

int
ircd_snprintf_append(char *str, size_t len, const char *format, ...)
{
        int x;
        va_list ap;
        va_start(ap, format);
        x = ircd_vsnprintf_append(str, len, format, ap);
        va_end(ap);
        return(x); 
}


