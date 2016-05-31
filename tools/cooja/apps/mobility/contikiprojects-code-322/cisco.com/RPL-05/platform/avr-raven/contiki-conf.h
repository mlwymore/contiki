/*
 * Copyright (c) 2006, Technical University of Munich
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
 * @(#)$$
 */

/**
 * \file
 *         Configuration for Atmel Raven
 *
 * \author
 *         Simon Barner <barner@in.tum.de>
 *         David Kopf <dak664@embarqmail.com>
 */

#ifndef __CONTIKI_CONF_H__
#define __CONTIKI_CONF_H__

/* MCU, Clock rate, ticks per second */
#define PLATFORM PLATFORM_AVR
#define RAVEN_REVISION RAVEN_D
#define MCU_MHZ 8
#define CLOCK_CONF_SECOND 125

/* COM port to be used for SLIP connection */
#define SLIP_PORT RS232_PORT_0

/* Pre-allocated memory for loadable modules heap space (in bytes)*/
#define MMEM_CONF_SIZE 256

/* Use the following address for code received via the codeprop
 * facility
 */
#define EEPROMFS_ADDR_CODEPROP 0x8000

#define CCIF
#define CLIF

#define RIMEADDR_CONF_SIZE       8

#define SICSLOWPAN_CONF_COMPRESSION   SICSLOWPAN_COMPRESSION_HC06
#define IEEE802154_CONF_PANID         0xBAAD //0xABCD 

/* RF230BB must be used with low power protocols */
#if RF230BB
#define SICSLOWPAN_CONF_CONVENTIONAL_MAC  1     //for barebones driver, sicslowpan calls radio->read function
#undef PACKETBUF_CONF_HDR_SIZE                  //RF230BB takes the packetbuf default for header size
#else
#define PACKETBUF_CONF_HDR_SIZE    0            //RF230 handles headers internally
#endif /*RF230BB */

#define SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS 2

#if 1  /* Network setup */

/* No radio cycling */
#define NETSTACK_CONF_NETWORK     sicslowpan_driver
#define NETSTACK_CONF_MAC         nullmac_driver
#define NETSTACK_CONF_RDC         sicslowmac_driver
#define NETSTACK_CONF_FRAMER      framer_802154
#define NETSTACK_CONF_RADIO       rf230_driver
#define RF230_CONF_AUTOACK        1
#define RF230_CONF_AUTORETRIES    2
#define SICSLOWPAN_CONF_FRAG      1
//Most browsers reissue GETs after 3 seconds which stops frag reassembly, longer MAXAGE does no good
#define SICSLOWPAN_CONF_MAXAGE    3
#define QUEUEBUF_CONF_NUM         1
#define QUEUEBUF_CONF_REF_NUM     1

#elif 0
/* Contiki-mac radio cycling */
#define NETSTACK_CONF_NETWORK     sicslowpan_driver
#define NETSTACK_CONF_MAC         nullmac_driver
#define NETSTACK_CONF_RDC         contikimac_driver
#define NETSTACK_CONF_FRAMER      framer_802154
#define NETSTACK_CONF_RADIO       rf230_driver
#define RF230_CONF_AUTOACK        0
#define RF230_CONF_AUTORETRIES    0

#elif 0
/* cx-mac radio cycling */
#define NETSTACK_CONF_NETWORK     sicslowpan_driver
#define NETSTACK_CONF_MAC         nullmac_driver
#define NETSTACK_CONF_RDC         cxmac_driver
#define NETSTACK_CONF_FRAMER      framer_802154
#define NETSTACK_CONF_RADIO       rf230_driver
#define RF230_CONF_AUTOACK        0
#define RF230_CONF_AUTORETRIES    0
#define MAC_CONF_CHANNEL_CHECK_RATE 8
#define SICSLOWPAN_CONF_FRAG      1
#define SICSLOWPAN_CONF_MAXAGE    3
#define QUEUEBUF_CONF_NUM         4
#define QUEUEBUF_CONF_REF_NUM     2
//Below will prevent fragmentation of TCP packets, undef for faster page loads, simpler wireshark captures
//#define UIP_CONF_TCP_MSS          48
//Below gives 10% duty cycle, undef for default 5%
//#define CXMAC_CONF_ON_TIME (RTIMER_ARCH_SECOND / 80)
//Below gives 50% duty cycle
//#define CXMAC_CONF_ON_TIME (RTIMER_ARCH_SECOND / 16)

#else
#error Network configuration not specified!
#endif   /* Network setup */

/* Logging adds 200 bytes to program size */
#define LOG_CONF_ENABLED         1

#define UIP_CONF_LL_802154       1
#define UIP_CONF_LLH_LEN         0

#define UIP_CONF_MAX_CONNECTIONS 2
#define UIP_CONF_MAX_LISTENPORTS 2
#define UIP_CONF_UDP_CONNS       2

#define UIP_CONF_IP_FORWARD      0
#define UIP_CONF_FWCACHE_SIZE    0

#define UIP_CONF_IPV6                 1
#define UIP_CONF_ROUTER               1
#define UIP_CONF_ROUTER_RPL           1
#define UIP_CONF_ETX                  0
#define RPL_CONF_DAG_OCP              0
#define RPL_CONF_ROOT_ENABLE          0 //1
#define RPL_CONF_LEAF_ONLY            0 //new
#define RPL_CONF_SEND_DIS             1 //0 
#define RPL_CONF_DAOROUTE_NB          4
#define RPL_CONF_PARENT_NBU           6
#define RPL_CONF_INSTANCE_ID          0
#define RPL_CONF_DAG_CONTROL          0x60  //0x60 D+A, 0x20 A, 0xa0 G+A, 0xe0 G+A+D 

#define UIP_CONF_IPV6_CHECKS     1
#define UIP_CONF_IPV6_QUEUE_PKT  0
#define UIP_CONF_IPV6_REASSEMBLY 0

#define UIP_CONF_DS6_NBR_NBU          10
#define UIP_CONF_DS6_DEFRT_NBU        2
#define UIP_CONF_DS6_PREFIX_NBU       2
#define UIP_CONF_DS6_SROUTE_NBU       2
#define UIP_CONF_DS6_ROUTE_NBU        10
#define UIP_CONF_DS6_ADDR_NBU         3
#define UIP_CONF_DS6_AADDR_NBU        1
#define UIP_CONF_DS6_MADDR_NBU        1

#define UIP_CONF_ICMP6                1
#define UIP_CONF_ICMP6_CONNS          3
#define UIP_CONF_ND6_API              1 
#define UIP_CONF_ND6_CONNS            1 

#define UIP_CONF_UDP             1
#define UIP_CONF_UDP_CHECKSUMS   1

#define UIP_CONF_TCP             1
#define UIP_CONF_TCP_SPLIT       1


#include <stdint.h>

typedef int32_t s32_t;
typedef unsigned short clock_time_t;
typedef unsigned char u8_t;
typedef unsigned short u16_t;
typedef unsigned long u32_t;
typedef unsigned short uip_stats_t;
typedef unsigned long off_t;

void clock_delay(unsigned int us2);
void clock_wait(int ms10);
void clock_set_seconds(unsigned long s);
unsigned long clock_seconds(void);

#endif /* __CONTIKI_CONF_H__ */
