/**
 * \addtogroup sicslowpan
 * @{
 */
/*
 * Copyright (c) 2008, Swedish Institute of Computer Science.
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
 * $Id: sicslowpan.c,v 1.21 2010/02/28 08:29:42 adamdunkels Exp $
 */
/**
 * \file
 *         6lowpan implementation (RFC4944 and draft-ietf-6lowpan-hc-06)
 *
 * \author Adam Dunkels <adam@sics.se>
 * \author Nicolas Tsiftes <nvt@sics.se>
 * \author Niclas Finne <nfi@sics.se>
 * \author Mathilde Durvy <mdurvy@cisco.com>
 * \author Julien Abeille <jabeille@cisco.com>
 * \author Joakim Eriksson <joakime@sics.se>
 */

#include <string.h>

#include "contiki.h"
#include "dev/watchdog.h"
#include "net/tcpip.h"
#include "net/uip.h"
#include "net/uip-ds6.h"
#include "net/rime.h"
#include "net/sicslowpan.h"
#include "net/netstack.h"

#define DEBUG 0
#if DEBUG
/* PRINTFI and PRINTFO are defined for input and output to debug one without changing the timing of the other */
u8_t p;
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINTFI(...) printf(__VA_ARGS__)
#define PRINTFO(...) printf(__VA_ARGS__)
#define PRINT6ADDR(addr) PRINTF(" %02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x ", ((u8_t *)addr)[0], ((u8_t *)addr)[1], ((u8_t *)addr)[2], ((u8_t *)addr)[3], ((u8_t *)addr)[4], ((u8_t *)addr)[5], ((u8_t *)addr)[6], ((u8_t *)addr)[7], ((u8_t *)addr)[8], ((u8_t *)addr)[9], ((u8_t *)addr)[10], ((u8_t *)addr)[11], ((u8_t *)addr)[12], ((u8_t *)addr)[13], ((u8_t *)addr)[14], ((u8_t *)addr)[15])
#define PRINTLLADDR(lladdr) PRINTF(" %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x ",lladdr->addr[0], lladdr->addr[1], lladdr->addr[2], lladdr->addr[3],lladdr->addr[4], lladdr->addr[5],lladdr->addr[6], lladdr->addr[7])
#define PRINTPACKETBUF() PRINTF("RIME buffer: "); for(p = 0; p < packetbuf_datalen(); p++){PRINTF("%.2X", *(rime_ptr + p));} PRINTF("\n")
#define PRINTUIPBUF() PRINTF("UIP buffer: "); for(p = 0; p < uip_len; p++){PRINTF("%.2X", uip_buf[p]);}PRINTF("\n")
#define PRINTSICSLOWPANBUF() PRINTF("SICSLOWPAN buffer: "); for(p = 0; p < sicslowpan_len; p++){PRINTF("%.2X", sicslowpan_buf[p]);}PRINTF("\n")
#else
#define PRINTF(...)
#define PRINTFI(...)
#define PRINTFO(...)
#define PRINT6ADDR(addr)
#define PRINTLLADDR(lladdr)
#define PRINTPACKETBUF()
#define PRINTUIPBUF()
#define PRINTSICSLOWPANBUF()
#endif /* DEBUG == 1*/

#if UIP_LOGGING
#include <stdio.h>
void uip_log(char *msg);
#define UIP_LOG(m) uip_log(m)
#else
#define UIP_LOG(m)
#endif /* UIP_LOGGING == 1 */

#ifndef SICSLOWPAN_COMPRESSION
#ifdef SICSLOWPAN_CONF_COMPRESSION
#define SICSLOWPAN_COMPRESSION SICSLOWPAN_CONF_COMPRESSION
#else
#define SICSLOWPAN_COMPRESSION SICSLOWPAN_COMPRESSION_IPV6
#endif /* SICSLOWPAN_CONF_COMPRESSION */
#endif /* SICSLOWPAN_COMPRESSION */

#define GET16(ptr,index) (((uint16_t)((ptr)[index] << 8)) | ((ptr)[(index) + 1]))
#define SET16(ptr,index,value) do {     \
  (ptr)[index] = ((value) >> 8) & 0xff; \
  (ptr)[index + 1] = (value) & 0xff;    \
} while(0)

/** \name Pointers in the rime buffer
 *  @{
 */
/* #define RIME_FRAG_BUF               ((struct sicslowpan_frag_hdr *)rime_ptr) */
#define RIME_FRAG_PTR           (rime_ptr)
#define RIME_FRAG_DISPATCH_SIZE 0   /* 16 bit */
#define RIME_FRAG_TAG           2   /* 16 bit */
#define RIME_FRAG_OFFSET        4   /* 8 bit */

/* define the buffer as a byte array */
#define RIME_IPHC_BUF              ((uint8_t *)(rime_ptr + rime_hdr_len))

/* #define RIME_HC1_BUF                ((struct sicslowpan_hc1_hdr *)(rime_ptr + rime_hdr_len)) */
#define RIME_HC1_PTR            (rime_ptr + rime_hdr_len)
#define RIME_HC1_DISPATCH       0 /* 8 bit */
#define RIME_HC1_ENCODING       1 /* 8 bit */
#define RIME_HC1_TTL            2 /* 8 bit */

/* #define RIME_HC1_HC_UDP_BUF  ((struct sicslowpan_hc1_hc_udp_hdr *)(rime_ptr + rime_hdr_len)) */
#define RIME_HC1_HC_UDP_PTR           (rime_ptr + rime_hdr_len)
#define RIME_HC1_HC_UDP_DISPATCH      0 /* 8 bit */
#define RIME_HC1_HC_UDP_HC1_ENCODING  1 /* 8 bit */
#define RIME_HC1_HC_UDP_UDP_ENCODING  2 /* 8 bit */
#define RIME_HC1_HC_UDP_TTL           3 /* 8 bit */
#define RIME_HC1_HC_UDP_PORTS         4 /* 8 bit */
#define RIME_HC1_HC_UDP_CHKSUM        5 /* 16 bit */

/* #define RIME_IPHC_DISPATCH            0 /\* 8 bit *\/ */
/* #define RIME_IPHC_ENCODING1           1 /\* 8 bit *\/ */
/* #define RIME_IPHC_ENCODING2           2 /\* 8 bit *\/ */

/* #define RIME_IP_BUF                         ((struct uip_ip_hdr *)(rime_ptr + rime_hdr_len)) */
/** @} */

/** \name Pointers in the sicslowpan and uip buffer
 *  @{
 */
#define SICSLOWPAN_IP_BUF   ((struct uip_ip_hdr *)&sicslowpan_buf[UIP_LLH_LEN])
#define SICSLOWPAN_UDP_BUF ((struct uip_udp_hdr *)&sicslowpan_buf[UIP_LLIPH_LEN])

#define UIP_IP_BUF          ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
#define UIP_UDP_BUF          ((struct uip_udp_hdr *)&uip_buf[UIP_LLIPH_LEN])
/** @} */


/** \brief Size of the 802.15.4 payload (127byte - 25 for MAC header) */
#define MAC_MAX_PAYLOAD 102

/** \name General variables
 *  @{
 */
/** A pointer to the mac driver */
const struct mac_driver *sicslowpan_mac;

/**
 * A pointer to the rime buffer.
 * We initialize it to the beginning of the rime buffer, then
 * access different fields by updating the offset rime_hdr_len.
 */
static u8_t *rime_ptr;

/**
 * rime_hdr_len is the total length of (the processed) 6lowpan headers
 * (fragment headers, IPV6 or HC1, HC2, and HC1 and HC2 non compressed
 * fields).
 */
static u8_t rime_hdr_len;

/**
 * The length of the payload in the Rime buffer.
 * The payload is what comes after the compressed or uncompressed
 * headers (can be the IP payload if the IP header only is compressed
 * or the UDP payload if the UDP header is also compressed)
 */
static u8_t rime_payload_len;

/**
 * uncomp_hdr_len is the length of the headers before compression (if HC2
 * is used this includes the UDP header in addition to the IP header).
 */
static u8_t uncomp_hdr_len;
/** @} */

#if SICSLOWPAN_CONF_FRAG
/** \name Fragmentation related variables
 *  @{
 */


/* NOTE: lenght is before the buffer to ensure alignment of the
   buffer */
static u16_t sicslowpan_len;
/**
 * The buffer used for the 6lowpan reassembly.
 * This buffer contains only the IPv6 packet (no MAC header, 6lowpan, etc).
 * It has a fix size as we do not use dynamic memory allocation.
 */

static u8_t sicslowpan_buf[UIP_BUFSIZE];

/** The total length of the IPv6 packet in the sicslowpan_buf. */

/**
 * length of the ip packet already sent / received.
 * It includes IP and transport headers.
 */
static u16_t processed_ip_len;

/** Datagram tag to be put in the fragments I send. */
static u16_t my_tag;

/** When reassembling, the tag in the fragments being merged. */
static u16_t reass_tag;

/** When reassembling, the source address of the fragments being merged */
rimeaddr_t frag_sender;

/** Reassembly %process %timer. */
static struct timer reass_timer;

/** @} */
#else /* SICSLOWPAN_CONF_FRAG */
/** The buffer used for the 6lowpan processing is uip_buf.
    We do not use any additional buffer.*/
#define sicslowpan_buf uip_buf
#define sicslowpan_len uip_len
#endif /* SICSLOWPAN_CONF_FRAG */


#if SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC06
/** \name HC06 specific variables
 *  @{
 */
/** Addresses contexts for IPHC. */
static struct sicslowpan_addr_context
addr_contexts[SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS];

/** pointer to an address context. */
static struct sicslowpan_addr_context *context;

/** pointer to the byte where to write next inline field. */
static u8_t *hc06_ptr;

/** Index for loops. */
static u8_t i;
/** @} */


/*--------------------------------------------------------------------*/
/** \name HC06 related functions
 * @{                                                                 */
