/*
 * Copyright (c) 2009, Swedish Institute of Computer Science.
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
 */

/**
 * \file
 *         Packet queue for ccmac. Use in the MAC layer.
 * \author
 *         Mat Wymore <mlwymore@iastate.edu>
 */

#include "net/mac/ccmac/ccmac-packetqueue.h"
#include "net/packetbuf.h"
#include "net/mac/rdc.h"
#include "net/queuebuf.h"
#include "net/netstack.h"
#include "lib/memb.h"
#include "lib/list.h"
#include "lib/random.h"

#ifndef QUEUEBUF_NUM
#define QUEUEBUF_NUM 10
#endif
#ifndef QUEUEBUF_THRESHOLD
#define QUEUEBUF_THRESHOLD QUEUEBUF_NUM / 2
#endif
#define MAX_QUEUED_PACKETS QUEUEBUF_NUM
#define MAX_MAC_TRANSMISSIONS 3

#define DEBUG 1

#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINTADDR(addr) PRINTF(" %02x%02x:%02x%02x:%02x%02x:%02x%02x ", ((uint8_t *)addr)[0], ((uint8_t *)addr)[1], ((uint8_t *)addr)[2], ((uint8_t *)addr)[3], ((uint8_t *)addr)[4], ((uint8_t *)addr)[5], ((uint8_t *)addr)[6], ((uint8_t *)addr)[7])
#else
#define PRINTF(...)
#define PRINTADDR(addr)
#endif

static int off(int keep_radio_on);

struct qbuf_metadata {
  mac_callback_t sent;
  void *cptr;
  uint8_t max_transmissions;
};

MEMB(packet_memb, struct rdc_buf_list, MAX_QUEUED_PACKETS);
MEMB(metadata_memb, struct qbuf_metadata, MAX_QUEUED_PACKETS);
LIST(packet_list);

static void packet_done(struct rdc_buf_list *packet, int status) {
  mac_callback_t sent_callback;
  struct qbuf_metadata *metadata;
  void *cptr;

  metadata = (struct qbuf_metadata *)packet->ptr;
  sent_callback = metadata->sent;
  cptr = metadata->cptr;

  list_remove(packet_list, packet);
  queuebuf_free(packet->buf);
  memb_free(&metadata_memb, metadata);
  memb_free(&packet_memb, packet);
  //TODO: correctly track number of transmissions
  mac_call_sent_callback(sent_callback, cptr, status, 1);
  return;
}

static void packet_sent(void *ptr, int status, int num_transmissions) {
  struct rdc_buf_list *packet;

  for (packet = list_head(packet_list); packet != NULL; packet = list_item_next(packet_list)) {
    if (queuebuf_attr(packet->buf, PACKETBUF_ATTR_MAC_SEQNO) == packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO)) {
      break;
    }
  }

  if (packet == NULL) {
    PRINTF("ccmac-packetqueue: seqno %d not found\n", packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO));
    return;
  }
  else if (packet->ptr == NULL) {
    PRINTF("ccmac-packetqueue: no metadata\n");
    return;
  }

  switch(status) {
  case MAC_TX_OK:
    packet_done(packet, status);
    break;
  case MAC_TX_NOACK:
    packet_done(packet, status);
    break;
  case MAC_TX_COLLISION:
    break;
  case MAC_TX_DEFERRED:
    break;
  case MAC_TX_ERR_FATAL:
    off(0);
  default:
    packet_done(packet, status);
  }
}

/*---------------------------------------------------------------------------*/
static void init(void) {
  memb_init(&packet_memb);
  memb_init(&metadata_memb);
  list_init(packet_list);
}

static void send(mac_callback_t sent_callback, void *ptr) {
  static uint16_t seqno;
  static uint8_t initialized = 0;

  /* As per csma.c */
  if (!initialized) {
    initialized = 1;
    seqno = random_rand();
  }
  if (seqno == 0) {
    seqno++;
  }
  packetbuf_set_attr(PACKETBUF_ATTR_MAC_SEQNO, seqno++);

  struct rdc_buf_list *packet;
  if (list_length(packet_list) < MAX_QUEUED_PACKETS) {
    packet = memb_alloc(&packet_memb);
    if (packet != NULL) {
      packet->ptr = memb_alloc(&metadata_memb);
      if (packet->ptr != NULL) {
        packet->buf = queuebuf_new_from_packetbuf();
        if (packet->buf != NULL) {
          struct qbuf_metadata *metadata = (struct qbuf_metadata *)packet->ptr;
          if (packetbuf_attr(PACKETBUF_ATTR_MAX_MAC_TRANSMISSIONS) == 0) {
            metadata->max_transmissions = MAX_MAC_TRANSMISSIONS;
          }
          else {
            metadata->max_transmissions = packetbuf_attr(PACKETBUF_ATTR_MAX_MAC_TRANSMISSIONS);
          }
          metadata->sent = sent_callback;
          metadata->cptr = ptr;
          list_add(packet_list, packet);
          //TODO: when should send?
          if (list_length(packet_list) >= QUEUEBUF_THRESHOLD) {
            PRINTF("ccmac-packetqueue: sending %d packets, threshold %d\n", list_length(packet_list), QUEUEBUF_THRESHOLD);
            NETSTACK_RDC.send_list(packet_sent, ptr, list_head(packet_list));
          }
          return;
        }
        memb_free(&metadata_memb, packet->ptr);
        PRINTF("ccmac-packetqueue: failed - undoing metadata alloc\n");
      }
      memb_free(&packet_memb, packet);
      PRINTF("ccmac-packetqueue: failed - undoing packet alloc\n");
    }
  }
  else {
    PRINTF("ccmac-packetqueue: packet queue full\n");
  }
  
  mac_call_sent_callback(sent_callback, ptr, MAC_TX_ERR, 1);
  return;
}

static void input(void) {
  NETSTACK_LLSEC.input();
}

static int on(void) {
  return NETSTACK_RDC.on();
}

static int off(int keep_radio_on) {
  /*struct rdc_buf_list *curr;
  struct rdc_buf_list *next;
  curr = list_head(packets);
  while (curr != NULL) {
    next = list_item_next(curr);
    memb_free(&packet_memb, curr);
    curr = next;
  }*/
  return NETSTACK_RDC.off(keep_radio_on);
}

static unsigned short channel_check_interval(void) {
  return NETSTACK_RDC.channel_check_interval();
}

const struct mac_driver ccmac_packetqueue_driver = {
  "ccmac-packetqueue",
  init,
  send,
  input,
  on,
  off,
  channel_check_interval
};
