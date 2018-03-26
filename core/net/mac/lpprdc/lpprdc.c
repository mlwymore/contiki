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
 *         Implementation of a low-power probing radio duty cycling protocol,
 *         based on contikimac.c.
 * \author
 *         Mat Wymore <mlwymore@gmail.com>
 */

#include "contiki-conf.h"
#include "dev/leds.h"
#include "dev/radio.h"
#include "dev/watchdog.h"
#include "lib/random.h"
#include "net/mac/mac-sequence.h"
#include "net/mac/lpprdc/lpprdc.h"
#include "net/mac/contikimac/contikimac-framer.h"
#include "net/netstack.h"
#include "net/rime/rime.h"
#include "sys/compower.h"
#include "sys/pt.h"
#include "sys/rtimer.h"


#include <string.h>

/* TX/RX cycles are synchronized with neighbor wake periods */
#ifdef CONTIKIMAC_CONF_WITH_PHASE_OPTIMIZATION
#define WITH_PHASE_OPTIMIZATION      CONTIKIMAC_CONF_WITH_PHASE_OPTIMIZATION
#else /* CONTIKIMAC_CONF_WITH_PHASE_OPTIMIZATION */
#define WITH_PHASE_OPTIMIZATION      0
#endif /* CONTIKIMAC_CONF_WITH_PHASE_OPTIMIZATION */
/* More aggressive radio sleeping when channel is busy with other traffic */
#ifndef WITH_FAST_SLEEP
#define WITH_FAST_SLEEP              1
#endif
/* Radio does CSMA and autobackoff */
#ifndef RDC_CONF_HARDWARE_CSMA
#define RDC_CONF_HARDWARE_CSMA       0
#endif
/* Radio returns TX_OK/TX_NOACK after autoack wait */
#ifndef RDC_CONF_HARDWARE_ACK
#define RDC_CONF_HARDWARE_ACK        0
#endif
/* MCU can sleep during radio off */
#ifndef RDC_CONF_MCU_SLEEP
#define RDC_CONF_MCU_SLEEP           0
#endif

/* INTER_PACKET_DEADLINE is the maximum time a receiver waits for the
   next packet of a burst when FRAME_PENDING is set. */
#ifdef CONTIKIMAC_CONF_INTER_PACKET_DEADLINE
#define INTER_PACKET_DEADLINE               CONTIKIMAC_CONF_INTER_PACKET_DEADLINE
#else
#define INTER_PACKET_DEADLINE               CLOCK_SECOND / 32
#endif

/* ContikiMAC performs periodic channel checks. Each channel check
   consists of two or more CCA checks. CCA_COUNT_MAX is the number of
   CCAs to be done for each periodic channel check. The default is
   two.*/
//#ifdef CONTIKIMAC_CONF_CCA_COUNT_MAX
//#define CCA_COUNT_MAX                      (CONTIKIMAC_CONF_CCA_COUNT_MAX)
//#else
//#define CCA_COUNT_MAX                      2
//#endif

/* Before starting a transmission, Contikimac checks the availability
   of the channel with CCA_COUNT_MAX_TX consecutive CCAs */
#ifdef CONTIKIMAC_CONF_CCA_COUNT_MAX_TX
#define CCA_COUNT_MAX_TX                   (CONTIKIMAC_CONF_CCA_COUNT_MAX_TX)
#else
#define CCA_COUNT_MAX_TX                   6
#endif

/* CCA_CHECK_TIME is the time it takes to perform a CCA check. */
/* Note this may be zero. AVRs have 7612 ticks/sec, but block until cca is done */
#ifdef CONTIKIMAC_CONF_CCA_CHECK_TIME
#define CCA_CHECK_TIME                     (CONTIKIMAC_CONF_CCA_CHECK_TIME)
#else
#define CCA_CHECK_TIME                     RTIMER_ARCH_SECOND / 8192
#endif

/* CCA_SLEEP_TIME is the time between two successive CCA checks. */
/* Add 1 when rtimer ticks are coarse */
#ifdef CONTIKIMAC_CONF_CCA_SLEEP_TIME
#define CCA_SLEEP_TIME CONTIKIMAC_CONF_CCA_SLEEP_TIME
#else
#if RTIMER_ARCH_SECOND > 8000
#define CCA_SLEEP_TIME                     RTIMER_ARCH_SECOND / 2000 + CCA_CHECK_TIME
//#define CCA_SLEEP_TIME                     RTIMER_ARCH_SECOND / 3225 - 2*CCA_CHECK_TIME
//#define CCA_SLEEP_TIME                     CCA_CHECK_TIME
#else
#define CCA_SLEEP_TIME                     (RTIMER_ARCH_SECOND / 2000) + 1 + CCA_CHECK_TIME
//#define CCA_SLEEP_TIME                     RTIMER_ARCH_SECOND / 3225 + 1 - 2*CCA_CHECK_TIME
//#define CCA_SLEEP_TIME                     CCA_CHECK_TIME
#endif /* RTIMER_ARCH_SECOND > 8000 */
#endif /* CONTIKIMAC_CONF_CCA_SLEEP_TIME */

/* CHECK_TIME is the total time it takes to perform CCA_COUNT_MAX
   CCAs. */
//#define CHECK_TIME                         (CCA_COUNT_MAX * (CCA_CHECK_TIME + CCA_SLEEP_TIME))
#define CHECK_TIME                         (1 * (CCA_CHECK_TIME + CCA_SLEEP_TIME))

/* CHECK_TIME_TX is the total time it takes to perform CCA_COUNT_MAX_TX
   CCAs. */
#define CHECK_TIME_TX                      (CCA_COUNT_MAX_TX * (CCA_CHECK_TIME + CCA_SLEEP_TIME))

/* LISTEN_TIME_AFTER_PACKET_DETECTED is the time that we keep checking
   for activity after a potential packet has been detected by a CCA
   check. */