/*--------------------------------------------------------------------*/
/** \brief find the context corresponding to prefix ipaddr */
static struct sicslowpan_addr_context*
addr_context_lookup_by_prefix(uip_ipaddr_t *ipaddr) {
  for(i = 0; i < SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS; i++) {
    if((addr_contexts[i].used == 1) &&
       uip_ipaddr_prefixcmp(&addr_contexts[i].prefix, ipaddr, 64)) {
      return &addr_contexts[i];
    }
  }
  return NULL;
}
/*--------------------------------------------------------------------*/
/** \brief find the context with the given number */
static struct sicslowpan_addr_context*
addr_context_lookup_by_number(u8_t number) {
  for(i = 0; i < SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS; i++) {
    if((addr_contexts[i].used == 1) &&
       addr_contexts[i].number == number) {
      return &addr_contexts[i];
    }
  }
  return NULL;
}
/*--------------------------------------------------------------------*/
/**
 * \brief Compress IP/UDP header
 *
 * This function is called by the 6lowpan code to create a compressed
 * 6lowpan packet in the packetbuf buffer from a full IPv6 packet in the
 * uip_buf buffer.
 *
 *
 * HC-06 (draft-ietf-6lowpan-hc, version 6)\n
 * http://tools.ietf.org/html/draft-ietf-6lowpan-hc-06
 *
 * \note We do not support ISA100_UDP header compression
 *
 * For LOWPAN_UDP compression, we either compress both ports or none.
 * General format with LOWPAN_UDP compression is
 * \verbatim
 *                      1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |0|1|1|TF |N|HLI|C|S|SAM|M|D|DAM| SCI   | DCI   | comp. IPv6 hdr|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | compressed IPv6 fields .....                                  |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | LOWPAN_UDP    | non compressed UDP fields ...                 |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | L4 data ...                                                   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * \endverbatim
 * \note The context number 00 is reserved for the link local prefix.
 * For unicast addresses, if we cannot compress the prefix, we neither
 * compress the IID.
 * \param rime_destaddr L2 destination address, needed to compress IP
 * dest
 */
static void
compress_hdr_hc06(rimeaddr_t *rime_destaddr)
{
  uint8_t tmp;
#if DEBUG
  PRINTF("before compression: ");
  for (tmp = 0; tmp < UIP_IP_BUF->len[1] + 40; tmp++) {
    uint8_t data = ((uint8_t *) (UIP_IP_BUF))[tmp];
    PRINTF("%02x", data);
  }
  PRINTF("\n");
#endif

  hc06_ptr = rime_ptr + 2;
  /*
   * As we copy some bit-length fields, in the IPHC encoding bytes,
   * we sometimes use |=
   * If the field is 0, and the current bit value in memory is 1,
   * this does not work. We therefore reset the IPHC encoding here
   */

  RIME_IPHC_BUF[0] = SICSLOWPAN_DISPATCH_IPHC;
  RIME_IPHC_BUF[1] = 0;
  RIME_IPHC_BUF[2] = 0; /* might not be used - but needs to be cleared */

  /*
   * Address handling needs to be made first since it might
   * cause an extra byte with [ SCI | DCI ]
   *
   */


  /* check if dest context exists (for allocating third byte) */
  /* TODO: fix this so that it remembers the looked up values for
     avoiding two lookups - or set the lookup values immediately */
  if(addr_context_lookup_by_prefix(&UIP_IP_BUF->destipaddr) != NULL ||
     addr_context_lookup_by_prefix(&UIP_IP_BUF->srcipaddr) != NULL) {
    /* set context flag and increase hc06_ptr */
    PRINTF("IPHC: compressing dest or src ipaddr - setting CID\n");
    RIME_IPHC_BUF[1] |= SICSLOWPAN_IPHC_CID;
    hc06_ptr++;
  }

  /*
   * Traffic class, flow label
   * If flow label is 0, compress it. If traffic class is 0, compress it
   * We have to process both in the same time as the offset of traffic class
   * depends on the presence of version and flow label
   */
 
  /* hc06 format of tc is ECN | DSCP , original is DSCP | ECN */
  tmp = (UIP_IP_BUF->vtc << 4) | (UIP_IP_BUF->tcflow >> 4);
  tmp = ((tmp & 0x03) << 6) | (tmp >> 2);
  
  if(((UIP_IP_BUF->tcflow & 0x0F) == 0) &&
     (UIP_IP_BUF->flow == 0)) {
    /* flow label can be compressed */
    RIME_IPHC_BUF[0] |= SICSLOWPAN_IPHC_FL_C;
    if(((UIP_IP_BUF->vtc & 0x0F) == 0) &&
       ((UIP_IP_BUF->tcflow & 0xF0) == 0)) {
      /* compress (elide) all */
      RIME_IPHC_BUF[0] |= SICSLOWPAN_IPHC_TC_C;
    } else {
      /* compress only the flow label */
     *hc06_ptr = tmp;
      hc06_ptr += 1;
    }
  } else {
    /* Flow label cannot be compressed */
    if(((UIP_IP_BUF->vtc & 0x0F) == 0) &&
       ((UIP_IP_BUF->tcflow & 0xF0) == 0)) {
      /* compress only traffic class */
      RIME_IPHC_BUF[0] |= SICSLOWPAN_IPHC_TC_C;
      *hc06_ptr = (tmp & 0xc0) |
        (UIP_IP_BUF->tcflow & 0x0F);
     memcpy(hc06_ptr + 1, &UIP_IP_BUF->flow, 2);
      hc06_ptr += 3;
    } else {
      /* compress nothing */
      memcpy(hc06_ptr, &UIP_IP_BUF->vtc, 4);
      /* but replace the top byte with the new ECN | DSCP format*/
      *hc06_ptr = tmp;
      hc06_ptr += 4;
   }
  }

  /* Note that the payload length is always compressed */

  /* Next header. We compress it if UDP */
#if UIP_CONF_UDP
  if(UIP_IP_BUF->proto == UIP_PROTO_UDP) {
    RIME_IPHC_BUF[0] |= SICSLOWPAN_IPHC_NH_C;
  } else {
#endif /*UIP_CONF_UDP*/
    *hc06_ptr = UIP_IP_BUF->proto;
    hc06_ptr += 1;
#if UIP_CONF_UDP
 }
#endif /*UIP_CONF_UDP*/

  /*
   * Hop limit
   * if 1: compress, encoding is 01
   * if 64: compress, encoding is 10
   * if 255: compress, encoding is 11
   * else do not compress
   */
  switch(UIP_IP_BUF->ttl) {
    case 1:
      RIME_IPHC_BUF[0] |= SICSLOWPAN_IPHC_TTL_1;
      break;
    case 64:
      RIME_IPHC_BUF[0] |= SICSLOWPAN_IPHC_TTL_64;
      break;
    case 255:
      RIME_IPHC_BUF[0] |= SICSLOWPAN_IPHC_TTL_255;
      break;
    default:
      *hc06_ptr = UIP_IP_BUF->ttl;
      hc06_ptr += 1;
      break;
  }

  /* source address - cannot be multicast */
  if(uip_is_addr_unspecified(&UIP_IP_BUF->srcipaddr)) {
    PRINTF("IPHC: compressing unspecified - setting SAC\n");
    RIME_IPHC_BUF[1] |= SICSLOWPAN_IPHC_SAC;
    RIME_IPHC_BUF[1] |= SICSLOWPAN_IPHC_SAM_00;
  } else if((context = addr_context_lookup_by_prefix(&UIP_IP_BUF->srcipaddr))
     != NULL) {
    /* elide the prefix - indicate by CID and set context + SAC */
    PRINTF("IPHC: compressing src with context - setting CID & SAC ctx: %d\n",
	   context->number);
    RIME_IPHC_BUF[1] |= SICSLOWPAN_IPHC_CID | SICSLOWPAN_IPHC_SAC;
    RIME_IPHC_BUF[2] |= context->number << 4;
    /* compession compare with this nodes address (source) */
    if(uip_is_addr_mac_addr_based(&UIP_IP_BUF->srcipaddr, &uip_lladdr)){
      /* elide the IID */
      RIME_IPHC_BUF[1] |= SICSLOWPAN_IPHC_SAM_11; /* 0-bits */
    } else {
      if(sicslowpan_is_iid_16_bit_compressable(&UIP_IP_BUF->srcipaddr)){
        /* compress IID to 16 bits */
        RIME_IPHC_BUF[1] |= SICSLOWPAN_IPHC_SAM_10; /* 16-bits */
        memcpy(hc06_ptr, &UIP_IP_BUF->srcipaddr.u16[7], 2);
        hc06_ptr += 2;
      } else {
        /* do not compress IID */
        RIME_IPHC_BUF[1] |= SICSLOWPAN_IPHC_SAM_01; /* 64-bits */
        memcpy(hc06_ptr, &UIP_IP_BUF->srcipaddr.u16[4], 8);
        hc06_ptr += 8;
      }
    }
    /* No context found for this address */
  } else if(uip_is_addr_link_local(&UIP_IP_BUF->srcipaddr)) {
    // TODO: make a function of this: compress_ll_hc06(&UIP_IP_BUF->srcipaddr);
    if(uip_is_addr_mac_addr_based(&UIP_IP_BUF->srcipaddr, &uip_lladdr)){
      RIME_IPHC_BUF[1] |= SICSLOWPAN_IPHC_SAM_11; /* 0-bits */
    } else if(sicslowpan_is_iid_16_bit_compressable(&UIP_IP_BUF->srcipaddr)){
      /* compress IID to 16 bits fe80::XXXX */
      RIME_IPHC_BUF[1] |= SICSLOWPAN_IPHC_SAM_10; /* 16-bits */
      memcpy(hc06_ptr, &UIP_IP_BUF->srcipaddr.u16[7], 2);
      hc06_ptr += 2;
    } else {
      /* do not compress IID => fe80::IID */
      RIME_IPHC_BUF[1] |= SICSLOWPAN_IPHC_SAM_01; /* 64-bits */
      memcpy(hc06_ptr, &UIP_IP_BUF->srcipaddr.u16[4], 8);
      hc06_ptr += 8;
    }
  } else {
    /* send the full address => SAC = 0, SAM = 00 */
    RIME_IPHC_BUF[1] |= SICSLOWPAN_IPHC_SAM_00; /* 128-bits */
    memcpy(hc06_ptr, &UIP_IP_BUF->srcipaddr.u16[0], 16);
    hc06_ptr += 16;
  }

  /* dest address*/
  if(uip_is_addr_mcast(&UIP_IP_BUF->destipaddr)) {
    /* Address is multicast, try to compress */
    RIME_IPHC_BUF[1] |= SICSLOWPAN_IPHC_M;
    if(sicslowpan_is_mcast_addr_compressable8(&UIP_IP_BUF->destipaddr)) {
      RIME_IPHC_BUF[1] |= SICSLOWPAN_IPHC_DAM_11;
      /* use last byte */
      *hc06_ptr = UIP_IP_BUF->destipaddr.u8[15];
      hc06_ptr += 1;
    } else if(sicslowpan_is_mcast_addr_compressable32(&UIP_IP_BUF->destipaddr)){
      RIME_IPHC_BUF[1] |= SICSLOWPAN_IPHC_DAM_10;
      /* second byte + the last three */
      *hc06_ptr = UIP_IP_BUF->destipaddr.u8[1];
      memcpy(hc06_ptr + 1, &UIP_IP_BUF->destipaddr.u8[13], 3);
      hc06_ptr += 4;
    } else if(sicslowpan_is_mcast_addr_compressable48(&UIP_IP_BUF->destipaddr)){
      RIME_IPHC_BUF[1] |= SICSLOWPAN_IPHC_DAM_01;
      /* second byte + the last five */
      *hc06_ptr = UIP_IP_BUF->destipaddr.u8[1];
      memcpy(hc06_ptr + 1, &UIP_IP_BUF->destipaddr.u8[11], 5);
      hc06_ptr += 6;
    } else {
      RIME_IPHC_BUF[1] |= SICSLOWPAN_IPHC_DAM_00;
      /* full address */
      memcpy(hc06_ptr, &UIP_IP_BUF->destipaddr.u8[0], 16);
      hc06_ptr += 16;
    }
  } else {
    /* Address is unicast, try to compress */
    if((context = addr_context_lookup_by_prefix(&UIP_IP_BUF->destipaddr)) != NULL) {
      /* elide the prefix */
      RIME_IPHC_BUF[1] |= SICSLOWPAN_IPHC_DAC;
      RIME_IPHC_BUF[2] |= context->number;
      /* compession compare with link adress (destination) */
      if(uip_is_addr_mac_addr_based(&UIP_IP_BUF->destipaddr, (uip_lladdr_t *)rime_destaddr)) {
        /* elide the IID */
        RIME_IPHC_BUF[1] |= SICSLOWPAN_IPHC_DAM_11; /* 0-bits */
      } else {
        if(sicslowpan_is_iid_16_bit_compressable(&UIP_IP_BUF->destipaddr)) {
          /* compress IID to 16 bits */
          RIME_IPHC_BUF[1] |= SICSLOWPAN_IPHC_DAM_10; /* 16-bits */
          memcpy(hc06_ptr, &UIP_IP_BUF->destipaddr.u16[7], 2);
          hc06_ptr += 2;
        } else {
          /* do not compress IID */
          RIME_IPHC_BUF[1] |= SICSLOWPAN_IPHC_DAM_01; /* 64-bits */
          memcpy(hc06_ptr, &UIP_IP_BUF->destipaddr.u16[4], 8);
          hc06_ptr += 8;
        }
      }
      /* No context found for this address */
    } else if(uip_is_addr_link_local(&UIP_IP_BUF->destipaddr)) {
      // TODO: make a function of this: compress_ll_hc06(&UIP_IP_BUF->destipaddr);
      if(uip_is_addr_mac_addr_based(&UIP_IP_BUF->destipaddr, (uip_lladdr_t *)rime_destaddr)) {
        RIME_IPHC_BUF[1] |= SICSLOWPAN_IPHC_DAM_11; /* 0-bits */
      } else if(sicslowpan_is_iid_16_bit_compressable(&UIP_IP_BUF->destipaddr)){
        /* compress IID to 16 bits fe80::XXXX */
        RIME_IPHC_BUF[1] |= SICSLOWPAN_IPHC_DAM_10; /* 16-bits */
        memcpy(hc06_ptr, &UIP_IP_BUF->destipaddr.u16[7], 2);
        hc06_ptr += 2;
      } else {
        /* do not compress IID => fe80::IID */
        RIME_IPHC_BUF[1] |= SICSLOWPAN_IPHC_DAM_01; /* 64-bits */
        memcpy(hc06_ptr, &UIP_IP_BUF->destipaddr.u16[4], 8);
        hc06_ptr += 8;
      }
    } else {
      /* send the full address */
      RIME_IPHC_BUF[1] |= SICSLOWPAN_IPHC_DAM_00; /* 128-bits */
      memcpy(hc06_ptr, &UIP_IP_BUF->destipaddr.u16[0], 16);
      hc06_ptr += 16;
    }
  }


  uncomp_hdr_len = UIP_IPH_LEN;

#if UIP_CONF_UDP
  /* UDP header compression */
  if(UIP_IP_BUF->proto == UIP_PROTO_UDP) {
    if(HTONS(UIP_UDP_BUF->srcport)  >= SICSLOWPAN_UDP_PORT_MIN &&
       HTONS(UIP_UDP_BUF->srcport)  <  SICSLOWPAN_UDP_PORT_MAX &&
       HTONS(UIP_UDP_BUF->destport) >= SICSLOWPAN_UDP_PORT_MIN &&
       HTONS(UIP_UDP_BUF->destport) <  SICSLOWPAN_UDP_PORT_MAX) {
      /* we can compress. Copy compressed ports, full chcksum */
      *hc06_ptr = SICSLOWPAN_NHC_UDP_C;
      *(hc06_ptr + 1) =
        (u8_t)((HTONS(UIP_UDP_BUF->srcport) -
                SICSLOWPAN_UDP_PORT_MIN) << 4) +
        (u8_t)((HTONS(UIP_UDP_BUF->destport) -
                SICSLOWPAN_UDP_PORT_MIN));
      memcpy(hc06_ptr + 2, &UIP_UDP_BUF->udpchksum, 2);
      hc06_ptr += 4;
    } else {
      /* we cannot compress. Copy uncompressed ports, full chcksum */
      *hc06_ptr = SICSLOWPAN_NHC_UDP_I;
      memcpy(hc06_ptr + 1, &UIP_UDP_BUF->srcport, 4);
      memcpy(hc06_ptr + 5, &UIP_UDP_BUF->udpchksum, 2);
      hc06_ptr += 7;
    }
    uncomp_hdr_len += UIP_UDPH_LEN;
  }
#endif /*UIP_CONF_UDP*/
  rime_hdr_len = hc06_ptr - rime_ptr;
  return;
}

