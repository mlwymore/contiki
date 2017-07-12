/*
 * Copyright (c) 2010, Swedish Institute of Computer Science.
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
 */

/**
 * \file
 *         RI-MAC implementation
 * \author
 *         Mat Wymore <mlwymore@iastate.edu>
 */

#define LOG_RETRIES 1
#define LOG_DELAY 1
#define DEBUG 0
#define LIMITED_DEBUG 1
#define TRACE_ON 1

#include "sys/clock.h"

#define LEDS 0
#define COMPOWER_ON 0

#if TRACE_ON
#include <stdio.h>
#define TRACE(format, ...) printf("TRACE " format, __VA_ARGS__)
#else
#define TRACE(...)
#endif

#if LOG_RETRIES
#define PRINT_RETRIES(format, ...) printf("RETRIES " format, __VA_ARGS__)
#else
#define PRINT_RETRIES(...)
#endif

#if LOG_DELAY
#define MAX_QUEUED_PACKETS 16
#include <stdio.h>
#include <lib/memb.h>
#include <lib/list.h>
struct delay_info {
  void * next;
  uint8_t seqno;
  clock_time_t timestamp;
};
MEMB(delay_info_memb, struct delay_info, MAX_QUEUED_PACKETS);
LIST(delay_info_list);
#define PRINT_DELAY(format, ...) printf("DELAY " format, __VA_ARGS__)
#endif

#include "contiki-conf.h"
#include "net/mac/mac.h"
#include "net/linkaddr.h"
#include "sys/rtimer.h"
#include "net/netstack.h"
#include "net/mac/rimac/rimac.h"
#include "lib/random.h"

#if LEDS
#include "dev/leds.h"
#endif

#include "sys/process.h"

#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINTDEBUG(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#define PRINTDEBUG(...)
#endif

#if LIMITED_DEBUG
#include <stdio.h>
#define LIM_PRINTF(...) printf(__VA_ARGS__)
#else
#define LIM_PRINTF(...)
#endif

#ifdef RIMAC_CONF_TBEACON
#define TBEACON RIMAC_CONF_TBEACON
#else
#define TBEACON RTIMER_SECOND/4
#endif

/* INTER_PACKET_DEADLINE is the maximum time a receiver waits for the
   next packet. Longer if DEBUG is enabled because the PRINTFs take time. */
#ifdef RIMAC_CONF_INTER_PACKET_DEADLINE
#define INTER_PACKET_DEADLINE               RIMAC_CONF_INTER_PACKET_DEADLINE
#else
#if DEBUG
#define INTER_PACKET_DEADLINE               RTIMER_SECOND / 65
#elif defined(TRACE_ON) || defined(LIMITED_DEBUG)
#define INTER_PACKET_DEADLINE               RTIMER_SECOND / 100
#else
#define INTER_PACKET_DEADLINE               RTIMER_SECOND / 200
#endif
#endif

#define RTIMER_WAKEUP_BUFFER_TIME RTIMER_SECOND / 250
#define CTIMER_WAKEUP_BUFFER_TIME CLOCK_SECOND / 32

#define RTIMER_MAX_TICKS 65536

#define MIN_BACKOFF_WINDOW 1

#if COMPOWER_ON
static struct compower_activity current_packet_compower;
#endif

/* rtimer for beaconing */
static struct rtimer beacon_timer;
/* rtimer for turning radio off after sending */
static struct rtimer off_timer;

static volatile uint8_t radio_is_on = 0;

static volatile uint8_t next_hop_is_awake = 0;
static volatile uint8_t packetbuf_locked = 0;
static volatile uint8_t sending_burst = 0;
//TODO: remove receiving_burst
static volatile uint8_t receiving_burst = 0;

static struct rdc_buf_list *curr_packet;
static struct rdc_buf_list *next_packet;

static uint8_t tx_counter = 0;
static clock_time_t backoff_window;

/* Used to allow process to call MAC callback */
static mac_callback_t saved_sent_callback;
static void * saved_ptr;