#ifdef CONTIKIMAC_CONF_LISTEN_TIME_AFTER_PACKET_DETECTED
#define LISTEN_TIME_AFTER_PACKET_DETECTED  CONTIKIMAC_CONF_LISTEN_TIME_AFTER_PACKET_DETECTED
#else
#define LISTEN_TIME_AFTER_PACKET_DETECTED  RTIMER_ARCH_SECOND / 80
#endif

/* MAX_SILENCE_PERIODS is the maximum amount of periods (a period is
   CCA_CHECK_TIME + CCA_SLEEP_TIME) that we allow to be silent before
   we turn of the radio. */
#ifdef CONTIKIMAC_CONF_MAX_SILENCE_PERIODS
#define MAX_SILENCE_PERIODS                CONTIKIMAC_CONF_MAX_SILENCE_PERIODS
#else
#define MAX_SILENCE_PERIODS                5
#endif

/* MAX_NONACTIVITY_PERIODS is the maximum number of periods we allow
   the radio to be turned on without any packet being received, when
   WITH_FAST_SLEEP is enabled. */
#ifdef CONTIKIMAC_CONF_MAX_NONACTIVITY_PERIODS
#define MAX_NONACTIVITY_PERIODS            CONTIKIMAC_CONF_MAX_NONACTIVITY_PERIODS
#else
#define MAX_NONACTIVITY_PERIODS            10
#endif

#define NETSTACK_RDC_CHANNEL_CHECK_RANDOMNESS_FRACTION 10

/* BROADCAST_POLL_RATE is the rate (Hz) at which the broadcasting process checks for
   a timeout */
#define BROADCAST_POLL_RATE 100

/* GUARD_TIME is the time before the expected phase of a neighbor that
   a transmitted should begin transmitting packets. */
#ifdef CONTIKIMAC_CONF_GUARD_TIME
#define GUARD_TIME                         CONTIKIMAC_CONF_GUARD_TIME
#else
#define GUARD_TIME                         10 * CHECK_TIME + CHECK_TIME_TX
#endif

/* INTER_PACKET_INTERVAL is the interval between two successive packet transmissions */
#ifdef CONTIKIMAC_CONF_INTER_PACKET_INTERVAL
#define INTER_PACKET_INTERVAL              CONTIKIMAC_CONF_INTER_PACKET_INTERVAL
#else
#define INTER_PACKET_INTERVAL              RTIMER_ARCH_SECOND / 2500
#endif

#define MAX_BACKOFF                        RTIMER_ARCH_SECOND / 100
#define MIN_BACKOFF                        MAX_BACKOFF / 16

#define MAX_PROBE_ATTEMPTS                 5

#define MAX_QUEUED_PACKETS                 QUEUEBUF_NUM

//#define AFTER_PROBE_SENT_WAIT_TIME         RTIMER_ARCH_SECOND / 1200
#define AFTER_PROBE_SENT_WAIT_TIME         RTIMER_ARCH_SECOND / 600
//#define AFTER_PROBE_SENT_WAIT_TIME         RTIMER_ARCH_SECOND / 600 + MAX_BACKOFF

/* AFTER_ACK_DETECTED_WAIT_TIME is the time to wait after a potential
   ACK packet has been detected until we can read it out from the
   radio. */
#ifdef CONTIKIMAC_CONF_AFTER_ACK_DETECTED_WAIT_TIME
#define AFTER_ACK_DETECTED_WAIT_TIME      CONTIKIMAC_CONF_AFTER_ACK_DETECTED_WAIT_TIME
#else
#define AFTER_ACK_DETECTED_WAIT_TIME      RTIMER_ARCH_SECOND / 1500
#endif

#define PROBE_RECEIVE_DURATION            RTIMER_ARCH_SECOND / 1000
#define PACKET_RECEIVE_DURATION           RTIMER_ARCH_SECOND / 250

#define ACK_LEN                           3

/* MAX_PHASE_STROBE_TIME is the time that we transmit repeated packets
   to a neighbor for which we have a phase lock. */
#ifdef CONTIKIMAC_CONF_MAX_PHASE_STROBE_TIME
#define MAX_PHASE_STROBE_TIME              CONTIKIMAC_CONF_MAX_PHASE_STROBE_TIME
#else
#define MAX_PHASE_STROBE_TIME              RTIMER_ARCH_SECOND / 60
#endif

#define BACKOFF_SLOT_LENGTH (RTIMER_ARCH_SECOND / 100)

#include <stdio.h>
static struct rtimer rt;
static struct ctimer probe_timer;
static struct pt pt;
static struct pt send_pt;

static volatile uint16_t probe_len = 0;
static linkaddr_t current_receiver_addr;
static volatile uint8_t current_seqno = 0;

static struct rdc_buf_list *curr_packet_list;
static mac_callback_t curr_callback;
static void *curr_ptr;

static volatile uint8_t contikimac_is_on = 0;
static volatile uint8_t contikimac_keep_radio_on = 0;

static volatile unsigned char we_are_sending = 0;
static volatile unsigned char we_are_listening = 0;
static volatile unsigned char we_are_broadcasting = 0;
static volatile unsigned char radio_is_on = 0;
static volatile unsigned char we_are_probing = 0;
static volatile unsigned char is_receiver_awake = 0;
static volatile unsigned char expecting_ack = 0;
static volatile unsigned char waiting_for_response = 0;
static volatile unsigned char activity_seen = 0;
static volatile unsigned char we_are_processing_input = 0;
static volatile unsigned char probe_timer_is_running = 0;
static volatile unsigned char continue_probing = 0;

static volatile uint16_t probe_rate = 0;
static volatile uint16_t probe_interval = 0;
static volatile uint16_t max_probe_interval = 0;
static volatile uint16_t ctimer_max_probe_interval = 0;
static volatile uint16_t listen_for_probe_timeout = 0;
static volatile clock_time_t backoff_probe_timeout = 0;
static volatile rtimer_clock_t current_backoff_window = 0;
static volatile rtimer_clock_t last_probe_time;