/*--------------------------------------------------------------------*/
/**
 * \brief Uncompress HC06 (i.e., IPHC and LOWPAN_UDP) headers and put
 * them in sicslowpan_buf
 *
 * This function is called by the input function when the dispatch is
 * HC06.
 * We %process the packet in the rime buffer, uncompress the header
 * fields, and copy the result in the sicslowpan buffer.
 * At the end of the decompression, rime_hdr_len and uncompressed_hdr_len
 * are set to the appropriate values
 *
 * \param ip_len Equal to 0 if the packet is not a fragment (IP length
 * is then inferred from the L2 length), non 0 if the packet is a 1st
 * fragment.
 */

static void
uncompress_hdr_hc06(u16_t ip_len) {
  uint8_t tmp;
  /* at least two byte will be used for the encoding */
  hc06_ptr = rime_ptr + rime_hdr_len + 2;

  /* another if the CID flag is set */
  if(RIME_IPHC_BUF[1] & SICSLOWPAN_IPHC_CID) {
    PRINTF("IPHC: CID flag set - increase header with one\n");
    hc06_ptr++;
  }

  /* Traffic class and flow label */
  if((RIME_IPHC_BUF[0] & SICSLOWPAN_IPHC_FL_C) == 0) {
    /* Flow label are carried inline */
    if((RIME_IPHC_BUF[0] & SICSLOWPAN_IPHC_TC_C) == 0) {
      /* Traffic class is carried inline */
      memcpy(&SICSLOWPAN_IP_BUF->tcflow, hc06_ptr + 1, 3);
      tmp = *hc06_ptr;
      hc06_ptr += 4;
        /* hc06 format of tc is ECN | DSCP , original is DSCP | ECN */
        /* set version, pick highest DSCP bits and set in vtc */
      SICSLOWPAN_IP_BUF->vtc = 0x60 | ((tmp >> 2) & 0x0f);
      /* ECN rolled down two steps + lowest DSCP bits at top two bits */
      SICSLOWPAN_IP_BUF->tcflow = ((tmp >> 2) & 0x30) | (tmp << 6) |
  	(SICSLOWPAN_IP_BUF->tcflow & 0x0f);
    } else {
      /* Traffic class is compressed (set version and no TC)*/
      SICSLOWPAN_IP_BUF->vtc = 0x60;
      /* highest flow label bits + ECN bits */
      SICSLOWPAN_IP_BUF->tcflow = (*hc06_ptr & 0x0F) |
  	((*hc06_ptr >> 2) & 0x30);
      memcpy(&SICSLOWPAN_IP_BUF->flow, hc06_ptr + 1, 2);
      hc06_ptr += 3;
    }
  } else {
    /* Version is always 6! */
    /* Version and flow label are compressed */
    if((RIME_IPHC_BUF[0] & SICSLOWPAN_IPHC_TC_C) == 0) {
      /* Traffic class is inline */
      SICSLOWPAN_IP_BUF->vtc = 0x60 | ((*hc06_ptr >> 2) & 0x0f);
      SICSLOWPAN_IP_BUF->tcflow = ((*hc06_ptr << 6) & 0xC0) | ((*hc06_ptr >> 2) & 0x30);
      SICSLOWPAN_IP_BUF->flow = 0;
      hc06_ptr += 3;
    } else {
      /* Traffic class is compressed */
      SICSLOWPAN_IP_BUF->vtc = 0x60;
      SICSLOWPAN_IP_BUF->tcflow = 0;
      SICSLOWPAN_IP_BUF->flow = 0;
    }
  }

  /* Next Header */
  if((RIME_IPHC_BUF[0] & SICSLOWPAN_IPHC_NH_C) == 0) {
    /* Next header is carried inline */
    SICSLOWPAN_IP_BUF->proto = *hc06_ptr;
    PRINTF("IPHC: next header inline: %d\n", SICSLOWPAN_IP_BUF->proto);
    hc06_ptr += 1;
  }

  /* Hop limit */
  switch(RIME_IPHC_BUF[0] & 0x03) {
    case SICSLOWPAN_IPHC_TTL_1:
      SICSLOWPAN_IP_BUF->ttl = 1;
      break;
    case SICSLOWPAN_IPHC_TTL_64:
      SICSLOWPAN_IP_BUF->ttl = 64;
      break;
    case SICSLOWPAN_IPHC_TTL_255:
      SICSLOWPAN_IP_BUF->ttl = 255;
      break;
    case SICSLOWPAN_IPHC_TTL_I:
      SICSLOWPAN_IP_BUF->ttl = *hc06_ptr;
      hc06_ptr += 1;
      break;
  }

  /* context based compression */
  if(RIME_IPHC_BUF[1] & SICSLOWPAN_IPHC_SAC) {
    uint8_t sci = (RIME_IPHC_BUF[1] & SICSLOWPAN_IPHC_CID) ?
      RIME_IPHC_BUF[2] >> 4 : 0;

    /* Source address */
    if((RIME_IPHC_BUF[1] & SICSLOWPAN_IPHC_SAM_11) != SICSLOWPAN_IPHC_SAM_00) {
      context =
	addr_context_lookup_by_number(sci);
      if(context == NULL) {
        PRINTF("sicslowpan uncompress_hdr: error context not found\n");
        return;
      } else {
        PRINTF("IPHC: found compressed source context for sci = %d\n", sci);
      }
    }

    switch(RIME_IPHC_BUF[1] & SICSLOWPAN_IPHC_SAM_11) {
    case SICSLOWPAN_IPHC_SAM_00:
      /* copy the unspecificed address */
      PRINTF("IPHC: unspecified address\n");
      memset(&SICSLOWPAN_IP_BUF->srcipaddr, 0, 16);
      break;
    case SICSLOWPAN_IPHC_SAM_01: /* 64 bits */
      /* copy prefix from context */
      memcpy(&SICSLOWPAN_IP_BUF->srcipaddr, context->prefix, 8);
      /* copy IID from packet */
      memcpy(&SICSLOWPAN_IP_BUF->srcipaddr.u8[8], hc06_ptr, 8);
      hc06_ptr += 8;
      break;
    case SICSLOWPAN_IPHC_SAM_10: /* 16 bits */
      /* unicast address */
      memcpy(&SICSLOWPAN_IP_BUF->srcipaddr, context->prefix, 8);
      /* copy 6 NULL bytes then 2 last bytes of IID */
      memset(&SICSLOWPAN_IP_BUF->srcipaddr.u8[8], 0, 6);
      memcpy(&SICSLOWPAN_IP_BUF->srcipaddr.u8[14], hc06_ptr, 2);
      hc06_ptr += 2;
      break;
    case SICSLOWPAN_IPHC_SAM_11: /* 0-bits */
      /* copy prefix from context */
      memcpy(&SICSLOWPAN_IP_BUF->srcipaddr, context->prefix, 8);
      /* infer IID from L2 address */
      uip_ds6_set_addr_iid(&SICSLOWPAN_IP_BUF->srcipaddr,
                           (uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_SENDER));
      break;
    }
    /* end context based compression */
  } else {
    /* no compression and link local */
    switch(RIME_IPHC_BUF[1] & SICSLOWPAN_IPHC_SAM_11) {
    case SICSLOWPAN_IPHC_SAM_00: /* 128 bits */
      /* copy whole address from packet */
      memcpy(&SICSLOWPAN_IP_BUF->srcipaddr.u8[0], hc06_ptr, 16);
      hc06_ptr += 16;
      break;
    case SICSLOWPAN_IPHC_SAM_01: /* 64 bits */
      SICSLOWPAN_IP_BUF->srcipaddr.u8[0] = 0xfe;
      SICSLOWPAN_IP_BUF->srcipaddr.u8[1] = 0x80;
      /* copy 6 NULL bytes then 2 last bytes of IID */
      memset(&SICSLOWPAN_IP_BUF->srcipaddr.u8[2], 0, 6);
      /* copy IID from packet */
      memcpy(&SICSLOWPAN_IP_BUF->srcipaddr.u8[8], hc06_ptr, 8);
      hc06_ptr += 8;
      break;
    case SICSLOWPAN_IPHC_SAM_10: /* 16 bits */
      SICSLOWPAN_IP_BUF->srcipaddr.u8[0] = 0xfe;
      SICSLOWPAN_IP_BUF->srcipaddr.u8[1] = 0x80;
      /* copy 12 NULL bytes then 2 last bytes of IID */
      memset(&SICSLOWPAN_IP_BUF->srcipaddr.u8[2], 0, 12);
      memcpy(&SICSLOWPAN_IP_BUF->srcipaddr.u8[14], hc06_ptr, 2);
      hc06_ptr += 2;
      break;
    case SICSLOWPAN_IPHC_SAM_11: /* 0 bits */
      /* setup link-local address */
      SICSLOWPAN_IP_BUF->srcipaddr.u8[0] = 0xfe;
      SICSLOWPAN_IP_BUF->srcipaddr.u8[1] = 0x80;
      /* copy 12 NULL bytes then 8 last bytes from L2 */
      memset(&SICSLOWPAN_IP_BUF->srcipaddr.u8[2], 0, 6);
      /* infer IID from L2 address */
      uip_ds6_set_addr_iid(&SICSLOWPAN_IP_BUF->srcipaddr,
                           (uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_SENDER));
      break;
    }
  }

  /* Destination address */

  /* multicast compression */
  if(RIME_IPHC_BUF[1] & SICSLOWPAN_IPHC_M) {
    /* context based multicast compression */
    if(RIME_IPHC_BUF[1] & SICSLOWPAN_IPHC_DAC) {
      /* TODO: implement this */
    } else {
      /* non-context based multicast compression */
      switch (RIME_IPHC_BUF[1] & SICSLOWPAN_IPHC_DAM_11) {
      case SICSLOWPAN_IPHC_DAM_00: /* 128 bits */
	/* copy whole address from packet */
	memcpy(&SICSLOWPAN_IP_BUF->destipaddr.u8[0], hc06_ptr, 16);
	hc06_ptr += 16;
	break;
      case SICSLOWPAN_IPHC_DAM_01: /* 48 bits FFXX::00XX:XXXX:XXXX */
	SICSLOWPAN_IP_BUF->destipaddr.u8[0] = 0xff;
	SICSLOWPAN_IP_BUF->destipaddr.u8[1] = *hc06_ptr;
	memset(&SICSLOWPAN_IP_BUF->destipaddr.u8[2], 0, 9);
	memcpy(&SICSLOWPAN_IP_BUF->destipaddr.u8[11], hc06_ptr + 1, 5);
	hc06_ptr += 6;
	break;
      case SICSLOWPAN_IPHC_DAM_10: /* 32 bits FFXX::00XX:XXXX */
	SICSLOWPAN_IP_BUF->destipaddr.u8[0] = 0xff;
	SICSLOWPAN_IP_BUF->destipaddr.u8[1] = *hc06_ptr;
	memset(&SICSLOWPAN_IP_BUF->destipaddr.u8[2], 0, 11);
	memcpy(&SICSLOWPAN_IP_BUF->destipaddr.u8[11], hc06_ptr + 1, 3);
	hc06_ptr += 4;
	break;
      case SICSLOWPAN_IPHC_DAM_11: /* 8 bits FF02::00XX */
	SICSLOWPAN_IP_BUF->destipaddr.u8[0] = 0xff;
	SICSLOWPAN_IP_BUF->destipaddr.u8[1] = 0x02;
	memset(&SICSLOWPAN_IP_BUF->destipaddr.u8[2], 0, 13);
	SICSLOWPAN_IP_BUF->destipaddr.u8[15] = *hc06_ptr;
	hc06_ptr++;
	break;
      }
    }
  } else {
    /* no multicast */
    /* Context based */
    if(RIME_IPHC_BUF[1] & SICSLOWPAN_IPHC_DAC) {
      uint8_t dci = (RIME_IPHC_BUF[1] & SICSLOWPAN_IPHC_CID) ?
	RIME_IPHC_BUF[2] & 0x0f : 0;
      context = addr_context_lookup_by_number(dci);

      /* all valid cases below need the context! */
      if(context == NULL) {
	PRINTF("sicslowpan uncompress_hdr: error context not found\n");
	return;
      }

      switch (RIME_IPHC_BUF[1] & SICSLOWPAN_IPHC_DAM_11) {
      case SICSLOWPAN_IPHC_DAM_01: /* 64 bits */
	/* copy prefix from context - rest from packet */
	memcpy(&SICSLOWPAN_IP_BUF->destipaddr, context->prefix, 8);
	memcpy(&SICSLOWPAN_IP_BUF->destipaddr.u8[8], hc06_ptr, 8);
	hc06_ptr += 8;
	break;
      case SICSLOWPAN_IPHC_DAM_10: /* 16 bits */
	/* unicast address */
	memcpy(&SICSLOWPAN_IP_BUF->destipaddr, context->prefix, 8);
	/* copy 6 NULL bytes then 2 last bytes of IID */
	memset(&SICSLOWPAN_IP_BUF->destipaddr.u8[8], 0, 6);
	memcpy(&SICSLOWPAN_IP_BUF->destipaddr.u8[14], hc06_ptr, 2);
	hc06_ptr += 2;
	break;
      case SICSLOWPAN_IPHC_DAM_11: /* 0 bits */
	/* unicast address */
	memcpy(&SICSLOWPAN_IP_BUF->destipaddr, context->prefix, 8);
        uip_ds6_set_addr_iid(&SICSLOWPAN_IP_BUF->destipaddr,
                             (uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_RECEIVER));
	break;
      }      
    } else {
      /* not context based => link local M = 0, DAC = 0 - same as SAC */
      switch (RIME_IPHC_BUF[1] & SICSLOWPAN_IPHC_DAM_11) {
      case SICSLOWPAN_IPHC_DAM_00: /* 128 bits */
	memcpy(&SICSLOWPAN_IP_BUF->destipaddr.u8[0], hc06_ptr, 16);
	hc06_ptr += 16;
	break;
      case SICSLOWPAN_IPHC_DAM_01: /* 64 bits */
	SICSLOWPAN_IP_BUF->destipaddr.u8[0] = 0xfe;
	SICSLOWPAN_IP_BUF->destipaddr.u8[1] = 0x80;
	memset(&SICSLOWPAN_IP_BUF->destipaddr.u8[2], 0, 6);
	memcpy(&SICSLOWPAN_IP_BUF->destipaddr.u8[8], hc06_ptr, 8);
	hc06_ptr += 8;
	break;
      case SICSLOWPAN_IPHC_DAM_10: /* 16 bits */
	SICSLOWPAN_IP_BUF->destipaddr.u8[0] = 0xfe;
	SICSLOWPAN_IP_BUF->destipaddr.u8[1] = 0x80;
	memset(&SICSLOWPAN_IP_BUF->destipaddr.u8[2], 0, 12);
	memcpy(&SICSLOWPAN_IP_BUF->destipaddr.u8[14], hc06_ptr, 2);
	hc06_ptr += 2;
	break;
      case SICSLOWPAN_IPHC_DAM_11: /* 0 bits */
	SICSLOWPAN_IP_BUF->destipaddr.u8[0] = 0xfe;
	SICSLOWPAN_IP_BUF->destipaddr.u8[1] = 0x80;
	memset(&SICSLOWPAN_IP_BUF->destipaddr.u8[2], 0, 6);
	uip_ds6_set_addr_iid(&SICSLOWPAN_IP_BUF->destipaddr,
                             (uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_RECEIVER));
	break;
      }
    }
  }
  uncomp_hdr_len += UIP_IPH_LEN;

  /* Next header processing - continued */
  if((RIME_IPHC_BUF[0] & SICSLOWPAN_IPHC_NH_C)) {
    /* The next header is compressed, NHC is following */
    if((*hc06_ptr & SICSLOWPAN_NDC_UDP_MASK) == SICSLOWPAN_NHC_UDP_ID) {
      SICSLOWPAN_IP_BUF->proto = UIP_PROTO_UDP;
      switch(*hc06_ptr) {
      case SICSLOWPAN_NHC_UDP_C:
	/* 1 byte for NHC, 1 byte for ports, 2 bytes chksum */
	SICSLOWPAN_UDP_BUF->srcport = HTONS(SICSLOWPAN_UDP_PORT_MIN +
					    (*(hc06_ptr + 1) >> 4));
	SICSLOWPAN_UDP_BUF->destport = HTONS(SICSLOWPAN_UDP_PORT_MIN +
					     ((*(hc06_ptr + 1)) & 0x0F));
	memcpy(&SICSLOWPAN_UDP_BUF->udpchksum, hc06_ptr + 2, 2);
	PRINTF("IPHC: Uncompressed UDP ports (4): %x, %x\n",
	       SICSLOWPAN_UDP_BUF->srcport, SICSLOWPAN_UDP_BUF->destport);
	hc06_ptr += 4;
	break;
      case SICSLOWPAN_NHC_UDP_I:
	/* 1 byte for NHC, 4 byte for ports, 2 bytes chksum */
	memcpy(&SICSLOWPAN_UDP_BUF->srcport, hc06_ptr + 1, 2);
	memcpy(&SICSLOWPAN_UDP_BUF->destport, hc06_ptr + 3, 2);
	memcpy(&SICSLOWPAN_UDP_BUF->udpchksum, hc06_ptr + 5, 2);
	PRINTF("IPHC: Uncompressed UDP ports (7): %x, %x\n",
	       SICSLOWPAN_UDP_BUF->srcport, SICSLOWPAN_UDP_BUF->destport);

	hc06_ptr += 7;
	break;
      default:
	PRINTF("sicslowpan uncompress_hdr: error unsupported UDP compression\n");
	return;
      }
      uncomp_hdr_len += UIP_UDPH_LEN;
    }
  }

  rime_hdr_len = hc06_ptr - rime_ptr;
  
  /* IP length field. */
  if(ip_len == 0) {
    /* This is not a fragmented packet */
    SICSLOWPAN_IP_BUF->len[0] = 0;
    SICSLOWPAN_IP_BUF->len[1] = packetbuf_datalen() - rime_hdr_len + uncomp_hdr_len - UIP_IPH_LEN;
  } else {
    /* This is a 1st fragment */
    SICSLOWPAN_IP_BUF->len[0] = (ip_len - UIP_IPH_LEN) >> 8;
    SICSLOWPAN_IP_BUF->len[1] = (ip_len - UIP_IPH_LEN) & 0x00FF;
  }
  
  /* length field in UDP header */
  if(SICSLOWPAN_IP_BUF->proto == UIP_PROTO_UDP) {
    memcpy(&SICSLOWPAN_UDP_BUF->udplen, &SICSLOWPAN_IP_BUF->len[0], 2);
  }

  return;
}
/** @} */
#endif /* SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC06 */