static uint8_t saved_packetbuf[PACKETBUF_SIZE];
static uint8_t saved_packetbuf_len;

/* -----------------------------------------------*/

PROCESS(wait_to_send_process, "Process that waits for a go-ahead signal and then sends packet burst");
static int off(int keep_radio_on);
static void send_beacon(struct rtimer * rt, void * ptr);
static void wake_up(struct rtimer * rt, void * ptr);
static void sleep(struct rtimer * rt, void * ptr);
static void clean_radio(void);


static void wake_up(struct rtimer * rt, void * ptr) {
  radio_is_on = 1;
  NETSTACK_RADIO.on();

  TRACE("%lu RADIO_ON 0\n", (unsigned long)clock_time());
  return;
}

static void set_periodic_rtimer(struct rtimer * rt, rtimer_clock_t interval, rtimer_callback_t func) {
  static volatile uint64_t a = 0;
  static volatile uint64_t b = 0;
  static volatile rtimer_clock_t now = 0;
  
  a = (uint64_t) RTIMER_TIME(rt);
  b = a + interval;
  now = RTIMER_NOW();

  while (1) {
    if (b >= RTIMER_MAX_TICKS) {
      b = b % RTIMER_MAX_TICKS;
      if (RTIMER_MAX_TICKS - 1 - now <= interval) {
        break;
      }
    }
    else if (now >= b) {
      b = b + interval;
    }
    else if (a >= now && a <= b) {
      if (a - now <= interval) {
        break;
      }
      b = b + interval;
    }
    else {
      break;
    }
  }

  now = RTIMER_NOW();
  if (b - now < INTER_PACKET_DEADLINE) {
    b = (b + interval) % RTIMER_MAX_TICKS;
  }

  rtimer_set(rt, (rtimer_clock_t)b, 1, func, NULL);
  //LIM_PRINTF("set_periodic_rtimer: Setting timer for %u (now is %u)\n", (uint16_t)b, (uint16_t) now);
}

//TODO: this doesn't seem to work, and it's inefficient anyway
static void clean_radio(void) {
  /* Throw away anything still stuck in the radio */
  if (NETSTACK_RADIO.pending_packet()) {
    LIM_PRINTF("clean_radio: removing pending packet\n");
    NETSTACK_RADIO.read(saved_packetbuf, PACKETBUF_SIZE);
  }
}

static void set_beacon_timer() {
  rtimer_clock_t interval;
  interval = TBEACON + random_rand() % TBEACON - TBEACON / 2;
  
  set_periodic_rtimer(&beacon_timer, interval, send_beacon);
}

static void sleep(struct rtimer * rt, void * ptr) {

  PRINTF("sleep: Turning off.\n");
  process_exit(&wait_to_send_process);
  packetbuf_locked = 0;
  clean_radio();
  NETSTACK_RADIO.off();
  TRACE("%lu RADIO_OFF 0\n", (unsigned long)clock_time());
#if COMPOWER_ON
  if (radio_is_on) {
    compower_accumulate(&compower_idle_activity);
  }
#endif
  radio_is_on = 0;
  sending_burst = 0;
  receiving_burst = 0;
  tx_counter = 0;

  rtimer_unset();
  /* Set timer for the next beacon */
  set_beacon_timer();
}

static void retry_packet(struct rtimer * rt, void * ptr) {
  uint8_t max_txs;
  static volatile uint8_t curr_txs;

  if (packetbuf_attr(PACKETBUF_ATTR_MAX_MAC_TRANSMISSIONS) == 0) {
    max_txs = 1;
  }
  else {
    max_txs = packetbuf_attr(PACKETBUF_ATTR_MAX_MAC_TRANSMISSIONS);
  }

  PRINT_RETRIES("%u\n", packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO));

  if (tx_counter >= max_txs) {
    curr_txs = tx_counter;
    LIM_PRINTF("retry_packet: max transmissions have failed, sleeping\n");
    mac_call_sent_callback(saved_sent_callback, saved_ptr, MAC_TX_NOACK, curr_txs);
    //TODO: sleep here? Yeah, if the upper layer is changed to accomodate.
    return;
  }

  process_post_synch(&wait_to_send_process, PROCESS_EVENT_CONTINUE, NULL);
  return;
}

