/*
 * $Id$
 */

#ifndef IRCD_LIB_H
#define IRCD_LIB_H 1

#include "setup.h"
#include "config.h"
#include "ircd_defs.h"
#include "tools.h"
#include "commio.h"
#include "ircd_memory.h"
#include "balloc.h"
#include "linebuf.h"
#include "snprintf.h"
#include "event.h"

typedef void log_cb(const char *buffer);
typedef void restart_cb(const char *buffer);
typedef void die_cb(const char *buffer);

void lib_ilog(const char *, ...);
void lib_restart(const char *, ...);
void lib_die(const char *, ...);
void set_time(void);
void ircd_lib(log_cb *xilog, restart_cb *irestart, die_cb *idie, int closeall);
struct timeval SystemTime;  
#ifndef CurrentTime
#define CurrentTime SystemTime.tv_sec
#endif


#endif