#if SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC1
/*--------------------------------------------------------------------*/
/** \name HC1 compression and uncompression functions
 *  @{                                                                */
/*--------------------------------------------------------------------*/
/**
 * \brief Compress IP/UDP header using HC1 and HC_UDP
 *
 * This function is called by the 6lowpan code to create a compressed
 * 6lowpan packet in the packetbuf buffer from a full IPv6 packet in the
 * uip_buf buffer.
 *
 *
 * If we can compress everything, we use HC1 dispatch, if not we use
 * IPv6 dispatch.\n
 * We can compress everything if:
 *   - IP version is
 *   - Flow label and traffic class are 0
 *   - Both src and dest ip addresses are link local
 *   - Both src and dest interface ID are recoverable from lower layer
 *     header
 *   - Next header is either ICMP, UDP or TCP
 * Moreover, if next header is UDP, we try to compress it using HC_UDP.
 * This is feasible is both ports are between F0B0 and F0B0 + 15\n\n
 *
 * Resulting header structure:
 * - For ICMP, TCP, non compressed UDP\n
 *   HC1 encoding = 11111010 (UDP) 11111110 (TCP) 11111100 (ICMP)\n
 * \verbatim
 *                      1                   2                   3
 * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | LoWPAN HC1 Dsp | HC1 encoding  | IPv6 Hop limit| L4 hdr + data|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | ...
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * \endverbatim
 *
 * - For compressed UDP
 *   HC1 encoding = 11111011, HC_UDP encoding = 11100000\n
 * \verbatim
 *                      1                   2                   3
 * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | LoWPAN HC1 Dsp| HC1 encoding  |  HC_UDP encod.| IPv6 Hop limit|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | src p.| dst p.| UDP checksum                  | L4 data...
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * \endverbatim
 *
 * \param rime_destaddr L2 destination address, needed to compress the
 * IP destination field
 */