static int send_packet() {
  PRINTF("send_packet: Sending packet.\n");
  if (!packetbuf_attr(PACKETBUF_ATTR_IS_CREATED_AND_SECURED)) {
    if (NETSTACK_FRAMER.create() < 0) {
      LIM_PRINTF("send_packet: framer failed.\n");
      return MAC_TX_ERR_FATAL;
    }
  }

  if (!NETSTACK_RADIO.channel_clear() || NETSTACK_RADIO.receiving_packet() ||
      NETSTACK_RADIO.pending_packet()) {
    LIM_PRINTF("send_packet: other activity detected.\n");
    return MAC_TX_COLLISION;
  }

  if (NETSTACK_RADIO.send(packetbuf_hdrptr(), packetbuf_totlen()) == RADIO_TX_OK) {
    return MAC_TX_OK;
  }

   return MAC_TX_ERR;
}

/* Send a beacon packet and start listening for a response. */
static void send_beacon(struct rtimer * rt, void * ptr) {
  int ret;
  rimac_beacon_payload_t beacon;
  //Per RI-MAC paper - always try with no backoff first
  beacon.backoff_window = 0;
  //Use null linkaddr to represent the "non-ack" beacon
  linkaddr_copy(&(beacon.dest_addr), &linkaddr_null);

  if (receiving_burst) {
    PRINTF("send_beacon: Skipping beacon - receiving a burst.\n");
    return;
  }

  if (sending_burst) {
    PRINTF("send_beacon: Skipping beacon - sending a burst.\n");
    return;
  }

  if (process_is_running(&wait_to_send_process)) {
    PRINTF("send_beacon: Skipping beacon - we're waiting to send.\n");
    return;
  }

  // If the radio is in use, don't send a beacon
  if (packetbuf_locked) {
    PRINTF("send_beacon: Skipping beacon - packetbuf locked.\n");
    return;
  }

  packetbuf_locked = 1;
  radio_is_on = 1;
  NETSTACK_RADIO.on();
  TRACE("%lu RADIO_ON 0\n", (unsigned long)clock_time());

  packetbuf_clear();
  packetbuf_set_addr(PACKETBUF_ADDR_SENDER, &linkaddr_node_addr);
  packetbuf_set_addr(PACKETBUF_ADDR_RECEIVER, &linkaddr_node_addr);
  packetbuf_set_attr(PACKETBUF_ATTR_PACKET_TYPE, PACKETBUF_ATTR_PACKET_TYPE_BEACON);
  packetbuf_set_datalen(sizeof(rimac_beacon_payload_t));
  memcpy(packetbuf_dataptr(), &beacon, sizeof(rimac_beacon_payload_t));

  ret = send_packet();
  TRACE("%lu BEACON 0\n", (unsigned long)clock_time());
  packetbuf_locked = 0;

  if (ret != MAC_TX_OK) {
    PRINTF("send_beacon: Beacon failed, skipping.\n");
    sleep(NULL, NULL);
  }

  PRINTF("send_beacon: Beacon sent.\n");
  #if LEDS
  leds_toggle(LEDS_GREEN);
  #endif

  //TODO: maybe should listen for longer than INTER_PACKET_DEADLINE? For retries.
  rtimer_set(&off_timer, RTIMER_NOW() + INTER_PACKET_DEADLINE, 1, sleep, NULL);

  return;
}

