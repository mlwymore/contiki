/*
 * Copyright (c) 2005, Swedish Institute of Computer Science
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 * @(#)$Id: contiki-conf.h,v 1.11 2008/10/15 09:14:25 adamdunkels Exp $
 */

#ifndef __CONTIKI_CONF_H__
#define __CONTIKI_CONF_H__

#include <inttypes.h>

#define CC_CONF_REGISTER_ARGS          1
#define CC_CONF_FUNCTION_POINTER_ARGS  1
#define CC_CONF_FASTCALL
#define CC_CONF_VA_ARGS                1
#define CC_CONF_INLINE                 inline

#define CCIF
#define CLIF

#undef vsnprintf
#undef sprintf
#undef snprintf
#undef localtime
#undef strtoul
#undef strstr

#define vsnprintf(...)	uvsnprintf(__VA_ARGS__)
#define sprintf(...) 		usprintf(__VA_ARGS__)
#define snprintf(...) 	usnprintf(__VA_ARGS__)
#define localtime(...) 	ulocaltime(__VA_ARGS__)
#define strtoul(...) 		ustrtoul(__VA_ARGS__)
#define strstr(...) 		ustrstr(__VA_ARGS__)

#undef bool
typedef unsigned char bool;

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef NULL
#define NULL 0
#endif

typedef unsigned long clock_time_t;
typedef unsigned short uip_stats_t;
typedef unsigned char u8_t;
typedef unsigned short u16_t;
typedef unsigned long u32_t;
typedef long s32_t;

/**
 * Contiki configuration
 */
#define CLOCK_CONF_SECOND 100
#define LOG_CONF_ENABLED	0



/* Not part of C99 but actually present */
int strcasecmp(const char*, const char*);

#endif /* __CONTIKI_CONF_H__ */