static void
compress_hdr_hc1(rimeaddr_t *rime_destaddr)
{
  /*
   * Check if all the assumptions for full compression
   * are valid :
   */
  if(UIP_IP_BUF->vtc != 0x60 ||
     UIP_IP_BUF->tcflow != 0 ||
     UIP_IP_BUF->flow != 0 ||
     !uip_is_addr_link_local(&UIP_IP_BUF->srcipaddr) ||
     !uip_is_addr_mac_addr_based(&UIP_IP_BUF->srcipaddr, &uip_lladdr) ||
     !uip_is_addr_link_local(&UIP_IP_BUF->destipaddr) ||
     !uip_is_addr_mac_addr_based(&UIP_IP_BUF->destipaddr,
                                 (uip_lladdr_t *)rime_destaddr) ||
     (UIP_IP_BUF->proto != UIP_PROTO_ICMP6 &&
      UIP_IP_BUF->proto != UIP_PROTO_UDP &&
      UIP_IP_BUF->proto != UIP_PROTO_TCP))
  {
    /*
     * IPV6 DISPATCH
     * Something cannot be compressed, use IPV6 DISPATCH,
     * compress nothing, copy IPv6 header in rime buffer
     */
    *rime_ptr = SICSLOWPAN_DISPATCH_IPV6;
    rime_hdr_len += SICSLOWPAN_IPV6_HDR_LEN;
    memcpy(rime_ptr + rime_hdr_len, UIP_IP_BUF, UIP_IPH_LEN);
    rime_hdr_len += UIP_IPH_LEN;
    uncomp_hdr_len += UIP_IPH_LEN;
  } else {
    /*
     * HC1 DISPATCH
     * maximum compresssion:
     * All fields in the IP header but Hop Limit are elided
     * If next header is UDP, we compress UDP header using HC2
     */
/*     RIME_HC1_BUF->dispatch = SICSLOWPAN_DISPATCH_HC1; */
    RIME_HC1_PTR[RIME_HC1_DISPATCH] = SICSLOWPAN_DISPATCH_HC1;
    uncomp_hdr_len += UIP_IPH_LEN;
    switch(UIP_IP_BUF->proto) {
      case UIP_PROTO_ICMP6:
        /* HC1 encoding and ttl */
/*         RIME_HC1_BUF->encoding = 0xFC; */
        RIME_HC1_PTR[RIME_HC1_ENCODING] = 0xFC;
/*         RIME_HC1_BUF->ttl = UIP_IP_BUF->ttl; */
        RIME_HC1_PTR[RIME_HC1_TTL] = UIP_IP_BUF->ttl;
        rime_hdr_len += SICSLOWPAN_HC1_HDR_LEN;
        break;
#if UIP_CONF_TCP
      case UIP_PROTO_TCP:
        /* HC1 encoding and ttl */
/*         RIME_HC1_BUF->encoding = 0xFE; */
        RIME_HC1_PTR[RIME_HC1_ENCODING] = 0xFE;
/*         RIME_HC1_BUF->ttl = UIP_IP_BUF->ttl; */
        RIME_HC1_PTR[RIME_HC1_TTL] = UIP_IP_BUF->ttl;
        rime_hdr_len += SICSLOWPAN_HC1_HDR_LEN;
        break;
#endif /* UIP_CONF_TCP */
#if UIP_CONF_UDP
      case UIP_PROTO_UDP:
        /*
         * try to compress UDP header (we do only full compression).
         * This is feasible if both src and dest ports are between
         * SICSLOWPAN_UDP_PORT_MIN and SICSLOWPAN_UDP_PORT_MIN + 15
         */
        PRINTF("local/remote port %u/%u\n",UIP_UDP_BUF->srcport,UIP_UDP_BUF->destport);
        if(HTONS(UIP_UDP_BUF->srcport)  >= SICSLOWPAN_UDP_PORT_MIN &&
           HTONS(UIP_UDP_BUF->srcport)  <  SICSLOWPAN_UDP_PORT_MAX &&
           HTONS(UIP_UDP_BUF->destport) >= SICSLOWPAN_UDP_PORT_MIN &&
           HTONS(UIP_UDP_BUF->destport) <  SICSLOWPAN_UDP_PORT_MAX) {
          /* HC1 encoding */
/*           RIME_HC1_HC_UDP_BUF->hc1_encoding = 0xFB; */
          RIME_HC1_HC_UDP_PTR[RIME_HC1_HC_UDP_HC1_ENCODING] = 0xFB;
        
          /* HC_UDP encoding, ttl, src and dest ports, checksum */
/*           RIME_HC1_HC_UDP_BUF->hc_udp_encoding = 0xE0; */
          RIME_HC1_HC_UDP_PTR[RIME_HC1_HC_UDP_UDP_ENCODING] = 0xE0;
/*           RIME_HC1_HC_UDP_BUF->ttl = UIP_IP_BUF->ttl; */
          RIME_HC1_HC_UDP_PTR[RIME_HC1_HC_UDP_TTL] = UIP_IP_BUF->ttl;
/*           RIME_HC1_HC_UDP_BUF->ports = (u8_t)((HTONS(UIP_UDP_BUF->srcport) - */
/*                                                SICSLOWPAN_UDP_PORT_MIN) << 4) + */
/*             (u8_t)((HTONS(UIP_UDP_BUF->destport) - SICSLOWPAN_UDP_PORT_MIN)); */
          RIME_HC1_HC_UDP_PTR[RIME_HC1_HC_UDP_PORTS] =
               (u8_t)((HTONS(UIP_UDP_BUF->srcport) -
                       SICSLOWPAN_UDP_PORT_MIN) << 4) +
               (u8_t)((HTONS(UIP_UDP_BUF->destport) - SICSLOWPAN_UDP_PORT_MIN));
/*           RIME_HC1_HC_UDP_BUF->udpchksum = UIP_UDP_BUF->udpchksum; */
          memcpy(&RIME_HC1_HC_UDP_PTR[RIME_HC1_HC_UDP_CHKSUM], &UIP_UDP_BUF->udpchksum, 2);
          rime_hdr_len += SICSLOWPAN_HC1_HC_UDP_HDR_LEN;
          uncomp_hdr_len += UIP_UDPH_LEN;
        } else {
          /* HC1 encoding and ttl */
/*           RIME_HC1_BUF->encoding = 0xFA; */
          RIME_HC1_PTR[RIME_HC1_ENCODING] = 0xFA;
/*           RIME_HC1_BUF->ttl = UIP_IP_BUF->ttl; */
          RIME_HC1_PTR[RIME_HC1_TTL] = UIP_IP_BUF->ttl;
          rime_hdr_len += SICSLOWPAN_HC1_HDR_LEN;
        }
        break;
#endif /*UIP_CONF_UDP*/
    }
  }
  return;
}