PROCESS_THREAD(wait_to_send_process, ev, data) {
  PROCESS_BEGIN();

  static int ret;

  do {
    //TODO: insert backoff time
    PROCESS_WAIT_EVENT_UNTIL(next_hop_is_awake && ev == PROCESS_EVENT_CONTINUE);

    rtimer_unset();

    tx_counter++;

    PRINTF("wait_to_send_process: Time to send!\n");
    #if LEDS
    leds_toggle(LEDS_RED);
    #endif

    if (packetbuf_locked) {
      PRINTF("wait_to_send_process: Packetbuf locked, waiting.\n");
      rtimer_set(&off_timer, RTIMER_NOW() + INTER_PACKET_DEADLINE, 1, retry_packet, NULL);

      continue;
    }

    packetbuf_locked = 1;
    next_packet = list_item_next(curr_packet);
    queuebuf_to_packetbuf(curr_packet->buf);

    ret = send_packet();

    //TODO: what if it's a collision, particularly from junk in the radio? How to get rid of junk?
    if (ret == MAC_TX_COLLISION) {
      clean_radio();
    }

#if TRACE_ON
    if (tx_counter <= 1) {
      TRACE("%lu DATA 0 %u\n", (unsigned long)clock_time(), packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO));
    }
    else {
      TRACE("%lu RETRY 0 %u\n", (unsigned long)clock_time(), packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO));
    }
#endif

    if (ret == MAC_TX_ERR_FATAL || ret == MAC_TX_ERR) {
      mac_call_sent_callback(saved_sent_callback, saved_ptr, MAC_TX_ERR_FATAL, 1);
      off(0);
      break;
    }

    packetbuf_locked = 0;

    rtimer_set(&off_timer, RTIMER_NOW() + INTER_PACKET_DEADLINE, 1, retry_packet, NULL);
      
  } while (1);

  sleep(NULL, NULL);
  PROCESS_END();
}

/*------------------RDC driver functions---------------------*/
static int on(void);
static void send_list(mac_callback_t sent_callback, void *ptr, struct rdc_buf_list *buf_list);

static void init(void) {
  PRINTF("init: Initializing RI-MAC\n");
#if LOG_DELAY
  memb_init(&delay_info_memb);
#endif
  /* Call sleep to clear all flags and whatnot */
  sleep(NULL, NULL);
  on();
}

/* Sends one packet */
static void send(mac_callback_t sent_callback, void *ptr) {
  static struct rdc_buf_list list;
  list.next = NULL;
  list.buf = queuebuf_new_from_packetbuf();
  list.ptr = NULL;
  send_list(sent_callback, ptr, &list);
  return;
}

/* Sends a list of packets. As part of this process, the sender waits for 
   a beacon from the next hop. */
static void send_list(mac_callback_t sent_callback, void *ptr, struct rdc_buf_list *buf_list) {
  struct rdc_buf_list *curr;
  struct rdc_buf_list *next;

#if LOG_DELAY
  struct delay_info *dinfo;
#endif

  PRINTF("send_list: Packets to send.\n");
  
  if (packetbuf_locked || sending_burst) {
    PRINTF("send_list: Packetbuf locked, can't secure packets.\n");
    mac_call_sent_callback(sent_callback, ptr, MAC_TX_COLLISION, 1);
    TRACE("%lu MAC_COLLISION 0\n", (unsigned long)clock_time());
    LIM_PRINTF("packetbuf lock: %d, sending_burst %d\n", packetbuf_locked, sending_burst);
    return;
  }

  packetbuf_locked = 1;
  TRACE("%lu DATA_ARRIVAL 0\n", (unsigned long)clock_time());
 
  curr = buf_list;
  do {
    next = list_item_next(curr);
    queuebuf_to_packetbuf(curr->buf);
    if(!packetbuf_attr(PACKETBUF_ATTR_IS_CREATED_AND_SECURED) || !packetbuf_attr(PACKETBUF_ATTR_PENDING)) {
      if(next != NULL) {
        packetbuf_set_attr(PACKETBUF_ATTR_PENDING, 1);
      }
      packetbuf_set_attr(PACKETBUF_ATTR_MAC_ACK, 1);
      packetbuf_set_attr(PACKETBUF_ATTR_PACKET_TYPE, PACKETBUF_ATTR_PACKET_TYPE_DATA);
      if(NETSTACK_FRAMER.create() < 0) {
        PRINTF("send_list: framer failed\n");
        mac_call_sent_callback(sent_callback, ptr, MAC_TX_ERR_FATAL, 1);
        return;
      }
      
      packetbuf_set_attr(PACKETBUF_ATTR_IS_CREATED_AND_SECURED, 1);
      queuebuf_update_from_packetbuf(curr->buf);
    }

#if LOG_DELAY
    dinfo = list_head(delay_info_list);
    do {
      if (dinfo == NULL) {
        break;
      }
      if (dinfo->seqno == packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO)) {
        break;
      }
      dinfo = list_item_next(dinfo);
    } while (dinfo != NULL);

    if (dinfo == NULL) {
      dinfo = memb_alloc(&delay_info_memb);
      dinfo->seqno = packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO);
      dinfo->timestamp = clock_time();
      list_add(delay_info_list, dinfo);
    }
