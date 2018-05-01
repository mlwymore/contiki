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
#define CCA_SLEEP_TIME                     RTIMER_ARCH_SECOND / 4000 + CCA_CHECK_TIME
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

#define NETSTACK_RDC_CHANNEL_CHECK_RANDOMNESS_FRACTION 2

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
#if LPPRDC_CONF_WITH_PROBE_TRAIN
#define AFTER_PROBE_SENT_WAIT_TIME         RTIMER_ARCH_SECOND / 1000
#else
#define AFTER_PROBE_SENT_WAIT_TIME         RTIMER_ARCH_SECOND / 1100
#endif
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

#define PROBE_CHANNEL_CHECK_TIME          RTIMER_ARCH_SECOND / 500
#define DATA_CHANNEL_CHECK_TIME           PROBE_CHANNEL_CHECK_TIME

/* MAX_PHASE_STROBE_TIME is the time that we transmit repeated packets
   to a neighbor for which we have a phase lock. */
#ifdef CONTIKIMAC_CONF_MAX_PHASE_STROBE_TIME
#define MAX_PHASE_STROBE_TIME              CONTIKIMAC_CONF_MAX_PHASE_STROBE_TIME
#else
#define MAX_PHASE_STROBE_TIME              RTIMER_ARCH_SECOND / 60
#endif

/* 320 us, the same as RI-MAC paper */
#define BACKOFF_SLOT_LENGTH (RTIMER_ARCH_SECOND / 3125)

#define MAX_BACKOFF_WINDOW 16

#include <stdio.h>
static struct rtimer rt;
static struct ctimer probe_timer;

#if RPL_CONF_OPP_ROUTING
#include "net/rpl/rpl.h"
static uint8_t we_are_anycasting;
#endif
static linkaddr_t current_receiver_addr;

#if LPPRDC_CONF_WITH_PROBE_TRAIN
static linkaddr_t last_receiver_to_ack;
static volatile uint8_t probe_for_sending = 0;
static volatile uint8_t prev_probes_remaining = 0;
#define INPUT_QUEUE_SIZE 64
static volatile uint8_t msg_index = 0;
static struct queuebuf * msg_array[INPUT_QUEUE_SIZE];
#endif

static volatile uint8_t current_seqno = 0;

static struct rdc_buf_list *curr_packet_list;
static mac_callback_t curr_callback;
static void *curr_ptr;

static volatile uint8_t contikimac_is_on = 0;
volatile uint8_t contikimac_keep_radio_on = 0;

static volatile unsigned char packetbuf_is_locked = 0;
volatile unsigned char we_are_sending = 0;
static volatile unsigned char we_are_broadcasting = 0;
volatile unsigned char we_are_probing = 0;
static volatile unsigned char is_receiver_awake = 0;
static volatile unsigned char expecting_ack = 0;
static volatile unsigned char waiting_for_response = 0;
static volatile unsigned char activity_seen = 0;
static volatile unsigned char we_are_processing_input = 0;
static volatile unsigned char probe_timer_is_running = 0;
static volatile unsigned char continue_probing = 0;
static volatile unsigned char packet_was_sent = 0;
static volatile unsigned char packet_was_acked = 0;

static volatile uint16_t probe_rate = 0;
static volatile uint16_t probe_interval = 0;
static volatile uint16_t max_probe_interval = 0;
static volatile uint16_t ctimer_max_probe_interval = 0;
static volatile uint16_t listen_for_probe_timeout = 0;
static volatile clock_time_t backoff_probe_timeout = 0;
static volatile rtimer_clock_t current_backoff_window = 0;
static volatile rtimer_clock_t last_probe_time;

#if LPPRDC_CONF_WITH_PROBE_TRAIN
#define ACKBUF_SIZE 26
#define PROBEBUF_SIZE 18
#else
#define ACKBUF_SIZE 28
#define PROBEBUF_SIZE 20
#endif
static int transmit_len = 0;
static uint8_t sendbuf[PACKETBUF_SIZE];
static uint8_t ackbuf[ACKBUF_SIZE];
static uint8_t probebuf[PROBEBUF_SIZE];

PROCESS(probe_process, "probe process");
PROCESS(send_process, "send process");
PROCESS(input_process, "input process");
 
static struct queuebuf input_queuebuf;
static volatile uint8_t input_queuebuf_locked = 0;

#if LPPRDC_CONF_WITH_PROBE_TRAIN
struct probe_packet {
  uint8_t probes_remaining;
};
#else
struct probe_packet {
  rtimer_clock_t backoff_window;
  uint8_t probing;
};
#endif

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
  if(contikimac_is_on) {
    NETSTACK_RADIO.on();
  }
}
/*---------------------------------------------------------------------------*/
static void
off(void)
{
  if(contikimac_is_on &&
     contikimac_keep_radio_on == 0) {
    NETSTACK_RADIO.off();
  }
}
/*---------------------------------------------------------------------------*/
static void process_input_queue(void);
/*---------------------------------------------------------------------------*/
static void powercycle_wrapper(struct rtimer *t, void *ptr);
/*---------------------------------------------------------------------------*/
static void
powercycle_turn_radio_off(void)
{
#if CONTIKIMAC_CONF_COMPOWER
  radio_value_t was_on;
  NETSTACK_RADIO.get_value(RADIO_PARAM_POWER_MODE, &was_on);
#endif /* CONTIKIMAC_CONF_COMPOWER */
  if(!contikimac_is_on) {
    return;
  }
  if(we_are_sending == 0 && !NETSTACK_RADIO.receiving_packet()) {
    off();
#if CONTIKIMAC_CONF_COMPOWER
    radio_value_t radio_is_on;
    NETSTACK_RADIO.get_value(RADIO_PARAM_POWER_MODE, &was_on);
    if(was_on && !radio_is_on) {
      compower_accumulate(&compower_idle_activity);
    }
#endif /* CONTIKIMAC_CONF_COMPOWER */
  } else {
    PRINTF("lpprdc: not turning radio off %d %d\n", we_are_sending, NETSTACK_RADIO.receiving_packet());
  }
}
/*---------------------------------------------------------------------------*/
static void
powercycle_turn_radio_on(void)
{
  if(packetbuf_is_locked == 0 && we_are_sending == 0) {
    on();
  }
}
/*---------------------------------------------------------------------------*/
/* Uses CCAs to check if the channel is clear for the interval time_clear. If
   with_restart, will continually check for a period of time_clear that is clear. */