/*--------------------------------------------------------------------*/
/**
 * \brief Uncompress HC1 (and HC_UDP) headers and put them in
 * sicslowpan_buf
 *
 * This function is called by the input function when the dispatch is
 * HC1.
 * We %process the packet in the rime buffer, uncompress the header
 * fields, and copy the result in the sicslowpan buffer.
 * At the end of the decompression, rime_hdr_len and uncompressed_hdr_len
 * are set to the appropriate values
 *
 * \param ip_len Equal to 0 if the packet is not a fragment (IP length
 * is then inferred from the L2 length), non 0 if the packet is a 1st
 * fragment.
 */
static void
uncompress_hdr_hc1(u16_t ip_len) {
  /* version, traffic class, flow label */
  SICSLOWPAN_IP_BUF->vtc = 0x60;
  SICSLOWPAN_IP_BUF->tcflow = 0;
  SICSLOWPAN_IP_BUF->flow = 0;
  
  /* src and dest ip addresses */
  uip_ip6addr(&SICSLOWPAN_IP_BUF->srcipaddr, 0xfe80, 0, 0, 0, 0, 0, 0, 0);
  uip_ds6_set_addr_iid(&SICSLOWPAN_IP_BUF->srcipaddr,
                       (uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_SENDER));
  uip_ip6addr(&SICSLOWPAN_IP_BUF->destipaddr, 0xfe80, 0, 0, 0, 0, 0, 0, 0);
  uip_ds6_set_addr_iid(&SICSLOWPAN_IP_BUF->destipaddr,
                       (uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_RECEIVER));
  
  uncomp_hdr_len += UIP_IPH_LEN;
  
  /* Next header field */
/* switch(RIME_HC1_BUF->encoding & 0x06) { */
  switch(RIME_HC1_PTR[RIME_HC1_ENCODING] & 0x06) {
    case SICSLOWPAN_HC1_NH_ICMP6:
      SICSLOWPAN_IP_BUF->proto = UIP_PROTO_ICMP6;
/*       SICSLOWPAN_IP_BUF->ttl = RIME_HC1_BUF->ttl; */
      SICSLOWPAN_IP_BUF->ttl = RIME_HC1_PTR[RIME_HC1_TTL];
      rime_hdr_len += SICSLOWPAN_HC1_HDR_LEN;
      break;
#if UIP_CONF_TCP
    case SICSLOWPAN_HC1_NH_TCP:
      SICSLOWPAN_IP_BUF->proto = UIP_PROTO_TCP;
/*       SICSLOWPAN_IP_BUF->ttl = RIME_HC1_BUF->ttl; */
      SICSLOWPAN_IP_BUF->ttl = RIME_HC1_PTR[RIME_HC1_TTL];
      rime_hdr_len += SICSLOWPAN_HC1_HDR_LEN;
      break;
#endif/* UIP_CONF_TCP */
#if UIP_CONF_UDP
    case SICSLOWPAN_HC1_NH_UDP:
      SICSLOWPAN_IP_BUF->proto = UIP_PROTO_UDP;
/*       if(RIME_HC1_HC_UDP_BUF->hc1_encoding & 0x01) { */
      if(RIME_HC1_HC_UDP_PTR[RIME_HC1_HC_UDP_HC1_ENCODING] & 0x01) {
        /* UDP header is compressed with HC_UDP */
/*         if(RIME_HC1_HC_UDP_BUF->hc_udp_encoding != */
        if(RIME_HC1_HC_UDP_PTR[RIME_HC1_HC_UDP_UDP_ENCODING] !=
           SICSLOWPAN_HC_UDP_ALL_C) {
          PRINTF("sicslowpan (uncompress_hdr), packet not supported");
          return;
        }
        /* IP TTL */
/*         SICSLOWPAN_IP_BUF->ttl = RIME_HC1_HC_UDP_BUF->ttl; */
        SICSLOWPAN_IP_BUF->ttl = RIME_HC1_HC_UDP_PTR[RIME_HC1_HC_UDP_TTL];
        /* UDP ports, len, checksum */
/*         SICSLOWPAN_UDP_BUF->srcport = HTONS(SICSLOWPAN_UDP_PORT_MIN + */
/*                                            (RIME_HC1_HC_UDP_BUF->ports >> 4)); */
        SICSLOWPAN_UDP_BUF->srcport =
          HTONS(SICSLOWPAN_UDP_PORT_MIN +
                (RIME_HC1_HC_UDP_PTR[RIME_HC1_HC_UDP_PORTS] >> 4));
/*         SICSLOWPAN_UDP_BUF->destport = HTONS(SICSLOWPAN_UDP_PORT_MIN + */
/*                                              (RIME_HC1_HC_UDP_BUF->ports & 0x0F)); */
        SICSLOWPAN_UDP_BUF->destport =
          HTONS(SICSLOWPAN_UDP_PORT_MIN +
                (RIME_HC1_HC_UDP_PTR[RIME_HC1_HC_UDP_PORTS] & 0x0F));
/*         SICSLOWPAN_UDP_BUF->udpchksum = RIME_HC1_HC_UDP_BUF->udpchksum; */
        memcpy(&SICSLOWPAN_UDP_BUF->udpchksum, &RIME_HC1_HC_UDP_PTR[RIME_HC1_HC_UDP_CHKSUM], 2);
        uncomp_hdr_len += UIP_UDPH_LEN;
        rime_hdr_len += SICSLOWPAN_HC1_HC_UDP_HDR_LEN;
      } else {
        rime_hdr_len += SICSLOWPAN_HC1_HDR_LEN;
      }
      break;
#endif/* UIP_CONF_UDP */
    default:
      /* this shouldn't happen, drop */
      return;
  }
  
  /* IP length field. */
  if(ip_len == 0) {
    /* This is not a fragmented packet */
    SICSLOWPAN_IP_BUF->len[0] = 0;
    SICSLOWPAN_IP_BUF->len[1] = packetbuf_datalen() - rime_hdr_len + uncomp_hdr_len - UIP_IPH_LEN;
  } else {
    /* This is a 1st fragment */
    SICSLOWPAN_IP_BUF->len[0] = (ip_len - UIP_IPH_LEN) >> 8;
    SICSLOWPAN_IP_BUF->len[1] = (ip_len - UIP_IPH_LEN) & 0x00FF;
  }
  /* length field in UDP header */
  if(SICSLOWPAN_IP_BUF->proto == UIP_PROTO_UDP) {
    memcpy(&SICSLOWPAN_UDP_BUF->udplen, &SICSLOWPAN_IP_BUF->len[0], 2);
  }
  return;
}
/** @} */
#endif /* SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC1 */