#endif
    curr = next;
  } while(next != NULL);

  packetbuf_locked = 0;

  saved_sent_callback = sent_callback;
  saved_ptr = ptr;

  wake_up(NULL, NULL);
  curr_packet = buf_list;
  process_start(&wait_to_send_process, NULL);
  
  return;
}

static void input(void) {
  int ret;
  uint8_t dataSeqno;
  //uint16_t pending;
  static uint32_t temp_time;
  linkaddr_t sender_addr;
  linkaddr_t next_hop_addr;
  rimac_beacon_payload_t beacon;
  //packetbuf_attr_t curr_rss;

  if (!radio_is_on) {
    LIM_PRINTF("input: Skipping processing of packet because attempt is after radio turned off.\n");
    return;
  }

#if LOG_DELAY
  struct delay_info * dinfo;
#endif

  clock_time_t now;

  now = clock_time();

  /* Check if the packetbuf is locked, but I think we need to go ahead anyway */
  if (packetbuf_locked) {
    PRINTF("input: Receive might be interrupting another task.\n");
  }
  packetbuf_locked = 1;
  //curr_rss = packetbuf_attr(PACKETBUF_ATTR_RSSI);

  if (NETSTACK_FRAMER.parse() < 0) {
    LIM_PRINTF("input: Framer failed to parse packet.\n");
    packetbuf_locked = 0;
    return;
  }

  switch (packetbuf_attr(PACKETBUF_ATTR_PACKET_TYPE)) {
    case PACKETBUF_ATTR_PACKET_TYPE_BEACON:
      if (process_is_running(&wait_to_send_process)) {
        rtimer_unset();
      }

      memcpy(&beacon, packetbuf_dataptr(), packetbuf_datalen());
      linkaddr_copy(&sender_addr, packetbuf_addr(PACKETBUF_ADDR_SENDER));
      
      //LIM_PRINTF("input: dest_addr %d.%d, me %d.%d, backoff_window %d\n", beacon.dest_addr.u8[0], beacon.dest_addr.u8[1], linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1], (int)beacon.backoff_window);
      //If beacon serves as an ACK to me...
      if (linkaddr_cmp(&(beacon.dest_addr), &linkaddr_node_addr)) {
        PRINTF("input: It's an ack! saved_ptr is %d\n", (int)saved_ptr);

        dataSeqno = (uint8_t)packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO);
        TRACE("%lu ACK_RECEIVED %d %u\n", (unsigned long)now, packetbuf_attr(PACKETBUF_ATTR_RSSI), dataSeqno);

        queuebuf_to_packetbuf(curr_packet->buf);

        if (dataSeqno != packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO)) {
          LIM_PRINTF("input: Wrong seqno in ack, discarding. %d, %d\n", dataSeqno, packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO));
          /*packetbuf_locked = 0;
          process_post_synch(&wait_to_send_process, PROCESS_EVENT_CONTINUE, NULL);
          return;*/
        }
        else {
          mac_call_sent_callback(saved_sent_callback, saved_ptr, MAC_TX_OK, 1);
      
          //packetbuf_locked = 0;
          tx_counter = 0;

          if (!packetbuf_attr(PACKETBUF_ATTR_PENDING)) {
            //LIM_PRINTF("final ack received\n");
            process_exit(&wait_to_send_process);
            sending_burst = 0;
        
            sleep(NULL, NULL);
            return;
          }
          else {
            curr_packet = next_packet;
            //process_post_synch(&wait_to_send_process, PROCESS_EVENT_CONTINUE, NULL);
          }
#if LOG_DELAY
          dinfo = list_head(delay_info_list);
          do {
            if (dinfo == NULL) {
              break;
            }
            if (dinfo->seqno == dataSeqno) {
              PRINT_DELAY("%u %lu\n", dataSeqno, (unsigned long)(now - dinfo->timestamp));
              list_remove(delay_info_list, dinfo);
              memb_free(&delay_info_memb, dinfo);
              break;
            }
            dinfo = list_item_next(dinfo);
          } while (dinfo != NULL);
#endif
        }
      }

      linkaddr_copy(&next_hop_addr, queuebuf_addr(curr_packet->buf, PACKETBUF_ADDR_RECEIVER));

      if (process_is_running(&wait_to_send_process) && linkaddr_cmp(&sender_addr, &next_hop_addr)) {
        next_hop_is_awake = 1;
      }
      else {
        return;
      }

      packetbuf_locked = 0;
      TRACE("%lu BEACON_RECEIVED\n", (unsigned long)now);

      if (next_hop_is_awake) {
        sending_burst = 1;
        process_post_synch(&wait_to_send_process, PROCESS_EVENT_CONTINUE, NULL);
      }

      break;

    case PACKETBUF_ATTR_PACKET_TYPE_DATA:
      linkaddr_copy(&next_hop_addr, packetbuf_addr(PACKETBUF_ADDR_RECEIVER));
      if (linkaddr_cmp(&next_hop_addr, &linkaddr_node_addr)) {
        rtimer_unset();
      }
      else {
        break;
      }

      dataSeqno = (uint8_t)packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO);
      //pending = packetbuf_attr(PACKETBUF_ATTR_PENDING);
      PRINTF("input: It's data! seqno %d\n", dataSeqno);
      TRACE("%lu DATA_RECEIVED %d %u\n", (unsigned long)now, packetbuf_attr(PACKETBUF_ATTR_RSSI), dataSeqno);
      
      if (saved_packetbuf_len != 0) {
        LIM_PRINTF("input: Overwriting saved packetbuf to send ack.\n");
      }
      
      packetbuf_copyto(saved_packetbuf);
      saved_packetbuf_len = packetbuf_totlen();

      /* RI-MAC specifies that the ACK is the same as a beacon, just with a destination
         address sent to the recipient of the ACK. The packet type needs to be a beacon
         so that non-recipients can use it as a signal to contend. */
      beacon.backoff_window = backoff_window;
      linkaddr_copy(&(beacon.dest_addr), packetbuf_addr(PACKETBUF_ADDR_SENDER));

      packetbuf_clear();
      packetbuf_set_addr(PACKETBUF_ADDR_SENDER, &linkaddr_node_addr);
      packetbuf_set_addr(PACKETBUF_ADDR_RECEIVER, &linkaddr_node_addr);
      packetbuf_set_attr(PACKETBUF_ATTR_PACKET_TYPE, PACKETBUF_ATTR_PACKET_TYPE_BEACON);
      packetbuf_set_attr(PACKETBUF_ATTR_MAC_SEQNO, dataSeqno);
      packetbuf_set_datalen(sizeof(rimac_beacon_payload_t));
      memcpy(packetbuf_dataptr(), &beacon, sizeof(rimac_beacon_payload_t));

      if (NETSTACK_FRAMER.create() < 0) {
        LIM_PRINTF("input: framer failed for ACK.\n");
        return;
      }

      //The RI-MAC paper specifies that the ACK is sent regardless of medium state
      ret = NETSTACK_RADIO.send(packetbuf_hdrptr(), packetbuf_totlen());
      TRACE("%lu ACK 0 %u\n", (unsigned long)clock_time(), dataSeqno);

      if (ret == RADIO_TX_OK) {
        PRINTF("input: Ack sent.\n");
#if LEDS
        leds_toggle(LEDS_RED);
#endif
      }
      else {
        PRINTF("input: Ack send failed.\n");
      }

      packetbuf_copyfrom(saved_packetbuf, saved_packetbuf_len);

      NETSTACK_MAC.input();

      saved_packetbuf_len = 0;

      temp_time = RTIMER_NOW() + INTER_PACKET_DEADLINE;
      //LIM_PRINTF("temp_time %lu, %u, %d, %u\n", temp_time, RTIMER_NOW(), INTER_PACKET_DEADLINE, RTIMER_SECOND);
      temp_time %= RTIMER_MAX_TICKS;
      rtimer_set(&off_timer, (rtimer_clock_t)temp_time, 1, sleep, NULL);

      packetbuf_locked = 0;
      break;