static int
channel_is_clear(rtimer_clock_t time_clear, int with_restart)
{
  rtimer_clock_t start = RTIMER_NOW();
  rtimer_clock_t wt;
  while(1) {
    on();
    if(!NETSTACK_RADIO.channel_clear()) {
      if(with_restart) {
        start = RTIMER_NOW();
      } else {
        off();
        return 0;
      }
    }
    if(!RTIMER_CLOCK_LT(RTIMER_NOW(), start + time_clear)) {
      break;
    } else {
      off();
      wt = RTIMER_NOW();
      while(RTIMER_CLOCK_LT(RTIMER_NOW(), wt + CCA_SLEEP_TIME)) { }
    }
  }
  return 1;
}
/*---------------------------------------------------------------------------*/
static void
powercycle_wrapper(struct rtimer *t, void *ptr)
{
  probe_timer_is_running = 0;
  if(waiting_for_response && !we_are_processing_input) {
    if(!activity_seen) {
      activity_seen = NETSTACK_RADIO.receiving_packet() | !NETSTACK_RADIO.channel_clear();
    }
    //process_poll(&probe_process);
    process_post(&probe_process, PROCESS_EVENT_POLL, NULL);
  }
}
/*---------------------------------------------------------------------------*/
static void
powercycle_bare_wrapper(struct rtimer *t, void *ptr)
{
  if(waiting_for_response) {
    process_poll(&probe_process);
    PRINTF(":D\n");
  } else {
    PRINTF("D:\n");
  }
}
/*---------------------------------------------------------------------------*/
static void
powercycle_ctimer_wrapper(void *ptr)
{
  process_post(&probe_process, PROCESS_EVENT_CONTINUE, NULL);
}
/*---------------------------------------------------------------------------*/
/*
 * Send probes and listen for a response.
*/
#if LPPRDC_CONF_WITH_PROBE_TRAIN
PROCESS_THREAD(probe_process, ev, data)
{
  PROCESS_BEGIN();

  static volatile uint8_t num_probes_remaining = 0;
  static rtimer_clock_t wt = 0;
  static uint8_t send_probe = 0;

  PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_CONTINUE);

  while(1) {
    if(!we_are_sending) {
      //Prep the initial probe. Backoff value is number of probes remaining in train. Starts with 0.
      
      if(channel_is_clear(PROBE_CHANNEL_CHECK_TIME, 0)) {
        send_probe = 1;
        we_are_probing = 1;
        current_backoff_window = 1;
        num_probes_remaining = 2;
        while(num_probes_remaining-- > 0) {
          //num_probes_remaining--;
          if(send_probe && num_probes_remaining > 0) {
            probebuf[16] = num_probes_remaining;
            if(NETSTACK_RADIO.prepare(probebuf, PROBEBUF_SIZE)) {
              PRINTF("lpprdc: radio locked while probing\n");
              break;
            } else if(NETSTACK_RADIO.transmit(PROBEBUF_SIZE) != RADIO_TX_OK) {
              PRINTF("lpprdc: collision while transmitting probe.\n");
              break;
            }
          } else if(num_probes_remaining <= 0) {
            break;
          }
          PRINTF("pr %d\n", num_probes_remaining);
          /* Probe was sent - wait for softack_input_callback and listen for collision */
          activity_seen = 0;
          waiting_for_response = 1;
          /* Need to wait a little extra time if we didn't just send a probe (i.e. the send was handled by softack code) */
          rtimer_clock_t extra_wait = send_probe == 1 ? 0 : AFTER_PROBE_SENT_WAIT_TIME / 4;

          rtimer_set(&rt, RTIMER_NOW() + AFTER_PROBE_SENT_WAIT_TIME*3/2 + extra_wait, 1,
                     powercycle_bare_wrapper, NULL);
          
          PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_POLL);
          PRINTF("5\n");
          
          if(!NETSTACK_RADIO.channel_clear() || NETSTACK_RADIO.receiving_packet()) {
            activity_seen = 1;
            wt = RTIMER_NOW();
            while(waiting_for_response && RTIMER_CLOCK_LT(RTIMER_NOW(), wt + PACKET_RECEIVE_DURATION)) {
              if(NETSTACK_RADIO.channel_clear() && !NETSTACK_RADIO.receiving_packet()) {
                break;
              }
              rtimer_set(&rt, RTIMER_NOW() + CCA_SLEEP_TIME / 2, 1,
                       powercycle_bare_wrapper, NULL);
              PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_POLL);
              PRINTF("0\n");
            }
          }

          if(!waiting_for_response) {
            PRINTF("2 %d\n", current_backoff_window);
            /* we sent an ack */
            send_probe = 0;
            if(current_backoff_window > 0) {
              num_probes_remaining = 2;
              current_backoff_window = 1;
            } else {
              num_probes_remaining = 0;
            }
          } else if(activity_seen) {
            PRINTF("3 %d\n", activity_seen);
            /* collision detected - if probes remaining, wait for the channel to clear */
            if(current_backoff_window < MAX_BACKOFF_WINDOW) {
              if(current_backoff_window == 0) {
                current_backoff_window = 1;
              } else {
                current_backoff_window = MIN(current_backoff_window * 2, MAX_BACKOFF_WINDOW);
              }
              num_probes_remaining = current_backoff_window + 1;
            }

            if(num_probes_remaining > 0) {
              //while(!NETSTACK_RADIO.channel_clear()) { }
              send_probe = 1;
            }
          }
        }
      }
      powercycle_turn_radio_off();
      process_input_queue();
    }
    PRINTF("4\n");
    current_backoff_window = 0;
    we_are_probing = 0;
    int32_t rand = random_rand() % (probe_interval / NETSTACK_RDC_CHANNEL_CHECK_RANDOMNESS_FRACTION);
    rand -= (probe_interval / NETSTACK_RDC_CHANNEL_CHECK_RANDOMNESS_FRACTION) / 2;
    uint32_t probe_interval_rand = (uint32_t)probe_interval + rand;
    ctimer_set(&probe_timer, probe_interval_rand, powercycle_ctimer_wrapper, NULL);
    PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_CONTINUE);
  }

  PROCESS_END();
}
#else /* LPPRDC_CONF_WITH_PROBE_TRAIN */
PROCESS_THREAD(probe_process, ev, data)
{
  PROCESS_BEGIN();

  static struct probe_packet probe;
  static uint8_t probe_attempts = 0;
  static rtimer_clock_t wait_time = 0;
  static rtimer_clock_t time_clear = 0;
  static struct queuebuf *rec_buf;
  static rtimer_clock_t wt;
  static int32_t rand = 0;
  static uint32_t probe_interval_rand = 0;

  PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_CONTINUE);

  while(1) {
    if(we_are_probing) {
    if(we_are_sending == 0 &&
       (!(NETSTACK_RADIO.receiving_packet() || NETSTACK_RADIO.pending_packet()) ||
        (NETSTACK_RADIO.pending_packet() && !probe_attempts))) {
    if(NETSTACK_RADIO.pending_packet()) {
      NETSTACK_RADIO.read(NULL, 0);
    }
    if(!waiting_for_response) {
      //we_are_probing = 1;
      if(probe_attempts > 0) {
        wait_time = ((16 << probe_attempts) - 1);
      } else {
        wait_time = 0;
      }
      probebuf[16] = (uint8_t)(wait_time);
      probebuf[17] = (uint8_t)(wait_time >> 8);
      wait_time = wait_time * BACKOFF_SLOT_LENGTH;
      if(wait_time) {
        wait_time += PACKET_RECEIVE_DURATION;
      }

      if(we_are_probing && NETSTACK_RADIO.prepare(probebuf, PROBEBUF_SIZE)) {
        PRINTF("lpprdc: radio locked while probing\n");
        powercycle_turn_radio_off();
        we_are_probing = 0;
      } else if(we_are_probing) {
        if(probe_attempts || channel_is_clear(PROBE_CHANNEL_CHECK_TIME, 0)) {
        //powercycle_turn_radio_on();
        if(NETSTACK_RADIO.transmit(PROBEBUF_SIZE) != RADIO_TX_OK) {
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
        we_are_probing = 0;
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
        while(RTIMER_CLOCK_LT(RTIMER_NOW(), wt + 5 + RTIMER_GUARD_TIME)) { }
        while(waiting_for_response && RTIMER_CLOCK_LT(RTIMER_NOW(), wt + wait_time)) {
          if(activity_seen) {
            if(NETSTACK_RADIO.channel_clear() && !NETSTACK_RADIO.receiving_packet()) {
              if(time_clear == 0) {
                time_clear = RTIMER_NOW();
              }
              if(RTIMER_CLOCK_DIFF(RTIMER_NOW(), time_clear) > 
                 AFTER_ACK_DETECTED_WAIT_TIME && 
                 !NETSTACK_RADIO.pending_packet()) {
                break;
              }
            } else {
              time_clear = 0;
            }
          } else {
            if(!NETSTACK_RADIO.channel_clear() || NETSTACK_RADIO.receiving_packet()) {
              activity_seen = 1;
              time_clear = 0;
            }
          }

         
          rtimer_set(&rt, RTIMER_NOW() + CCA_SLEEP_TIME / 2, 1,
                     powercycle_wrapper, NULL);
          probe_timer_is_running = 1;
          PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_POLL);
          if(activity_seen) {
            wait_time += AFTER_ACK_DETECTED_WAIT_TIME;
          }
        }

        if(!waiting_for_response) {
          /* A response was received. We reset probe_attempts because
             of the successful contention resolution. */
          probe_attempts = 0;
          wait_time = 0;
          if(!continue_probing) {
            //Let the radio layer turn off the radio after the ack is done sending
          } else {
            waiting_for_response = 1;
            continue;
          }
        } else if(activity_seen) {
          if(probe_attempts < MAX_PROBE_ATTEMPTS) {
            waiting_for_response = 0;
            rtimer_clock_t start_time = RTIMER_NOW();
            while(RTIMER_CLOCK_LT(RTIMER_NOW(), start_time + AFTER_ACK_DETECTED_WAIT_TIME)) {
              if(NETSTACK_RADIO.channel_clear()) {
                break;
              }
            }
            continue;
          } else {
            powercycle_turn_radio_off();
          }
        } else {
          /* We timed out */
//PRINTF("p\n");
          powercycle_turn_radio_off();
        }
      }
    } else if(we_are_probing && !we_are_sending) {
      PRINTF("probe skipped but stuff\n");
      /* If we skipped because of an incoming packet, we need to give input a 
         chance to handle it. */
      if(NETSTACK_RADIO.pending_packet() || NETSTACK_RADIO.receiving_packet()) {
        rtimer_set(&rt, RTIMER_NOW() + CCA_SLEEP_TIME, 1,
                     powercycle_bare_wrapper, NULL);
        PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_POLL);
        /* If input didn't get called, just give up. This can happen if another
           packet was sent at the same time we tried to probe. */
    //    if((NETSTACK_RADIO.pending_packet() || NETSTACK_RADIO.receiving_packet()) && !we_are_processing_input) {
          off();
      //  }
      } else {
        rtimer_clock_t start_time = RTIMER_NOW();
        while(RTIMER_CLOCK_LT(RTIMER_NOW(), start_time + AFTER_ACK_DETECTED_WAIT_TIME)) {
           if(!NETSTACK_RADIO.channel_clear()) {
             start_time = RTIMER_NOW();
           }
        }
        continue;
      }
    } else {
      PRINTF("lpprdc: skipping probe\n");
    }
    waiting_for_response = 0;
    we_are_probing = 0;
    continue_probing = 0;
    rand = random_rand() % (probe_interval / NETSTACK_RDC_CHANNEL_CHECK_RANDOMNESS_FRACTION);
    rand -= (probe_interval / NETSTACK_RDC_CHANNEL_CHECK_RANDOMNESS_FRACTION) / 2;
    probe_interval_rand = (uint32_t)probe_interval + rand;
    ctimer_set(&probe_timer, probe_interval_rand, powercycle_ctimer_wrapper, NULL);
    probe_attempts = 0;
    }
    PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_CONTINUE);
  }

  PROCESS_END();
}
#endif /* LPPRDC_CONF_WITH_PROBE_TRAIN */
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
//static char send_packet();
/* Timer callback triggered on timeout when listening for probe */
static void
listening_off(void *ptr)
{
  //send_packet();
  process_poll(&send_process);
}
/*---------------------------------------------------------------------------*/
/* Timer callback triggered on timeout when waiting for backoff */
static void
backoff_timeout(struct rtimer *rt, void *ptr)
{
  if(!is_receiver_awake) {
    /* This means no more probes were heard - it's time to send. */
    process_poll(&send_process);
    //send_packet();
  }
}
/*---------------------------------------------------------------------------*/
static char
load_next_packet(int advance_list)
{
  while(1) {
    if(advance_list) {
      curr_packet_list = list_item_next(curr_packet_list);
    }
    if(curr_packet_list == NULL) {
      return 0;
    }
    queuebuf_to_packetbuf(curr_packet_list->buf);
    packetbuf_copyto(sendbuf);
    transmit_len = packetbuf_totlen();
    if(transmit_len <= 0) {
      curr_packet_list = list_item_next(curr_packet_list);
      continue;
    }
    linkaddr_copy(&current_receiver_addr, packetbuf_addr(PACKETBUF_ADDR_RECEIVER));
    we_are_broadcasting = packetbuf_holds_broadcast();
#if RPL_CONF_OPP_ROUTING
    we_are_anycasting = packetbuf_attr(PACKETBUF_ATTR_USE_OPP_ROUTING);
#endif
    current_seqno = packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO);
    return transmit_len;
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(send_process, ev, data)
{
  PROCESS_BEGIN();

  static uint8_t got_packet_ack = 0;
  static uint8_t collisions = 0;
  static uint8_t retry = 0;
  static int ret = 0;
  static uint8_t contikimac_was_on = 1;
  static struct rdc_buf_list *next;
  static struct ctimer ct;
  struct addr_list_item *item;
  int list_contains_addr = 0;
  static linkaddr_t receiver_addr;
  static int packet_pushed = 0;
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

  we_are_sending = 1;
  retry = 0;
  current_backoff_window = 0;

  if(!load_next_packet(0)) {
    PRINTF("lpprdc: no packet to send\n");
    return 0;
  }

  do {

    collisions = 0;

    if(!retry) {
    /* Send the packet once if channel clear - similar to RI-MAC's 
       "beacon on request" but skipping the beacons */
      packet_pushed = 0;
      
      if(we_are_broadcasting) {
        /* If it's a broadcast, we won't push a packet */
        packet_pushed++;

        broadcast_start_time = clock_time();
        PRINTF("lpprdc: send broadcast\n");

        if(broadcast_rate_drop()) {
          mac_call_sent_callback(curr_callback, curr_ptr, MAC_TX_COLLISION, 1);
          continue;
        }
      } else {
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
  
#if LPPRDC_CONF_WITH_PROBE_TRAIN
    linkaddr_copy(&last_receiver_to_ack, &linkaddr_null);
    probe_for_sending = 1;
    prev_probes_remaining = 1;
    do {
      packet_was_sent = 0;
      packet_was_acked = 0;
      if(packet_pushed && !is_receiver_awake) {
        /* We need to wait for a probe before doing anything. */
        on();
      
        if(we_are_broadcasting) {
          ctimer_set(&ct, ctimer_max_probe_interval - (clock_time() - broadcast_start_time), listening_off, NULL);
        } else if(retry && current_backoff_window > 0) {
          ctimer_set(&ct, (uint32_t)CLOCK_SECOND * current_backoff_window / RTIMER_ARCH_SECOND, listening_off, NULL);
        } else {
          ctimer_set(&ct, listen_for_probe_timeout, listening_off, NULL);
        }
        /* Yield until a beacon is heard (input) or timeout */
        PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_POLL);
        ctimer_stop(&ct);
      }

      if(!we_are_broadcasting && packet_was_sent && packet_was_acked) {
        break;
      }

      /* If we timed out listening for a probe, call it a noack (for unicast) or done (for broadcast) */
      if(!is_receiver_awake && packet_pushed) {
        off();
        contikimac_is_on = contikimac_was_on;
        //queuebuf_to_packetbuf(curr_packet_list->buf);
        retry = 0;
        break;
      } else if(we_are_broadcasting) {
        /* This is for if our broadcast has timed out, and we're just waiting for the current receiver to ack */
        PROCESS_WAIT_UNTIL(ev == PROCESS_EVENT_CONTINUE);
        break;
      }

      /* If we got a backoff beacon, we back off. */
      if(current_backoff_window > 0) {
        is_receiver_awake = 0;
        packet_was_sent = 0;
        packet_was_acked = 0;
        uint16_t backoff_beacons = random_rand() % current_backoff_window;
        //backoff_beacons = MAX(backoff_beacons, RTIMER_GUARD_TIME);
        //printf("b %u %u\n", current_backoff_window, backoff_time);
        //rtimer_set(&rt, RTIMER_NOW() + backoff_time, 1, backoff_timeout, NULL);
        PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_POLL);
        /* If we got another probe, we should now follow backoff instructions for that probe. Also,
           if we got another probe from a different node and responded to it, we shouldn't send again. */
        if(is_receiver_awake || (packet_was_sent && (we_are_broadcasting || packet_was_acked))) {
          continue;
        }
      }

      /* If we just backed off or we are pushing a packet, we should be polite
         and make sure we're not interrupting a data/probe exchange */
      if(current_backoff_window > 0 || !packet_pushed) {
        channel_is_clear(DATA_CHANNEL_CHECK_TIME, 1);
      }

      if(NETSTACK_RADIO.prepare(sendbuf, transmit_len)) {
        if(!packet_pushed++) {
          continue;
        }
        collisions++;
        break;
      }

      ret = NETSTACK_RADIO.transmit(transmit_len);
      packet_was_sent = 1;
      is_receiver_awake = 0;

      if(we_are_broadcasting) {
        packet_pushed++;
        continue;
      }

      if(ret == RADIO_TX_OK) {
        ctimer_set(&ct, 2, listening_off, NULL);
        /* Wait for the ACK probe packet */
        packet_was_acked = 0;
        PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_POLL);
        ctimer_stop(&ct);
        if(packet_was_acked) {
          packet_pushed++;
          retry = 0;
        } else {
          if(is_receiver_awake) {
            /* We must have heard a backoff probe */
            retry = 1;
            packet_pushed++;
          } else {
            /* Assume the receiver went to sleep, or is otherwise unavailable */
            retry = 0;
            PRINTF("lpprdc: ack listen timed out\n");
          }
        }
      } else if(packet_pushed) {
        /* Unicast and radio didn't return OK... radio layer collision */
        retry = 0;
        packet_pushed = -1;
        current_backoff_window = 0;
      }
    } while (!packet_pushed++ || (we_are_broadcasting && clock_time() < broadcast_start_time + ctimer_max_probe_interval));
#else /* LPPRDC_CONF_WITH_PROBE_TRAIN */    
    do {
      packet_was_sent = 0;
      packet_was_acked = 0;
      if(packet_pushed && !is_receiver_awake) {
        /* We need to wait for a probe before doing anything. */
        on();
      
        if(we_are_broadcasting) {
          ctimer_set(&ct, ctimer_max_probe_interval - (clock_time() - broadcast_start_time), listening_off, NULL);
        } else if(retry && current_backoff_window > 0) {
          ctimer_set(&ct, (uint32_t)CLOCK_SECOND * current_backoff_window / RTIMER_ARCH_SECOND, listening_off, NULL);
        } else {
          ctimer_set(&ct, listen_for_probe_timeout, listening_off, NULL);
        }
        /* Yield until a beacon is heard (input) or timeout */
        PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_POLL);
        ctimer_stop(&ct);
      }

      /* If we sent a packet and it was acked, we're ready for the next packet */
      if(!we_are_broadcasting && packet_was_sent && packet_was_acked) {
        break;
      }

      /* If we timed out listening for a probe, call it a noack (for unicast) or done (for broadcast) */
      if(!is_receiver_awake && packet_pushed) {
        off();
        contikimac_is_on = contikimac_was_on;
        //queuebuf_to_packetbuf(curr_packet_list->buf);
        retry = 0;
        break;
      }

      /* If we got a backoff beacon, we back off. */
      if(current_backoff_window > 0) {
        is_receiver_awake = 0;
        packet_was_sent = 0;
        packet_was_acked = 0;
        uint16_t backoff_time = (random_rand() % current_backoff_window) * BACKOFF_SLOT_LENGTH;
        backoff_time = MAX(backoff_time, RTIMER_GUARD_TIME);
        //printf("b %u %u\n", current_backoff_window, backoff_time);
        rtimer_set(&rt, RTIMER_NOW() + backoff_time, 1, backoff_timeout, NULL);
        PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_POLL);
        /* If we got another probe, we should now follow backoff instructions for that probe. Also,
           if we got another probe from a different node and responded to it, we shouldn't send again. */
        if(is_receiver_awake || (packet_was_sent && (we_are_broadcasting || packet_was_acked))) {
          continue;
        }
      }

      /* If we just backed off or we are pushing a packet, we should be polite
         and make sure we're not interrupting a data/probe exchange */
      if(current_backoff_window > 0 || !packet_pushed) {
        channel_is_clear(DATA_CHANNEL_CHECK_TIME, 1);
      }

      if(NETSTACK_RADIO.prepare(sendbuf, transmit_len)) {
        if(!packet_pushed++) {
          continue;
        }
        collisions++;
        break;
      }

      ret = NETSTACK_RADIO.transmit(transmit_len);
      packet_was_sent = 1;
      is_receiver_awake = 0;

      if(we_are_broadcasting) {
        packet_pushed++;
        continue;
      }

      if(ret == RADIO_TX_OK) {
        ctimer_set(&ct, 2, listening_off, NULL);
        /* Wait for the ACK probe packet */
        packet_was_acked = 0;
        PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_POLL);
        ctimer_stop(&ct);
        if(packet_was_acked) {
          packet_pushed++;
          retry = 0;
        } else {
          if(is_receiver_awake) {
            /* We must have heard a backoff probe */
            retry = 1;
            packet_pushed++;
          } else {
            /* Assume the receiver went to sleep, or is otherwise unavailable */
            retry = 0;
            PRINTF("lpprdc: ack listen timed out\n");
          }
        }
      } else if(packet_pushed) {
        /* Unicast and radio didn't return OK... radio layer collision */
        retry = 0;
        packet_pushed = -1;
        current_backoff_window = 0;
      }
    } while (!packet_pushed++ || (we_are_broadcasting && clock_time() < broadcast_start_time + ctimer_max_probe_interval));