#if SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_IPV6
/*--------------------------------------------------------------------*/
/** \name IPv6 dispatch "compression" function
 * @{                                                                 */
/*--------------------------------------------------------------------*/
/* \brief Packets "Compression" when only IPv6 dispatch is used
 *
 * There is no compression in this case, all fields are sent
 * inline. We just add the IPv6 dispatch byte before the packet.
 * \verbatim
 * 0               1                   2                   3
 * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | IPv6 Dsp      | IPv6 header and payload ...
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * \endverbatim
 */
static void
compress_hdr_ipv6(rimeaddr_t *rime_destaddr) {
  *rime_ptr = SICSLOWPAN_DISPATCH_IPV6;
  rime_hdr_len += SICSLOWPAN_IPV6_HDR_LEN;
  memcpy(rime_ptr + rime_hdr_len, UIP_IP_BUF, UIP_IPH_LEN);
  rime_hdr_len += UIP_IPH_LEN;
  uncomp_hdr_len += UIP_IPH_LEN;
  return;
}
/** @} */
#endif /* SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_IPV6 */

 

/*--------------------------------------------------------------------*/
/** \name Input/output functions common to all compression schemes
 * @{                                                                 */
/*--------------------------------------------------------------------*/
/**
 * \brief This function is called by the 6lowpan code to send out a
 * packet.
 * \param dest the link layer destination address of the packet
 */
static void
send_packet(rimeaddr_t *dest)
{

  /* Set the link layer destination address for the packet as a
   * packetbuf attribute. The MAC layer can access the destination
   * address with the function packetbuf_addr(PACKETBUF_ADDR_RECEIVER).
   */
  packetbuf_set_addr(PACKETBUF_ADDR_RECEIVER, dest);

  /* XXX we should provide a callback function that is called when the
     packet is sent. For now, we just supply a NULL pointer. */
  NETSTACK_MAC.send(NULL, NULL);

  /* If we are sending multiple packets in a row, we need to let the
     watchdog know that we are still alive. */
  watchdog_periodic();
}

/** \brief Take an IP packet and format it to be sent on an 802.15.4
 *  network using 6lowpan.
 *  \param localdest The MAC address of the destination
 *
 *  The IP packet is initially in uip_buf. Its header is compressed
 *  and if necessary it is fragmented. The resulting
 *  packet/fragments are put in packetbuf and delivered to the 802.15.4
 *  MAC.
 */
static u8_t
output(uip_lladdr_t *localdest)
{
  /* The MAC address of the destination of the packet */
  rimeaddr_t dest;
  

  /* init */
  uncomp_hdr_len = 0;
  rime_hdr_len = 0;

  /* reset rime buffer */
  packetbuf_clear();
  rime_ptr = packetbuf_dataptr();

  /* ack */
  packetbuf_set_attr(PACKETBUF_ATTR_MAX_MAC_REXMIT, 2);
 
  if(UIP_IP_BUF->proto == UIP_PROTO_TCP) {
    /*    packetbuf_set_attr(PACKETBUF_ATTR_PACKET_TYPE,
          PACKETBUF_ATTR_PACKET_TYPE_STREAM);*/
  }
    
  /*
   * The destination address will be tagged to each outbound
   * packet. If the argument localdest is NULL, we are sending a
   * broadcast packet.
   */
  if(localdest == NULL) {
    rimeaddr_copy(&dest, &rimeaddr_null);
  } else {
    packetbuf_set_attr(PACKETBUF_ATTR_RELIABLE, 1);  
    rimeaddr_copy(&dest, (const rimeaddr_t *)localdest);
  }
  
  PRINTFO("sicslowpan output: sending packet len %d\n", uip_len);
  
  /* Try to compress the headers */
#if SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC1
  compress_hdr_hc1(&dest);
#endif /* SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC1 */
#if SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_IPV6
  compress_hdr_ipv6(&dest);
#endif /* SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_IPV6 */
#if SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC06
  compress_hdr_hc06(&dest);
#endif /* SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC06 */
  PRINTFO("sicslowpan output: header of len %d\n", rime_hdr_len);
  
  if(uip_len - uncomp_hdr_len > MAC_MAX_PAYLOAD - rime_hdr_len) {
#if SICSLOWPAN_CONF_FRAG
    struct queuebuf *q;
    /*
     * The outbound IPv6 packet is too large to fit into a single 15.4
     * packet, so we fragment it into multiple packets and send them.
     * The first fragment contains frag1 dispatch, then
     * IPv6/HC1/HC06/HC_UDP dispatchs/headers.
     * The following fragments contain only the fragn dispatch.
     */

    /* Create 1st Fragment */
    PRINTFO("sicslowpan output: 1rst fragment ");

    /* move HC1/HC06/IPv6 header */
    memmove(rime_ptr + SICSLOWPAN_FRAG1_HDR_LEN, rime_ptr, rime_hdr_len);

    /*
     * FRAG1 dispatch + header
     * Note that the length is in units of 8 bytes
     */
/*     RIME_FRAG_BUF->dispatch_size = */
/*       htons((SICSLOWPAN_DISPATCH_FRAG1 << 8) | uip_len); */
    SET16(RIME_FRAG_PTR, RIME_FRAG_DISPATCH_SIZE,
          ((SICSLOWPAN_DISPATCH_FRAG1 << 8) | uip_len));
/*     RIME_FRAG_BUF->tag = htons(my_tag); */
    SET16(RIME_FRAG_PTR, RIME_FRAG_TAG, my_tag);

    /* Copy payload and send */
    rime_hdr_len += SICSLOWPAN_FRAG1_HDR_LEN;
    rime_payload_len = (MAC_MAX_PAYLOAD - rime_hdr_len) & 0xf8;
    PRINTFO("(len %d, tag %d)\n", rime_payload_len, my_tag);
    memcpy(rime_ptr + rime_hdr_len,
           (void *)UIP_IP_BUF + uncomp_hdr_len, rime_payload_len);
    packetbuf_set_datalen(rime_payload_len + rime_hdr_len);
    q = queuebuf_new_from_packetbuf();
    if(q == NULL) {
      PRINTFO("could not allocate queuebuf for first fragment, dropping packet\n");
      return 0;
    }
    send_packet(&dest);
    queuebuf_to_packetbuf(q);
    queuebuf_free(q);
    q = NULL;

    /* set processed_ip_len to what we already sent from the IP payload*/
    processed_ip_len = rime_payload_len + uncomp_hdr_len;
    
    /*
     * Create following fragments
     * Datagram tag is already in the buffer, we need to set the
     * FRAGN dispatch and for each fragment, the offset
     */
    rime_hdr_len = SICSLOWPAN_FRAGN_HDR_LEN;
/*     RIME_FRAG_BUF->dispatch_size = */
/*       htons((SICSLOWPAN_DISPATCH_FRAGN << 8) | uip_len); */
    SET16(RIME_FRAG_PTR, RIME_FRAG_DISPATCH_SIZE,
          ((SICSLOWPAN_DISPATCH_FRAGN << 8) | uip_len));
    rime_payload_len = (MAC_MAX_PAYLOAD - rime_hdr_len) & 0xf8;
    while(processed_ip_len < uip_len){
      PRINTFO("sicslowpan output: fragment ");
/*       RIME_FRAG_BUF->offset = processed_ip_len >> 3; */
      RIME_FRAG_PTR[RIME_FRAG_OFFSET] = processed_ip_len >> 3;
      
      /* Copy payload and send */
      if(uip_len - processed_ip_len < rime_payload_len){
        /* last fragment */
        rime_payload_len = uip_len - processed_ip_len;
      }
      PRINTFO("(offset %d, len %d, tag %d)\n",
             processed_ip_len >> 3, rime_payload_len, my_tag);
      memcpy(rime_ptr + rime_hdr_len,
             (void *)UIP_IP_BUF + processed_ip_len, rime_payload_len);
      packetbuf_set_datalen(rime_payload_len + rime_hdr_len);
      q = queuebuf_new_from_packetbuf();
      if(q == NULL) {
        PRINTFO("could not allocate queuebuf, dropping fragment\n");
        return 0;
      }
      send_packet(&dest);
      queuebuf_to_packetbuf(q);
      queuebuf_free(q);
      q = NULL;
      processed_ip_len += rime_payload_len;
    }
    
    /* end: reset global variables  */
    my_tag++;
    processed_ip_len = 0;
#else /* SICSLOWPAN_CONF_FRAG */
    PRINTFO("sicslowpan output: Packet too large to be sent without fragmentation support; dropping packet\n");
    return 0;
#endif /* SICSLOWPAN_CONF_FRAG */
  } else {
    /*
     * The packet does not need to be fragmented
     * copy "payload" and send
     */
    memcpy(rime_ptr + rime_hdr_len, (void *)UIP_IP_BUF + uncomp_hdr_len,
           uip_len - uncomp_hdr_len);
    packetbuf_set_datalen(uip_len - uncomp_hdr_len + rime_hdr_len);
    send_packet(&dest);
  }
  return 1;
}

/*--------------------------------------------------------------------*/
/** \brief Process a received 6lowpan packet.
 *  \param r The MAC layer
 *
 *  The 6lowpan packet is put in packetbuf by the MAC. If its a frag1 or
 *  a non-fragmented packet we first uncompress the IP header. The
 *  6lowpan payload and possibly the uncompressed IP header are then
 *  copied in siclowpan_buf. If the IP packet is complete it is copied
 *  to uip_buf and the IP layer is called.
 *
 * \note We do not check for overlapping sicslowpan fragments
 * (it is a SHALL in the RFC 4944 and should never happen)
 */