/*    case PACKETBUF_ATTR_PACKET_TYPE_ACK:
      //TODO: add support for hardware acks
      linkaddr_copy(&next_hop_addr, packetbuf_addr(PACKETBUF_ADDR_RECEIVER));
      if (linkaddr_cmp(&next_hop_addr, &linkaddr_node_addr)) {
        rtimer_unset();
      }
      else {
        return;
      }

      PRINTF("input: It's an ack! saved_ptr is %d\n", (int)saved_ptr);

      dataSeqno = (uint8_t)packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO);
      TRACE("%lu ACK_RECEIVED %d %u\n", (unsigned long)now, packetbuf_attr(PACKETBUF_ATTR_RSSI), dataSeqno);

      queuebuf_to_packetbuf(curr_packet->buf);

      if (dataSeqno != packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO)) {
        LIM_PRINTF("input: Wrong seqno in ack, discarding. %d, %d\n", dataSeqno, packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO));
        packetbuf_locked = 0;
        process_post_synch(&wait_to_send_process, PROCESS_EVENT_CONTINUE, NULL);
        return;
      }

      mac_call_sent_callback(saved_sent_callback, saved_ptr, MAC_TX_OK, 1);
      
      packetbuf_locked = 0;
      tx_counter = 0;

      if (!packetbuf_attr(PACKETBUF_ATTR_PENDING)) {
        LIM_PRINTF("final ack received\n");
        process_exit(&wait_to_send_process);
        sending_burst = 0;
        
        sleep(NULL, NULL);
      }
      else {
        curr_packet = next_packet;
        process_post_synch(&wait_to_send_process, PROCESS_EVENT_CONTINUE, NULL);
      }
#if LOG_DELAY
      dinfo = list_head(delay_info_list);
      do {
        if (dinfo == NULL) {
          break;
        }
        if (dinfo->seqno == dataSeqno) {
          PRINT_DELAY("%u %lu\n", dataSeqno, (unsigned long)(now - dinfo->timestamp));
          list_remove(delay_info_list, dinfo);
          memb_free(&delay_info_memb, dinfo);
          break;
        }
        dinfo = list_item_next(dinfo);
      } while (dinfo != NULL);
#endif
      break;
*/
  }
  return;
}

/* Turn on regular beaconing (i.e. start beacon timer). */
static int on(void) {
  PRINTF("on: starting beaconing.\n");
  backoff_window = 0;
  set_beacon_timer();
  return 1;
}

/* Stop regular beaconing (i.e. stop beacon timer). */
static int off(int keep_radio_on) {
  rtimer_unset();
  if (!keep_radio_on) {
    sleep(NULL, NULL);
  }
  return 1;
}

/* Return Tbeacon */
static unsigned short channel_check_interval(void) {
  return TBEACON;
}

const struct rdc_driver rimac_driver = {
  "ri-mac",
  init,
  send,
  send_list,
  input,
  on,
  off,
  channel_check_interval,
};