#endif /* LPPRDC_CONF_WITH_PROBE_TRAIN */

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

    if(we_are_broadcasting) {
      queuebuf_to_packetbuf(curr_packet_list->buf);
      mac_call_sent_callback(curr_callback, curr_ptr, MAC_TX_OK, 1);
      if(!load_next_packet(1)) {
        break;
      }
    } else if(packet_was_acked) {
      PRINTF("!\n");
      queuebuf_to_packetbuf(curr_packet_list->buf);
      mac_call_sent_callback(curr_callback, curr_ptr, MAC_TX_OK, 1);
      if(!load_next_packet(1)) {
        break;
      }
    } else if(retry) {
      PRINTF("lpprdc: retrying\n");
    } else {
      /* This means no ACK, but no probe either */
      queuebuf_to_packetbuf(curr_packet_list->buf);
      mac_call_sent_callback(curr_callback, curr_ptr, MAC_TX_NOACK, 1);
      /* We break instead of trying any additional packets because
         CSMA currently only puts one neighbor's packets in the list. */
      break;
    }
  } while(1);
  process_input_queue();
  is_receiver_awake = 0;
  we_are_sending = 0;
  retry = 0;

  /*int i;
  for(i = 0; i < MAX_QUEUED_PACKETS; i++) {
    if(packets_to_ok[i] != NULL) {
      queuebuf_to_packetbuf(packets_to_ok[i]);
      mac_call_sent_callback(curr_callback, curr_ptr, MAC_TX_OK, 1);
    } else {
      break;
    }
  }*/

  PROCESS_END();
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
  if(we_are_sending || we_are_probing) {
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
#if !LPPRDC_CONF_WITH_PROBE_TRAIN
    if(!packetbuf_attr(PACKETBUF_ATTR_IS_CREATED_AND_SECURED)) {
#endif
      /* create and secure this frame */
      if(next != NULL) {
        packetbuf_set_attr(PACKETBUF_ATTR_PENDING, 1);
      }
#if !NETSTACK_CONF_BRIDGE_MODE
      /* If NETSTACK_CONF_BRIDGE_MODE is set, assume PACKETBUF_ADDR_SENDER is already set. */
      packetbuf_set_addr(PACKETBUF_ADDR_SENDER, &linkaddr_node_addr);
#endif
#if LPPRDC_CONF_WITH_PROBE_TRAIN
      packetbuf_set_attr(PACKETBUF_ATTR_MAC_ACK, 1);
#else
      packetbuf_set_attr(PACKETBUF_ATTR_MAC_ACK, !packetbuf_holds_broadcast());
#endif
      if(NETSTACK_FRAMER.create() < 0) {
        PRINTF("lpprdc: framer failed\n");
        mac_call_sent_callback(sent, ptr, MAC_TX_ERR_FATAL, 1);
        return;
      }

      packetbuf_set_attr(PACKETBUF_ATTR_IS_CREATED_AND_SECURED, 1);
      queuebuf_update_from_packetbuf(curr->buf);
#if !LPPRDC_CONF_WITH_PROBE_TRAIN
    }
#endif
    curr = next;
  } while(next != NULL);

  /*int i;
  for(i = 0; i < MAX_QUEUED_PACKETS; i++) {
    packets_to_ok[i] = NULL;
  }
  packets_to_ok_index = 0;*/

  curr_packet_list = buf_list;
  curr_callback = sent;
  curr_ptr = ptr;

  process_start(&send_process, NULL);
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
PROCESS_THREAD(input_process, ev, data)
{
  PROCESS_BEGIN();
  while(1) {
    PROCESS_WAIT_EVENT();
    if(ev == PROCESS_EVENT_CONTINUE) {
      PRINTF("in-p\n");
      packetbuf_is_locked = 1;
      queuebuf_to_packetbuf((struct queuebuf *)data);
      NETSTACK_MAC.input();
      packetbuf_is_locked = 0;
      queuebuf_free((struct queuebuf *)data);
      //input_queuebuf_locked = 0;
      PRINTF("in-p-d\n");
    }
  }
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
static void process_input_queue(void)
{
  PRINTF("input queue %d\n", msg_index);
  while(msg_index > 0) {
    PRINTF("in-p\n");
    packetbuf_is_locked = 1;
    queuebuf_to_packetbuf(msg_array[--msg_index]);
    NETSTACK_MAC.input();
    packetbuf_is_locked = 0;
    queuebuf_free(msg_array[msg_index]);
    //input_queuebuf_locked = 0;
    PRINTF("in-p-d\n");
  }
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
  PRINTF("in\n");
  
  if(!we_are_probing && !we_are_sending) {
    off();
  }

  if(packetbuf_totlen() > 0 && NETSTACK_FRAMER.parse() >= 0) {
#if !LPPRDC_CONF_WITH_PROBE_TRAIN
    activity_seen = 0;
#endif
    is_broadcast = packetbuf_holds_broadcast();
    if(packetbuf_attr(PACKETBUF_ATTR_FRAME_TYPE) == PACKETBUF_ATTR_PACKET_TYPE_PROBE) {
#if !LPPRDC_CONF_WITH_PROBE_TRAIN
    //if(packetbuf_datalen() == sizeof(struct probe_packet)) {
      if(we_are_sending && is_broadcast && (we_are_broadcasting ||
         linkaddr_cmp(packetbuf_addr(PACKETBUF_ADDR_SENDER), &current_receiver_addr))) {      
        current_backoff_window = ((struct probe_packet *)packetbuf_dataptr())->backoff_window;
        if(current_backoff_window) {
          is_receiver_awake = 1;
          process_poll(&send_process);
        }
      } else if(we_are_probing && !probe_timer_is_running) {
        process_poll(&probe_process);
      }
#endif
      we_are_processing_input = 0;
      return;
    } else if (packetbuf_attr(PACKETBUF_ATTR_FRAME_TYPE) == PACKETBUF_ATTR_PACKET_TYPE_ACK_PROBE) {
      we_are_processing_input = 0;
      return;
    }

#if !LPPRDC_CONF_WITH_PROBE_TRAIN
      if(is_broadcast && !we_are_sending && current_backoff_window == 0) {
        /* If we just received a broadcast and aren't in a backoff
           loop, we can sleep. */
        off();
        waiting_for_response = 0;
      }
#endif

    if(packetbuf_datalen() > 0 &&
       packetbuf_totlen() > 0 &&
       (linkaddr_cmp(packetbuf_addr(PACKETBUF_ADDR_RECEIVER),
                     &linkaddr_node_addr) ||
        is_broadcast)) {
      /* This is a regular packet that is destined to us or to the
         broadcast address. */

        //if(!(we_are_sending || we_are_probing)) {
        //  off();
        //}


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
#if LPPRDC_CONF_WITH_PROBE_TRAIN
        if((we_are_probing || we_are_sending) && msg_index < INPUT_QUEUE_SIZE) {
          PRINTF("m+\n");
          msg_array[msg_index++] = msg;
        } else if(!(we_are_probing || we_are_sending)) {
          process_post(&input_process, PROCESS_EVENT_CONTINUE, msg);
        } else if(msg_index >= INPUT_QUEUE_SIZE) {
          PRINTF("lpprdc: Input message queue full, message dropped.\n");
        }
#else
        process_post(&input_process, PROCESS_EVENT_CONTINUE, msg);
#endif
      } else {
        PRINTF("lpprdc: duplicate detected\n");
      }
#if !LPPRDC_CONF_WITH_PROBE_TRAIN
      if(we_are_probing && current_backoff_window == 0) {
PRINTF("k\n");
        if(is_broadcast) {
          waiting_for_response = 0;
          process_poll(&probe_process);
        }
      }
#endif
      we_are_processing_input = 0;
      return;
    } else {
      //PRINTDEBUG("lpprdc: data not for us\n");
    }
  } else {
    PRINTF("lpprdc: failed to parse (%u)\n", packetbuf_totlen());
  }
#if !LPPRDC_CONF_WITH_PROBE_TRAIN
      if(we_are_probing && !probe_timer_is_running) {
PRINTF("5\n");
        process_poll(&probe_process);
      }
#endif
  we_are_processing_input = 0;
}
/*---------------------------------------------------------------------------*/
/* The frame was not acked because it was invalid. */
static void
softack_invalid_frame_callback(const uint8_t *frame, uint8_t framelen)
{
#if LPPRDC_CONF_WITH_PROBE_TRAIN
  uint8_t fcf = frame[0];
  PRINTF("lpprdc: invalid frame callback\n");
  if((fcf & 7) == 1) {
    /* Data packet was invalid */
    if(we_are_probing) {
      process_poll(&probe_process);
    }
  }
#endif
}
/*---------------------------------------------------------------------------*/
/* The frame was acked (i.e. we wanted to ack it AND it was not corrupt).
 * Store the last acked sequence number to avoid repeatedly acking in case
 * we're not duty cycled (e.g. border router) */