static int packets_to_ok[MAX_QUEUED_PACKETS];
static int packets_to_ok_index = 0;

PROCESS(input_process, "input process");
//AUTOSTART_PROCESSES(&input_process);

static struct queuebuf input_queuebuf;
static volatile uint8_t input_queuebuf_locked = 0;

struct probe_packet {
  rtimer_clock_t backoff_window;
  uint8_t probing;
};

#define DEBUG 1
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINTDEBUG(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#define PRINTDEBUG(...)
#endif

#if CONTIKIMAC_CONF_COMPOWER
static struct compower_activity current_packet;
#endif /* CONTIKIMAC_CONF_COMPOWER */

#if CONTIKIMAC_CONF_BROADCAST_RATE_LIMIT
static struct timer broadcast_rate_timer;
static int broadcast_rate_counter;
#endif /* CONTIKIMAC_CONF_BROADCAST_RATE_LIMIT */

static unsigned short duty_cycle(void);
/*---------------------------------------------------------------------------*/
static void
on(void)
{
  if(contikimac_is_on && radio_is_on == 0) {
    radio_is_on = 1;
    NETSTACK_RADIO.on();
  }
}
/*---------------------------------------------------------------------------*/
static void
off(void)
{
  if(contikimac_is_on && radio_is_on != 0 &&
     contikimac_keep_radio_on == 0) {
    radio_is_on = 0;
    NETSTACK_RADIO.off();
  }
}
/*---------------------------------------------------------------------------*/
static void powercycle_wrapper(struct rtimer *t, void *ptr);
static char powercycle(void *ptr);
/*---------------------------------------------------------------------------*/
static void
powercycle_turn_radio_off(void)
{
#if CONTIKIMAC_CONF_COMPOWER
  uint8_t was_on = radio_is_on;
#endif /* CONTIKIMAC_CONF_COMPOWER */
  if(!contikimac_is_on) {
    return;
  }
  if(we_are_listening == 0 && !NETSTACK_RADIO.receiving_packet()) {
    off();
#if CONTIKIMAC_CONF_COMPOWER
    if(was_on && !radio_is_on) {
      compower_accumulate(&compower_idle_activity);
    }
#endif /* CONTIKIMAC_CONF_COMPOWER */
  } else {
    PRINTF("lpprdc: not turning radio off\n");
  }
}
/*---------------------------------------------------------------------------*/
static void
powercycle_turn_radio_on(void)
{
  if(we_are_sending == 0 && we_are_listening == 0) {
    on();
  }
}
/*---------------------------------------------------------------------------*/
static void
powercycle_wrapper(struct rtimer *t, void *ptr)
{
  probe_timer_is_running = 0;
  if(waiting_for_response && !we_are_processing_input) {
    powercycle(ptr);
  } else {

  }
}
/*---------------------------------------------------------------------------*/
static void
powercycle_bare_wrapper(struct rtimer *t, void *ptr)
{
  powercycle(ptr);
}
/*---------------------------------------------------------------------------*/
static void
powercycle_ctimer_wrapper(void *ptr)
{
  powercycle(ptr);
}
/*---------------------------------------------------------------------------*/
/*
 * Send probes and listen for a response.
*/
static char
powercycle(void *ptr)
{

  PT_BEGIN(&pt);

  static struct probe_packet probe;
  static uint8_t probe_attempts = 0;
  static rtimer_clock_t wait_time = 0;
  static rtimer_clock_t time_clear = 0;
  static struct queuebuf *rec_buf;
  static rtimer_clock_t wt;
  static int32_t rand = 0;
  static uint32_t probe_interval_rand = 0;

  while(1) {

    if(we_are_listening == 0 && we_are_sending == 0 &&
       !(NETSTACK_RADIO.receiving_packet() || NETSTACK_RADIO.pending_packet())) {
    if(!waiting_for_response) {
      we_are_probing = 1;
      if(probe_attempts > 0) {
        wait_time = ((1 << probe_attempts) - 1) * BACKOFF_SLOT_LENGTH;
      } else {
        wait_time = 0;
      }
      probe.backoff_window = wait_time;
      probe.probing = 1;
      packetbuf_copyfrom(&probe, sizeof(struct probe_packet));
      packetbuf_set_attr(PACKETBUF_ATTR_MAC_ACK, 0);
      packetbuf_set_addr(PACKETBUF_ADDR_SENDER, &linkaddr_node_addr);
      packetbuf_set_addr(PACKETBUF_ADDR_RECEIVER, &linkaddr_null);
      if(NETSTACK_FRAMER.create() < 0) {
        PRINTDEBUG("lpprdc: Failed to create probe packet.\n");
        powercycle_turn_radio_off();
        we_are_probing = 0;
      } else if(NETSTACK_RADIO.prepare(packetbuf_hdrptr(), packetbuf_totlen())) {
        PRINTF("lpprdc: radio locked while probing\n");
        powercycle_turn_radio_off();
        we_are_probing = 0;
      } else { 
        powercycle_turn_radio_on();
        if(NETSTACK_RADIO.transmit(packetbuf_totlen()) != RADIO_TX_OK) {
         // PRINTF("lpprdc: radio layer collision while probing\n");
          powercycle_turn_radio_off();
          we_are_probing = 0;
        } else {
          last_probe_time = RTIMER_NOW();
          probe_attempts++;
          current_backoff_window = wait_time;
        }
      }
    } else {
      PRINTF("lpprdc: skipping probe, waiting for response\n");
    }

      /* Listen for response and time out */
      if(we_are_probing) {
        activity_seen = 0;
        waiting_for_response = 1;
        time_clear = 0;
        wait_time += AFTER_PROBE_SENT_WAIT_TIME;
        wt = RTIMER_NOW();
        /* We wait for either a response, or for a timeout */
        while(waiting_for_response && RTIMER_CLOCK_LT(RTIMER_NOW(), wt + wait_time)) {
          /* The first time we CCA, if there's activity, it's possibly
             a holdover from TXing, so we wait for the CCA to go low first. */
          if(NETSTACK_RADIO.channel_clear() && !NETSTACK_RADIO.receiving_packet()) {
            if(time_clear == 0) {
              time_clear = RTIMER_NOW();
            }
            if(activity_seen){
              if(RTIMER_CLOCK_DIFF(RTIMER_NOW(), time_clear) > 
                 PROBE_RECEIVE_DURATION + AFTER_ACK_DETECTED_WAIT_TIME && 
                 !NETSTACK_RADIO.pending_packet()) {
                break;
              }
            }
          /* Only declare activity if the channel was clear at some point */
          } else if(time_clear) {
            activity_seen = 1;
            time_clear = 0;
            wait_time += PACKET_RECEIVE_DURATION + PROBE_RECEIVE_DURATION + AFTER_ACK_DETECTED_WAIT_TIME;
          }
          rtimer_set(&rt, RTIMER_NOW() + CCA_SLEEP_TIME, 1,
                     powercycle_wrapper, NULL);
          probe_timer_is_running = 1;
          PT_YIELD(&pt);
        }

        if(!waiting_for_response) {
          /* A response was received. We reset probe_attempts because
             of the successful contention resolution. */
          probe_attempts = 0;
          wait_time = 0;
          if(!continue_probing) {
            powercycle_turn_radio_off();
          } else {
            waiting_for_response = 1;
            continue;
          }
        } else if(activity_seen) {
          leds_blink();
          if(probe_attempts < MAX_PROBE_ATTEMPTS) {
        
            waiting_for_response = 0;
            rtimer_clock_t start_time = RTIMER_NOW();
            while(RTIMER_CLOCK_LT(RTIMER_NOW(), start_time + PROBE_RECEIVE_DURATION + AFTER_ACK_DETECTED_WAIT_TIME)) {
              if(!NETSTACK_RADIO.channel_clear()) {
                start_time = RTIMER_NOW();
              }
            }
            continue;
          } else {
            powercycle_turn_radio_off();
          }
        } else {
          /* We timed out */
          powercycle_turn_radio_off();
        }
      }
    } else if(we_are_probing) {
      /* If we skipped because of an incoming packet, we need to give input a 
         chance to handle it. */
      if(NETSTACK_RADIO.pending_packet() || NETSTACK_RADIO.receiving_packet()) {
        rtimer_set(&rt, RTIMER_NOW() + CCA_SLEEP_TIME, 1,
                     powercycle_bare_wrapper, NULL);
        PT_YIELD(&pt);
        /* If input didn't get called, just give up. This can happen if another
           packet was sent at the same time we tried to probe. */
        if((NETSTACK_RADIO.pending_packet() || NETSTACK_RADIO.receiving_packet()) && !we_are_processing_input) {
          off();
        }
      } else {
        rtimer_clock_t start_time = RTIMER_NOW();
        while(RTIMER_CLOCK_LT(RTIMER_NOW(), start_time + PROBE_RECEIVE_DURATION + AFTER_ACK_DETECTED_WAIT_TIME)) {
           if(!NETSTACK_RADIO.channel_clear()) {
             start_time = RTIMER_NOW();
           }
        }
        continue;
      }
    } else {
      PRINTF("lpprdc: skipping probe\n");
      PRINTF("listening %d, sending %d\n", we_are_listening, we_are_sending);
    }
    waiting_for_response = 0;
    we_are_probing = 0;
    rand = (int32_t)random_rand() - (RANDOM_RAND_MAX / 2);
    rand = rand / NETSTACK_RDC_CHANNEL_CHECK_RANDOMNESS_FRACTION * probe_interval / (RANDOM_RAND_MAX / 2);
    probe_interval_rand = (uint32_t)probe_interval + rand;
    ctimer_set(&probe_timer, probe_interval_rand, powercycle_ctimer_wrapper, NULL);
    probe_attempts = 0;
    PT_YIELD(&pt);
  }

  PT_END(&pt);
}
/*---------------------------------------------------------------------------*/
static int
broadcast_rate_drop(void)
{
#if CONTIKIMAC_CONF_BROADCAST_RATE_LIMIT
  if(!timer_expired(&broadcast_rate_timer)) {
    broadcast_rate_counter++;
    if(broadcast_rate_counter < CONTIKIMAC_CONF_BROADCAST_RATE_LIMIT) {
      return 0;
    } else {
      return 1;
    }
  } else {
    timer_set(&broadcast_rate_timer, CLOCK_SECOND);
    broadcast_rate_counter = 0;
    return 0;
  }
#else /* CONTIKIMAC_CONF_BROADCAST_RATE_LIMIT */
  return 0;
#endif /* CONTIKIMAC_CONF_BROADCAST_RATE_LIMIT */
}
/*---------------------------------------------------------------------------*/
static char send_packet();
/* Timer callback triggered on timeout when listening for probe */
static void
listening_off(void *ptr)
{
  send_packet();
}
/*---------------------------------------------------------------------------*/
/* Timer callback triggered on timeout when waiting for backoff */
static void
backoff_timeout(struct rtimer *rt, void *ptr)
{
  if(!is_receiver_awake) {
    /* This means no more probes were heard - it's time to send. */
    send_packet();
  }
}
/*---------------------------------------------------------------------------*/
static char
send_packet()
{
  PT_BEGIN(&send_pt);

  static uint8_t got_packet_ack = 0;
  static uint8_t is_broadcast = 0;
  static uint8_t collisions = 0;
  static uint8_t retry = 0;
  static int transmit_len = 0;
  static int ret = 0;
  static uint8_t contikimac_was_on = 1;
  static struct rdc_buf_list *next;
  static struct ctimer ct;
  struct addr_list_item *item;
  int list_contains_addr = 0;
  static linkaddr_t receiver_addr;
  static uint8_t packet_pushed = 0;
  static clock_time_t broadcast_start_time = 0;

  /* Exit if RDC and radio were explicitly turned off */
  if(!contikimac_is_on && !contikimac_keep_radio_on) {
    PRINTF("lpprdc: radio is turned off\n");
    mac_call_sent_callback(curr_callback, curr_ptr, MAC_TX_ERR_FATAL, 1);
    return 0;
  }

  if(we_are_probing) {
    PRINTF("lpprdc: collision receiving %d, pending %d\n",
           NETSTACK_RADIO.receiving_packet(), NETSTACK_RADIO.pending_packet());
    mac_call_sent_callback(curr_callback, curr_ptr, MAC_TX_COLLISION, 1);
    return 0;
  }

  we_are_listening = 1;
  retry = 0;
  current_backoff_window = 0;
  linkaddr_copy(&current_receiver_addr, &linkaddr_null);

  do {

    collisions = 0;
    got_packet_ack = 0;

    if(!retry) {
    /* Send the packet once if channel clear - similar to RI-MAC's 
       "beacon on request" but skipping the beacons */
      packet_pushed = 0;
      next = list_item_next(curr_packet_list);

      queuebuf_to_packetbuf(curr_packet_list->buf);
      transmit_len = packetbuf_totlen();
      if(transmit_len == 0) {
        PRINTF("lpprdc: send_packet data len 0\n");
        mac_call_sent_callback(curr_callback, curr_ptr, MAC_TX_ERR_FATAL, 1);
        if(next != NULL) {
          curr_packet_list = next;
        }
        continue;
      }
      current_seqno = packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO);

      if(packetbuf_holds_broadcast()) {
        is_broadcast = 1;
        we_are_broadcasting = 1;
        /* If it's a broadcast, we won't push a packet, because it won't
           get acked anyway */
        packet_pushed++;
        linkaddr_copy(&current_receiver_addr, &linkaddr_null);

        broadcast_start_time = clock_time();
        PRINTDEBUG("lpprdc: send broadcast\n");

        if(broadcast_rate_drop()) {
          mac_call_sent_callback(curr_callback, curr_ptr, MAC_TX_COLLISION, 1);
          if(next != NULL) {
            curr_packet_list = next;
          }
          continue;
        }
      } else {
        we_are_broadcasting = 0;
        is_broadcast = 0;
        linkaddr_copy(&current_receiver_addr, packetbuf_addr(PACKETBUF_ADDR_RECEIVER));
#if NETSTACK_CONF_WITH_IPV6
        PRINTF("lpprdc: send unicast to %02x%02x:%02x%02x:%02x%02x:%02x%02x\n",
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[0],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[1],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[2],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[3],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[4],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[5],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[6],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[7]);
#else /* NETSTACK_CONF_WITH_IPV6 */
    /*PRINTDEBUG("lpprdc: send unicast to %u.%u\n",
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[0],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[1]);*/
#endif /* NETSTACK_CONF_WITH_IPV6 */
      } //broadcast or unicast
    } //!retry

    /* This seems to fix the case where a beacon was received on
       accident and not processed. */
    if(NETSTACK_RADIO.pending_packet()) {
      NETSTACK_RADIO.read(NULL, 0);
    }

  /* Set contikimac_is_on to one to allow the on() and off() functions
     to control the radio. We restore the old value of
     contikimac_is_on when we are done. */
    contikimac_was_on = contikimac_is_on;
    contikimac_is_on = 1;

    on();
    
    do {
      if(packet_pushed && !is_receiver_awake) {
        /* We need to wait for a probe before doing anything. */
        on();
      
        if(is_broadcast) {
          ctimer_set(&ct, ctimer_max_probe_interval - (clock_time() - broadcast_start_time), listening_off, NULL);
        } else if(retry && current_backoff_window > 0) {
          ctimer_set(&ct, (uint32_t)CLOCK_SECOND * current_backoff_window / RTIMER_ARCH_SECOND, listening_off, NULL);
        } else {
          ctimer_set(&ct, listen_for_probe_timeout, listening_off, NULL);
        }
        /* Yield until a beacon is heard (input) or timeout */
        PT_YIELD(&send_pt);
        ctimer_stop(&ct);
      }

      if(!is_receiver_awake && packet_pushed) {
        off();
        contikimac_is_on = contikimac_was_on;
        queuebuf_to_packetbuf(curr_packet_list->buf);
        if(!is_broadcast) {
          mac_call_sent_callback(curr_callback, curr_ptr, MAC_TX_NOACK, 1);
        } else {
          mac_call_sent_callback(curr_callback, curr_ptr, MAC_TX_OK, 1);
        }
        we_are_sending = 0;
        retry = 0;
        break;
      }

      /* We back off while listening for more probes */
      if(current_backoff_window > 0) {
        //PRINTF("b %u\n", current_backoff_window);
        is_receiver_awake = 0;
        uint16_t backoff_time = random_rand() % current_backoff_window;
        backoff_time = MAX(backoff_time, RTIMER_GUARD_TIME);
        PRINTF("b %u %u\n", current_backoff_window, backoff_time);
        rtimer_set(&rt, RTIMER_NOW() + backoff_time, 1, backoff_timeout, NULL);
        PT_YIELD(&send_pt);
        /* If we got another probe, we should now follow backoff instructions for that probe */
        if(is_receiver_awake) {
          continue;
        }
      }

      /* If we just backed off or we are pushing a packet, we should be polite
         and make sure we're not interrupting a data/probe exchange */
      if(current_backoff_window > 0 || !packet_pushed) {
        rtimer_clock_t start_time = RTIMER_NOW();
        while(RTIMER_CLOCK_LT(RTIMER_NOW(), start_time + PROBE_RECEIVE_DURATION + AFTER_ACK_DETECTED_WAIT_TIME)) {
          if(!NETSTACK_RADIO.channel_clear()) {
            //PRINTF("z\n");
            start_time = RTIMER_NOW();
          }
        }
      }

      we_are_sending = 1;
      queuebuf_to_packetbuf(curr_packet_list->buf);
#if RDC_CONF_HARDWARE_ACK
      off();
#endif

      if(NETSTACK_RADIO.prepare(packetbuf_hdrptr(), transmit_len)) {
        if(!packet_pushed++) {
          continue;
        }
        collisions++;
        break;
      }

      ret = NETSTACK_RADIO.transmit(transmit_len);

      is_receiver_awake = 0;

      if(is_broadcast) {
        we_are_sending = 0;
        packet_pushed++;
        continue;
      }

      if(ret == RADIO_TX_OK) {
        ctimer_set(&ct, 3, listening_off, NULL);
        /* Wait for the ACK probe packet */
        expecting_ack = 1;
        PT_YIELD(&send_pt);
        ctimer_stop(&ct);
        if(!expecting_ack) {
          PRINTF("!\n");
          got_packet_ack = 1;
          retry = 0;
          packet_pushed++;
        } else {
          if(is_receiver_awake) {
            //PRINTF("lpprdc: backoff beacon received\n");
            retry = 1;
          } else {
            //TODO: If we didn't hear a probe while listening for an ACK, what does that mean?
            PRINTF("lpprdc: ack listen timed out\n");
          }
        }
      } else if(packet_pushed) {
        /* Unicast and radio didn't return OK... radio layer collision */
        retry = 1;
      }
    } while (!packet_pushed++ || (is_broadcast && clock_time() < broadcast_start_time + ctimer_max_probe_interval));

    if(!retry) {
      off();
    }

#if CONTIKIMAC_CONF_COMPOWER
    /* Accumulate the power consumption for the packet transmission. */
    compower_accumulate(&current_packet);

    /* Convert the accumulated power consumption for the transmitted
       packet to packet attributes so that the higher levels can keep
       track of the amount of energy spent on transmitting the
       packet. */
    compower_attrconv(&current_packet);

    /* Clear the accumulated power consumption so that it is ready for
       the next packet. */
    compower_clear(&current_packet);
#endif /* CONTIKIMAC_CONF_COMPOWER */

    contikimac_is_on = contikimac_was_on;
    we_are_sending = 0;

    if(got_packet_ack || is_broadcast) {
      //queuebuf_to_packetbuf(curr_packet_list->buf);
      //mac_call_sent_callback(curr_callback, curr_ptr, MAC_TX_OK, 1);
      packets_to_ok[packets_to_ok_index++] = curr_packet_list->buf;
      if(next != NULL) {
        /* We're in a burst, no need to wake the receiver up again */
        //is_receiver_awake = 1;
        curr_packet_list = next;
      }
    } else if(retry) {
      PRINTF("lpprdc: retrying\n");
      continue;
    } else {
      /* This means no ACK, but no probe either */
      queuebuf_to_packetbuf(curr_packet_list->buf);
      mac_call_sent_callback(curr_callback, curr_ptr, MAC_TX_NOACK, 1);
      /* We break instead of trying any additional packets because
         CSMA currently only puts one neighbor's packets in the list. */
      break;
    }
  } while(retry || (next != NULL));

  is_receiver_awake = 0;
  we_are_listening = 0;
  retry = 0;

  int i;
  for(i = 0; i < MAX_QUEUED_PACKETS; i++) {
    if(packets_to_ok[i] != NULL) {
      queuebuf_to_packetbuf(packets_to_ok[i]);
      mac_call_sent_callback(curr_callback, curr_ptr, MAC_TX_OK, 1);
    } else {
      break;
    }
  }

  PT_END(&send_pt);
}
/*---------------------------------------------------------------------------*/
static void
qsend_list(mac_callback_t sent, void *ptr, struct rdc_buf_list *buf_list)
{
  struct rdc_buf_list *curr;
  struct rdc_buf_list *next;

  if(buf_list == NULL) {
    return;
  }

  /* Do not send during reception of a burst */
  if(we_are_listening || we_are_probing) {
    PRINTF("failed qsend\n");
    /* Prepare the packetbuf for callback */
    queuebuf_to_packetbuf(buf_list->buf);
    /* Return COLLISION so the MAC may try again later */
    mac_call_sent_callback(sent, ptr, MAC_TX_COLLISION, 1);
    return;
  }

  /* Create and secure frames in advance */
  curr = buf_list;
  do {
    next = list_item_next(curr);
    queuebuf_to_packetbuf(curr->buf);
    if(!packetbuf_attr(PACKETBUF_ATTR_IS_CREATED_AND_SECURED)) {
      /* create and secure this frame */
      if(next != NULL) {
        packetbuf_set_attr(PACKETBUF_ATTR_PENDING, 1);
      }
#if !NETSTACK_CONF_BRIDGE_MODE
      /* If NETSTACK_CONF_BRIDGE_MODE is set, assume PACKETBUF_ADDR_SENDER is already set. */
      packetbuf_set_addr(PACKETBUF_ADDR_SENDER, &linkaddr_node_addr);
#endif
      packetbuf_set_attr(PACKETBUF_ATTR_MAC_ACK, 0);
      if(NETSTACK_FRAMER.create() < 0) {
        PRINTF("lpprdc: framer failed\n");
        mac_call_sent_callback(sent, ptr, MAC_TX_ERR_FATAL, 1);
        return;
      }

      packetbuf_set_attr(PACKETBUF_ATTR_IS_CREATED_AND_SECURED, 1);
      queuebuf_update_from_packetbuf(curr->buf);
    }
    curr = next;
  } while(next != NULL);

  int i;
  for(i = 0; i < MAX_QUEUED_PACKETS; i++) {
    packets_to_ok[i] = NULL;
  }
  packets_to_ok_index = 0;

  curr_packet_list = buf_list;
  curr_callback = sent;
  curr_ptr = ptr;
  PT_INIT(&send_pt);
  send_packet();
}
/*---------------------------------------------------------------------------*/
static void
qsend_packet(mac_callback_t sent, void *ptr)
{
  static struct rdc_buf_list list;
  list.next = NULL;
//TODO: We should not be creating a queuebuf here if we can't free it somehow.
//CSMA only calls qsend_list though.
  list.buf = queuebuf_new_from_packetbuf();
  list.ptr = NULL;
  qsend_list(sent, ptr, &list);
}
/*---------------------------------------------------------------------------*/
/* Timer callback triggered when receiving a burst, after having
   waited for a next packet for a too long time. Turns the radio off
   and leaves burst reception mode */