static void
input(void)
{
  /* size of the IP packet (read from fragment) */
  u16_t frag_size = 0;
  /* offset of the fragment in the IP packet */
  u8_t frag_offset = 0;
#if SICSLOWPAN_CONF_FRAG
  /* tag of the fragment */
  u16_t frag_tag = 0;
#endif /*SICSLOWPAN_CONF_FRAG*/

  /* init */
  uncomp_hdr_len = 0;
  rime_hdr_len = 0;

  /* The MAC puts the 15.4 payload inside the RIME data buffer */
  rime_ptr = packetbuf_dataptr();

#if SICSLOWPAN_CONF_FRAG
  /* if reassembly timed out, cancel it */
  if(timer_expired(&reass_timer)){
    sicslowpan_len = 0;
    processed_ip_len = 0;
  }
  /*
   * Since we don't support the mesh and broadcast header, the first header
   * we look for is the fragmentation header
   */
/*   switch((ntohs(RIME_FRAG_BUF->dispatch_size) & 0xf800) >> 8) { */
  switch((GET16(RIME_FRAG_PTR, RIME_FRAG_DISPATCH_SIZE) & 0xf800) >> 8) {
    case SICSLOWPAN_DISPATCH_FRAG1:
      PRINTFI("sicslowpan input: FRAG1 ");
      frag_offset = 0;
/*       frag_size = (ntohs(RIME_FRAG_BUF->dispatch_size) & 0x07ff); */
      frag_size = GET16(RIME_FRAG_PTR, RIME_FRAG_DISPATCH_SIZE) & 0x07ff;
/*       frag_tag = ntohs(RIME_FRAG_BUF->tag); */
      frag_tag = GET16(RIME_FRAG_PTR, RIME_FRAG_TAG);
      PRINTFI("size %d, tag %d, offset %d)\n",
             frag_size, frag_tag, frag_offset);
      rime_hdr_len += SICSLOWPAN_FRAG1_HDR_LEN;
	  
      break;
    case SICSLOWPAN_DISPATCH_FRAGN:
      /*
       * set offset, tag, size
       * Offset is in units of 8 bytes
       */
      PRINTFI("sicslowpan input: FRAGN ");
/*       frag_offset = RIME_FRAG_BUF->offset; */
      frag_offset = RIME_FRAG_PTR[RIME_FRAG_OFFSET];
/*       frag_tag = ntohs(RIME_FRAG_BUF->tag); */
      frag_tag = GET16(RIME_FRAG_PTR, RIME_FRAG_TAG);
/*       frag_size = (ntohs(RIME_FRAG_BUF->dispatch_size) & 0x07ff); */
      frag_size = GET16(RIME_FRAG_PTR, RIME_FRAG_DISPATCH_SIZE) & 0x07ff;
      PRINTFI("size %d, tag %d, offset %d)\n",
             frag_size, frag_tag, frag_offset);
      rime_hdr_len += SICSLOWPAN_FRAGN_HDR_LEN;
      break;
    default:
      break;
  }

  if(processed_ip_len > 0) {
    /* reassembly is ongoing */
    if((frag_size > 0 &&
        (frag_size != sicslowpan_len ||
         reass_tag  != frag_tag ||
         !rimeaddr_cmp(&frag_sender, packetbuf_addr(PACKETBUF_ADDR_SENDER))))  ||
       frag_size == 0) {
      /*
       * the packet is a fragment that does not belong to the packet
       * being reassembled or the packet is not a fragment.
       */
      PRINTFI("sicslowpan input: Dropping 6lowpan packet that is not a fragment of the packet currently being reassembled\n");
      return;
    }
  } else {
    /*
     * reassembly is off
     * start it if we received a fragment
     */
    if(frag_size > 0){
      sicslowpan_len = frag_size;
      reass_tag = frag_tag;
      timer_set(&reass_timer, SICSLOWPAN_REASS_MAXAGE*CLOCK_SECOND);
      PRINTFI("sicslowpan input: INIT FRAGMENTATION (len %d, tag %d)\n",
             sicslowpan_len, reass_tag);
      rimeaddr_copy(&frag_sender, packetbuf_addr(PACKETBUF_ADDR_SENDER));
    }
  }

  if(rime_hdr_len == SICSLOWPAN_FRAGN_HDR_LEN) {
    /* this is a FRAGN, skip the header compression dispatch section */
    goto copypayload;
  }
#endif /* SICSLOWPAN_CONF_FRAG */

  /* Process next dispatch and headers */
/*   switch(RIME_HC1_BUF->dispatch) { */
#if SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC06
  if((RIME_HC1_PTR[RIME_HC1_DISPATCH] & 0xe0) == SICSLOWPAN_DISPATCH_IPHC) {
    PRINTFI("sicslowpan input: IPHC\n");
    uncompress_hdr_hc06(frag_size);
  } else
#endif /* SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC06 */
    switch(RIME_HC1_PTR[RIME_HC1_DISPATCH]) {
#if SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC1
    case SICSLOWPAN_DISPATCH_HC1:
      PRINTFI("sicslowpan input: HC1\n");
      uncompress_hdr_hc1(frag_size);
      break;
#endif /* SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC1 */
    case SICSLOWPAN_DISPATCH_IPV6:
      PRINTFI("sicslowpan input: IPV6\n");
      rime_hdr_len += SICSLOWPAN_IPV6_HDR_LEN;

      /* Put uncompressed IP header in sicslowpan_buf. */
      memcpy(SICSLOWPAN_IP_BUF, rime_ptr + rime_hdr_len, UIP_IPH_LEN);

      /* Update uncomp_hdr_len and rime_hdr_len. */
      rime_hdr_len += UIP_IPH_LEN;
      uncomp_hdr_len += UIP_IPH_LEN;
      break;
    default:
      /* unknown header */
/*       PRINTF("sicslowpan input: unknown dispatch\n"); */
      PRINTFI("sicslowpan input: unknown dispatch: %u\n",
             RIME_HC1_PTR[RIME_HC1_DISPATCH]);
      return;
  }
   
    
#if SICSLOWPAN_CONF_FRAG
 copypayload:
#endif /*SICSLOWPAN_CONF_FRAG*/
  /*
   * copy "payload" from the rime buffer to the sicslowpan_buf
   * if this is a first fragment or not fragmented packet,
   * we have already copied the compressed headers, uncomp_hdr_len
   * and rime_hdr_len are non 0, frag_offset is.
   * If this is a subsequent fragment, this is the contrary.
   */
  rime_payload_len = packetbuf_datalen() - rime_hdr_len;
  memcpy((void *)SICSLOWPAN_IP_BUF + uncomp_hdr_len + (u16_t)(frag_offset << 3), rime_ptr + rime_hdr_len, rime_payload_len);
  
  /* update processed_ip_len if fragment, sicslowpan_len otherwise */

#if SICSLOWPAN_CONF_FRAG
  if(frag_size > 0){
    if(processed_ip_len == 0) {
      processed_ip_len += uncomp_hdr_len;
    }
    processed_ip_len += rime_payload_len;
  } else {
#endif /* SICSLOWPAN_CONF_FRAG */
    sicslowpan_len = rime_payload_len + uncomp_hdr_len;
#if SICSLOWPAN_CONF_FRAG
  }

  /*
   * If we have a full IP packet in sicslowpan_buf, deliver it to
   * the IP stack
   */
  if(processed_ip_len == 0 || (processed_ip_len == sicslowpan_len)){
    PRINTFI("sicslowpan input: IP packet ready (length %d)\n",
           sicslowpan_len);
    memcpy((void *)UIP_IP_BUF, (void *)SICSLOWPAN_IP_BUF, sicslowpan_len);
    uip_len = sicslowpan_len;
    sicslowpan_len = 0;
    processed_ip_len = 0;
#endif /* SICSLOWPAN_CONF_FRAG */

#if DEBUG
    {
      uint8_t tmp;
      PRINTF("after decompression: ");
      for (tmp = 0; tmp < SICSLOWPAN_IP_BUF->len[1] + 40; tmp++) {
	uint8_t data = ((uint8_t *) (SICSLOWPAN_IP_BUF))[tmp];
	PRINTF("%02x", data);
      }
      PRINTF("\n");
    }
#endif

    tcpip_input();
#if SICSLOWPAN_CONF_FRAG
  }
#endif /* SICSLOWPAN_CONF_FRAG */
}
/** @} */

/*--------------------------------------------------------------------*/
/* \brief 6lowpan init function (called by the MAC layer)             */
/*--------------------------------------------------------------------*/
void
sicslowpan_init(void)
{
  /* remember the mac driver */
  sicslowpan_mac = &NETSTACK_MAC;

  /*
   * Set out output function as the function to be called from uIP to
   * send a packet.
   */
  tcpip_set_outputfunc(output);

#if SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC06
#if SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS < 1
#error sicslowpan compression HC06 requires at least one address context.
#error Change SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS in contiki-conf.h.
#endif

  /*
   * Initialize the address contexts
   * Context 00 is not anymore link local context since
   * there is built-in support for link-local compression
   * in hc-06
   * - should we be able to have longer contexts than 64 bits?
   * currently 64 bits length is supported...
   */
  addr_contexts[0].used = 0; //1 mathilde
  addr_contexts[0].number = 0;
  addr_contexts[0].prefix[0] = 0xaa; 
  addr_contexts[0].prefix[1] = 0xaa;
  
#if SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS > 1
  for(i = 1; i < SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS; i++) {
    addr_contexts[i].used = 0;
  }
#endif /* SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS > 1 */

#endif /* SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC06 */
}
/*--------------------------------------------------------------------*/
const struct network_driver sicslowpan_driver = {
  "sicslowpan",
  sicslowpan_init,
  input
};
/*--------------------------------------------------------------------*/
/** @} */