static void
softack_acked_callback(const uint8_t *frame, uint8_t framelen)
{
  uint8_t fcf, ack_required, seqno;

  fcf = frame[0];
  ack_required = (fcf >> 5) & 1;
  seqno = frame[2];

  if((fcf & 7) == 1) {
    /* Data packet */
    /* We just acked a data packet. If we're probing, we should continue probing.
       If we heard the packet while sending, we shouldn't do anything. */
    if(we_are_probing) {
#if LPPRDC_CONF_WITH_PROBE_TRAIN
      PRINTF("c\n");
      waiting_for_response = 0;
      process_poll(&probe_process);//, PROCESS_EVENT_CONTINUE, NULL);
#else
      if(current_backoff_window) {
        continue_probing = 1;
      } else {
        /* Set we_are_probing to 0 so the radio layer can handle turning off the radio */
        we_are_probing = 0;
        continue_probing = 0;
      }
      waiting_for_response = 0;
      current_backoff_window = 0;
      process_poll(&probe_process);
#endif
    }
  } else if ((fcf & 7) == 6) {
    /* Broadcast probe */
    /* We just sent a data packet as a response to a probe. We're expecting an
       ack (if unicast), but there's nothing to do until it arrives. */
    packet_was_sent = 1;
  } else if ((fcf & 7) == 7) {
    /* Ack probe */
    /* This occurs if we had data to send and the ack wasn't to us */
    packet_was_sent = 1;
  }
}