static void
recv_burst_off(void *ptr)
{
  if(we_are_probing) {
    powercycle(NULL);
  } else {
    off();
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(input_process, ev, data)
{
  PROCESS_BEGIN();
  while(1) {
    PROCESS_WAIT_EVENT();
    if(ev == PROCESS_EVENT_CONTINUE) {
      we_are_sending = 1;
      queuebuf_to_packetbuf((struct queuebuf *)data);
      NETSTACK_MAC.input();
      we_are_sending = 0;
      queuebuf_free((struct queuebuf *)data);
      //input_queuebuf_locked = 0;
    }
  }
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
static void
input_packet(void)
{
  static struct ctimer ct;
  static int duplicate = 0;
  static int is_broadcast = 0;
  static struct queuebuf *rec_buf;
  static uint8_t seqno;
  static linkaddr_t addr_to_ack;
  static struct probe_packet probe;
  static uint8_t they_are_probing = 0;

  we_are_processing_input = 1;
  
  if(!we_are_probing && !we_are_listening) {
    off();
  }
  
  if(packetbuf_datalen() == ACK_LEN) {
    we_are_processing_input = 0;
    return;
  }

  if(packetbuf_totlen() > 0 && NETSTACK_FRAMER.parse() >= 0) {
    activity_seen = 0;
    if(packetbuf_datalen() == sizeof(struct probe_packet)) {
      //PRINTF("p\n");
      they_are_probing = ((struct probe_packet *)packetbuf_dataptr())->probing;
      if(linkaddr_cmp(packetbuf_addr(PACKETBUF_ADDR_RECEIVER), &linkaddr_node_addr)) {
        if(expecting_ack) {
          current_backoff_window = ((struct probe_packet *)(packetbuf_dataptr()))->backoff_window;
          expecting_ack = 0;
          if(they_are_probing) {
            is_receiver_awake = 1;
          }
          send_packet();
          
        } else {

        }
      } else if(we_are_listening && they_are_probing &&
        (linkaddr_cmp(packetbuf_addr(PACKETBUF_ADDR_SENDER), &current_receiver_addr) || we_are_broadcasting)) {
        current_backoff_window = ((struct probe_packet *)(packetbuf_dataptr()))->backoff_window;
        is_receiver_awake = 1;
        send_packet();
      } else {
        //PRINTF("lpprdc: ignored beacon %u %u\n", packetbuf_addr(PACKETBUF_ADDR_SENDER)->u8[7], current_receiver_addr.u8[7]);
      }
      we_are_processing_input = 0;
      if(we_are_probing && !probe_timer_is_running) {
        powercycle(NULL);
      }
      return;
    }

    is_broadcast = packetbuf_holds_broadcast();
    if(packetbuf_datalen() > 0 &&
       packetbuf_totlen() > 0 &&
       (linkaddr_cmp(packetbuf_addr(PACKETBUF_ADDR_RECEIVER),
                     &linkaddr_node_addr) ||
        is_broadcast)) {
      /* This is a regular packet that is destined to us or to the
         broadcast address. */
      //PRINTF("lpprdc: packet received, broadcast: %d\n", packetbuf_holds_broadcast());

        if(!(we_are_listening || we_are_probing)) {
          off();
        }


#if CONTIKIMAC_CONF_COMPOWER
      /* Accumulate the power consumption for the packet reception. */
      compower_accumulate(&current_packet);
      /* Convert the accumulated power consumption for the received
         packet to packet attributes so that the higher levels can
         keep track of the amount of energy spent on receiving the
         packet. */
      compower_attrconv(&current_packet);

      /* Clear the accumulated power consumption so that it is ready
         for the next packet. */
      compower_clear(&current_packet);
#endif /* CONTIKIMAC_CONF_COMPOWER */

     if(!is_broadcast) { /*|| (is_broadcast && we_are_probing &&
        RTIMER_CLOCK_DIFF(RTIMER_NOW(), last_probe_time) <= AFTER_PROBE_SENT_WAIT_TIME + PACKET_RECEIVE_DURATION)) {*/
        seqno = packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO);
        linkaddr_copy(&addr_to_ack, packetbuf_addr(PACKETBUF_ADDR_SENDER));
        probe.backoff_window = 0;
        probe.probing = we_are_probing;
        rec_buf = queuebuf_new_from_packetbuf();
        packetbuf_copyfrom(&probe, sizeof(struct probe_packet));
        packetbuf_set_attr(PACKETBUF_ATTR_MAC_ACK, 0);
        packetbuf_set_attr(PACKETBUF_ATTR_MAC_SEQNO, seqno);
        packetbuf_set_addr(PACKETBUF_ADDR_SENDER, &linkaddr_node_addr);
        packetbuf_set_addr(PACKETBUF_ADDR_RECEIVER, &addr_to_ack);
        if(NETSTACK_FRAMER.create() < 0) {
          PRINTF("?\n");
          waiting_for_response = 1;
        } else if(NETSTACK_RADIO.prepare(packetbuf_hdrptr(), packetbuf_totlen())) {
          PRINTF("??\n");
          waiting_for_response = 1;
        } else {
          if(NETSTACK_RADIO.transmit(packetbuf_totlen() != RADIO_TX_OK)) {
            PRINTF("???\n");
            waiting_for_response = 1;
          } else {
            last_probe_time = RTIMER_NOW();
            if(current_backoff_window == 0) {
              continue_probing = 0;
            } else {
              continue_probing = 1;
            }
            current_backoff_window = 0;
            waiting_for_response = 0;
          }
        }
        queuebuf_to_packetbuf(rec_buf);
        queuebuf_free(rec_buf);
      } else if(we_are_probing && current_backoff_window == 0) {
        /* If we just received a broadcast and aren't in a backoff
           loop, we can sleep. */
        waiting_for_response = 0;
        off();
      }

#if RDC_WITH_DUPLICATE_DETECTION
      /* Check for duplicate packet. */
      duplicate = mac_sequence_is_duplicate();
      if(duplicate) {
        /* Drop the packet. */
        //PRINTF("lpprdc: Drop duplicate\n");
      } else {
        mac_sequence_register_seqno();
      }
#endif /* RDC_WITH_DUPLICATE_DETECTION */

      if(!duplicate) {
        struct queuebuf *msg;
        msg = queuebuf_new_from_packetbuf();
        process_post(&input_process, PROCESS_EVENT_CONTINUE, msg);
        /*if(is_broadcast && !input_queuebuf_locked) {
          queuebuf_update_from_packetbuf(&input_queuebuf);
          input_queuebuf_locked = 1;
          PROCESS_POST(&input_process, PROCESS_EVENT_CONTINUE, NULL);
        } else {
          if(is_broadcast) {
            PRINTF("lpprdc: input queuebuf locked\n");
          }
          NETSTACK_MAC.input();
        }*/
      }
      we_are_processing_input = 0;
      if(we_are_probing) {
        powercycle(NULL);
      }
      return;
    } else {
      //PRINTDEBUG("lpprdc: data not for us\n");
    }
  } else {
    //PRINTF("lpprdc: failed to parse (%u)\n", packetbuf_totlen());
  }
  we_are_processing_input = 0;
      if(we_are_probing && !probe_timer_is_running) {
        powercycle(NULL);
      }
}
/*---------------------------------------------------------------------------*/
static void
init(void)
{
  /* Turn off address filtering and autoack so we can overhear packets
     during contention resolution, and so we can use probes as acks. */
  radio_value_t radio_rx_mode = 0;
  if(NETSTACK_RADIO.get_value(RADIO_PARAM_RX_MODE, &radio_rx_mode) != RADIO_RESULT_OK) {
    PRINTF("lpprdc: failed to get radio params, aborting\n");
    return;
  }
  printf("rxmode %d\n", radio_rx_mode);
  radio_rx_mode &= ~RADIO_RX_MODE_ADDRESS_FILTER;
  radio_rx_mode &= ~RADIO_RX_MODE_AUTOACK;
    printf("rxmode %d\n", radio_rx_mode);
  if(NETSTACK_RADIO.set_value(RADIO_PARAM_RX_MODE, radio_rx_mode) != RADIO_RESULT_OK) {
    PRINTF("lpprdc: failed to set radio params, aborting\n");
    return;
  }
  radio_is_on = 0;

  struct probe_packet probe;
  probe.backoff_window = 0;
  //probe.msg = '!';
  packetbuf_copyfrom(&probe, sizeof(struct probe_packet));
  packetbuf_set_addr(PACKETBUF_ADDR_RECEIVER, &linkaddr_null);
  packetbuf_set_addr(PACKETBUF_ADDR_SENDER, &linkaddr_node_addr);
  if(NETSTACK_FRAMER.create() < 0) {
    PRINTDEBUG("lpprdc: Failed to create probe packet.\n");
  }

  probe_len = packetbuf_totlen();

  probe_rate = NETSTACK_RDC_CHANNEL_CHECK_RATE;
  probe_interval = CLOCK_SECOND / probe_rate;
  ctimer_max_probe_interval = probe_interval + probe_interval / NETSTACK_RDC_CHANNEL_CHECK_RANDOMNESS_FRACTION;
  max_probe_interval = RTIMER_ARCH_SECOND / CLOCK_SECOND * ctimer_max_probe_interval;
  listen_for_probe_timeout = 3*ctimer_max_probe_interval;
  PRINTF("listen_for_probe_timeout %u\n", listen_for_probe_timeout);

  PT_INIT(&pt);
  PT_INIT(&send_pt);

  process_start(&input_process, NULL);

  ctimer_set(&probe_timer, probe_interval, powercycle_ctimer_wrapper, NULL);

  contikimac_is_on = 1;
}
/*---------------------------------------------------------------------------*/
static int
turn_on(void)
{
  if(contikimac_is_on == 0) {
    contikimac_is_on = 1;
    contikimac_keep_radio_on = 0;
    ctimer_set(&probe_timer, probe_interval, powercycle_ctimer_wrapper, NULL);
  }
  return 1;
}
/*---------------------------------------------------------------------------*/
static int
turn_off(int keep_radio_on)
{
  contikimac_is_on = 0;
  contikimac_keep_radio_on = keep_radio_on;
  if(keep_radio_on) {
    radio_is_on = 1;
    return NETSTACK_RADIO.on();
  } else {
    radio_is_on = 0;
    return NETSTACK_RADIO.off();
  }
}
/*---------------------------------------------------------------------------*/
static unsigned short
duty_cycle(void)
{
  /* This is called by CSMA to set the backoff length */
  return (1ul * CLOCK_SECOND / probe_rate);
}
/*---------------------------------------------------------------------------*/
const struct rdc_driver lpprdc_driver = {
  "LPP-RDC",
  init,
  qsend_packet,
  qsend_list,
  input_packet,
  turn_on,
  turn_off,
  duty_cycle,
};
/*---------------------------------------------------------------------------*/
uint16_t
lpprdc_debug_print(void)
{
  return 0;
}
/*---------------------------------------------------------------------------*/