/* Called for every incoming frame from interrupt. We check if we want to ack the
 * frame and prepare an ACK if needed */
static void
softack_input_callback(const uint8_t *frame, uint8_t framelen, uint8_t **ackbufptr, uint8_t *acklen)
{
  uint8_t fcf, fcf2, ack_required, dest_addr_mode, seqno, dest_addr_len;

  int i;

  fcf = frame[0];
  fcf2 = frame[1];
  ack_required = (fcf >> 5) & 1;
  dest_addr_mode = (fcf2 >> 2) & 3;
  seqno = frame[2];

  dest_addr_len = dest_addr_mode == FRAME802154_SHORTADDRMODE ? 2:8;

#if LPPRDC_CONF_WITH_PROBE_TRAIN
  activity_seen = 1;
#endif

  if((fcf & 7) == 1) {
    /* Data packet */
    if(ack_required) {
      uint8_t *dest_addr = frame + 2 + 3;
      uint8_t dest_addr_big_endian[dest_addr_len];
      for(i = 0; i < dest_addr_len; i++) {
        dest_addr_big_endian[i] = dest_addr[dest_addr_len - 1 - i];
      }
        PRINTF("a %d\n", we_are_probing);
      //TODO: cheating and deciding it's a broadcast if short addr mode is used for dest
      if(linkaddr_cmp(dest_addr_big_endian, &linkaddr_node_addr) || (dest_addr_len == 2 && we_are_probing)) {
        PRINTF("b\n");
        /* The data packet was unicast to us. Respond with ack probe. */
        uint8_t *sender_addr = frame + 2 + 3 + dest_addr_len; /* 2 for short addr mode */
        uint8_t sender_addr_big_endian[8];
        for(i = 0; i < 8; i++) {
          sender_addr_big_endian[i] = sender_addr[7 - i];
        }
        ackbuf[2] = seqno;
        memcpy(ackbuf + 2 + 3, sender_addr, 8);
#if LPPRDC_CONF_WITH_PROBE_TRAIN
        if(we_are_probing && current_backoff_window > 0) {
          ackbuf[2 + 3 + 8 + 8 + 1] = 1;
        } else {
          ackbuf[2 + 3 + 8 + 8 + 1] = 0;
        }
#else
        if(we_are_probing) {
          ackbuf[2 + 3 + 8 + 8 + 3 + 2] = 1;
        } else {
          ackbuf[2 + 3 + 8 + 8 + 3 + 2] = 0;
        }
#endif
        *ackbufptr = ackbuf;
        *acklen = ACKBUF_SIZE;
        return;
      }
    }
  } else if((fcf & 7) == 6) {
    /* Broadcast probe */
    uint8_t they_are_probing = 0;
#if LPPRDC_CONF_WITH_PROBE_TRAIN
    uint8_t probes_remaining = 0;
    if(framelen >= 2 + 3 + 2 + 8 + 2) {
      probes_remaining = frame[2 + 3 + 2 + 8 + 1];
      if(probes_remaining > prev_probes_remaining) {
        probe_for_sending = random_rand() % probes_remaining + 1;
      }
      PRINTF("rem %u %u %u\n", probes_remaining, prev_probes_remaining, probe_for_sending);
      prev_probes_remaining = probes_remaining;
    } else {
      PRINTF("lpprdc: couldn't get probes remaining\n");
      *acklen = 0;
      return;
    }
    if(probe_for_sending != probes_remaining) {
      if(probes_remaining == 1) {
        probe_for_sending = 1;
      }
      *acklen = 0;
      return;
    }
    they_are_probing = probes_remaining > 0 ? 1 : 0;
#else
    rtimer_clock_t backoff_window = 0;
    if(framelen >= 2 + 3 + 2 + 8 + 2) {
      backoff_window = (frame[2 + 3 + 2 + 8 + 1] << 8) | frame[2 + 3 + 2 + 8 + 1 + 1];
      if(backoff_window) {
        *acklen = 0;
        return;
      }
    } else {
      PRINTF("lpprdc: couldn't get backoff window\n");
      *acklen = 0;
      return;
    }
    
    if(framelen > 18) {
      they_are_probing = frame[2 + 3 + 2 + 8 + 1 + 2];
    } else {
      they_are_probing = 0;
    }
#endif

    /* If we're sending, send a packet as response (if we don't need to back off) */
    if(we_are_sending && they_are_probing) {
      uint8_t *sender_addr = frame + 2 + 3 + 2; /* 2 for short addr mode */
      uint8_t sender_addr_big_endian[8];
      for(i = 0; i < 8; i++) {
        sender_addr_big_endian[i] = sender_addr[7 - i];
      }
      if(!we_are_broadcasting) {
#if RPL_CONF_OPP_ROUTING
        if((we_are_anycasting && rpl_node_in_forwarder_set(sender_addr_big_endian)) ||
           (!we_are_anycasting && linkaddr_cmp(sender_addr_big_endian, &current_receiver_addr))) {
#else
        if(linkaddr_cmp(sender_addr_big_endian, &current_receiver_addr)) {
#endif
          memcpy(sendbuf + 2 + 3, sender_addr, 8);
        } else { 
          *acklen = 0;
          return;
        }
      } else {
        /* We're broadcasting */
#if LPPRDC_CONF_WITH_PROBE_TRAIN
        if(linkaddr_cmp(sender_addr_big_endian, &last_receiver_to_ack)) {
          *acklen = 0;
          return;
        }
        is_receiver_awake = 1;
#endif
      }

      *ackbufptr = sendbuf;
      *acklen = transmit_len;
      return;
    }
  } else if ((fcf & 7) == 7) {
    /* Ack probe */
    /* If we have data to send, we can. But if this ack was to us, it won't be ready. */
    uint8_t *dest_addr = frame + 2 + 3;
    uint8_t dest_addr_big_endian[8];
    uint8_t they_are_probing;
    for(i = 0; i < 8; i++) {
      dest_addr_big_endian[i] = dest_addr[7 - i];
    }
#if LPPRDC_CONF_WITH_PROBE_TRAIN
    uint8_t probes_remaining = 0;
    if(framelen >= 2 + 3 + 8 + 8 + 2) {
      probes_remaining = frame[2 + 3 + 8 + 8 + 1]; 
      /*if(probes_remaining > prev_probes_remaining) {
        probe_for_sending = random_rand() % probes_remaining;
      }*/
      /* Always reset backoff on an ack probe */
      probe_for_sending = 1;
      prev_probes_remaining = 1;
    } else {
      PRINTF("lpprdc: couldn't get probes remaining\n");
      *acklen = 0;
      return;
    }
    /* If this ack was to a pushed unicast packet, the node might not be probing */
    /*if(probes_remaining == 0) {
      PRINTF("e\n");
      *acklen = 0;
      return;
    }*/
    they_are_probing = probes_remaining > 0 ? 1 : 0;
#else
    if(framelen > 26) {
      they_are_probing = frame[2 + 3 + 8 + 8 + 3 + 2];
    } else {
      they_are_probing = 0;
    }
#endif

    uint8_t *sender_addr = frame + 2 + 3 + 8;
    uint8_t sender_addr_big_endian[8];
    for(i = 0; i < 8; i++) {
      sender_addr_big_endian[i] = sender_addr[7 - i];
    }
    if(linkaddr_cmp(dest_addr_big_endian, &linkaddr_node_addr) && seqno == current_seqno) {
      if(they_are_probing) {
        current_backoff_window = 0;
#if LPPRDC_CONF_WITH_PROBE_TRAIN
        if(we_are_broadcasting) {
          is_receiver_awake = 0;
          process_post(&send_process, PROCESS_EVENT_CONTINUE, NULL);
        }
        linkaddr_copy(&last_receiver_to_ack, sender_addr_big_endian);
#else
        is_receiver_awake = 1;
#endif
      }

      packet_was_acked = 1;
      if(!we_are_broadcasting) {
        process_poll(&send_process);
      }
    } else if(we_are_sending && they_are_probing) {
      /* If this wasn't an ack to us and we have data to send, we can do that */
      if(we_are_broadcasting) {
#if LPPRDC_CONF_WITH_PROBE_TRAIN
        if(linkaddr_cmp(sender_addr_big_endian, &last_receiver_to_ack)) {
          PRINTF("d\n");
          *acklen = 0;
          return;
        }
        is_receiver_awake = 1;
#else
        current_backoff_window = 0;
#endif
      } else {
        uint8_t *sender_addr = frame + 2 + 3 + 8;
        uint8_t sender_addr_big_endian[8];
        for(i = 0; i < 8; i++) {
          sender_addr_big_endian[i] = sender_addr[7 - i];
        }
#if RPL_CONF_OPP_ROUTING
        if((we_are_anycasting && rpl_node_in_forwarder_set(sender_addr_big_endian)) ||
           (!we_are_anycasting && linkaddr_cmp(sender_addr_big_endian, &current_receiver_addr))) {
#else
        if(linkaddr_cmp(sender_addr_big_endian, &current_receiver_addr)) {
#endif
          current_backoff_window = 0;
          memcpy(sendbuf + 2 + 3, sender_addr, 8);
        } else { 
          *acklen = 0;
          return;
        }
      }
      *ackbufptr = sendbuf;
      *acklen = transmit_len;

      return;
    }
  }
  *acklen = 0;
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
  radio_rx_mode &= ~RADIO_RX_MODE_ADDRESS_FILTER;
  radio_rx_mode &= ~RADIO_RX_MODE_AUTOACK;
  if(NETSTACK_RADIO.set_value(RADIO_PARAM_RX_MODE, radio_rx_mode) != RADIO_RESULT_OK) {
    PRINTF("lpprdc: failed to set radio params, aborting\n");
    return;
  }

  cc2420_softack_subscribe(softack_input_callback, softack_acked_callback, softack_invalid_frame_callback);

  struct probe_packet probe;
#if LPPRDC_CONF_WITH_PROBE_TRAIN
  probe.probes_remaining = 0;
#else
  probe.backoff_window = 0;
  probe.probing = 1;
#endif

  packetbuf_copyfrom(&probe, sizeof(struct probe_packet));
  packetbuf_set_addr(PACKETBUF_ADDR_RECEIVER, &linkaddr_null);
  packetbuf_set_addr(PACKETBUF_ADDR_SENDER, &linkaddr_node_addr);
  packetbuf_set_attr(PACKETBUF_ATTR_FRAME_TYPE, PACKETBUF_ATTR_PACKET_TYPE_PROBE);
  if(NETSTACK_FRAMER.create() < 0) {
    PRINTDEBUG("lpprdc: Failed to create probe packet.\n");
  }
  PRINTF("lpprdc: probe size %d", packetbuf_totlen());
  packetbuf_copyto(probebuf);

  linkaddr_t addr;
  addr.u8[0] = 1;
  addr.u8[1] = 2;
  packetbuf_copyfrom(&probe, sizeof(struct probe_packet));
  packetbuf_set_addr(PACKETBUF_ADDR_RECEIVER, &addr);
  packetbuf_set_addr(PACKETBUF_ADDR_SENDER, &linkaddr_node_addr);
  packetbuf_set_attr(PACKETBUF_ATTR_FRAME_TYPE, PACKETBUF_ATTR_PACKET_TYPE_ACK_PROBE);
  if(NETSTACK_FRAMER.create() < 0) {
    PRINTDEBUG("lpprdc: Failed to create ack probe packet.\n");
  }
  PRINTF("lpprdc: ack probe size %d", packetbuf_totlen());
  packetbuf_copyto(ackbuf);

  probe_rate = NETSTACK_RDC_CHANNEL_CHECK_RATE;
  probe_interval = CLOCK_SECOND / probe_rate;
  ctimer_max_probe_interval = probe_interval + probe_interval / NETSTACK_RDC_CHANNEL_CHECK_RANDOMNESS_FRACTION;
  max_probe_interval = RTIMER_ARCH_SECOND / CLOCK_SECOND * ctimer_max_probe_interval;
  listen_for_probe_timeout = 3*ctimer_max_probe_interval;

  process_start(&probe_process, NULL);

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
    return NETSTACK_RADIO.on();
  } else {
    return NETSTACK_RADIO.off();
  }
}
/*---------------------------------------------------------------------------*/
static unsigned short
channel_check_interval(void)
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
  channel_check_interval,
};
/*---------------------------------------------------------------------------*/
uint16_t
lpprdc_debug_print(void)
{
  return 0;
}
/*---------------------------------------------------------------------------*/
