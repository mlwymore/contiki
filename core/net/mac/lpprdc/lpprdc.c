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
#include "net/netstack.h"
#include "net/rime/rime.h"
#include "sys/compower.h"
#include "sys/pt.h"
#include "sys/rtimer.h"


#include <string.h>

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
//#define CCA_SLEEP_TIME                     RTIMER_ARCH_SECOND / 4000
#define CCA_SLEEP_TIME                     RTIMER_ARCH_SECOND / 4000 + CCA_CHECK_TIME
//#define CCA_SLEEP_TIME                     RTIMER_ARCH_SECOND / 3225 - 2*CCA_CHECK_TIME
//#define CCA_SLEEP_TIME                     CCA_CHECK_TIME
#else
#define CCA_SLEEP_TIME                     (RTIMER_ARCH_SECOND / 2000) + 1 + CCA_CHECK_TIME

#endif /* RTIMER_ARCH_SECOND > 8000 */
#endif /* CONTIKIMAC_CONF_CCA_SLEEP_TIME */


#define NETSTACK_RDC_CHANNEL_CHECK_RANDOMNESS_FRACTION 2


/* AFTER_ACK_DETECTED_WAIT_TIME is the time to wait after a potential
   ACK packet has been detected until we can read it out from the
   radio. */
#ifdef CONTIKIMAC_CONF_AFTER_ACK_DETECTED_WAIT_TIME
#define AFTER_ACK_DETECTED_WAIT_TIME      CONTIKIMAC_CONF_AFTER_ACK_DETECTED_WAIT_TIME
#else
#define AFTER_ACK_DETECTED_WAIT_TIME      RTIMER_ARCH_SECOND / 1500
#endif

#define AFTER_PROBE_SENT_WAIT_TIME        RTIMER_ARCH_SECOND / 1000
//#define AFTER_PROBE_SENT_WAIT_TIME        RTIMER_ARCH_SECOND / 500

#define INTER_PACKET_INTERVAL             RTIMER_ARCH_SECOND / 1100
#define PROBE_TRAIN_PERIOD                RTIMER_ARCH_SECOND / 400

#define PROBE_CHANNEL_CHECK_TIME          PROBE_TRAIN_PERIOD
#define DATA_CHANNEL_CHECK_TIME           PROBE_CHANNEL_CHECK_TIME

#define INITIAL_NUM_PROBES                    2

#define MIN_BACKOFF_WINDOW 2
#define MAX_BACKOFF_WINDOW 32

#define CTIMER_TIMEOUT 2
#define ACK_LISTEN_TIMEOUT (RTIMER_ARCH_SECOND / 1300)

#include <stdio.h>
static struct ctimer probe_timer;

#if RPL_CONF_OPP_ROUTING
#include "net/rpl/rpl.h"
static uint8_t we_are_anycasting;
#endif
static linkaddr_t current_receiver_addr;


static linkaddr_t last_receiver_to_ack;
static volatile uint8_t probe_for_sending = 0;
static volatile uint8_t prev_probe_id = 0;

static volatile uint8_t current_seqno = 0;

static struct rdc_buf_list *curr_packet_list;
static mac_callback_t curr_callback;
static void *curr_ptr;

static volatile uint8_t contikimac_is_on = 0;
static volatile uint8_t contikimac_keep_radio_on = 0;

static volatile unsigned char we_are_sending = 0;
static volatile unsigned char we_are_broadcasting = 0;
static volatile unsigned char we_are_probing = 0;
static volatile unsigned char is_receiver_awake = 0;
static volatile unsigned char waiting_for_response = 0;
static volatile unsigned char activity_seen = 0;
static volatile uint8_t receiving_packet = 0;
static volatile unsigned char packet_was_sent = 0;
static volatile unsigned char packet_was_acked = 0;
static volatile unsigned char done_broadcasting = 0;
static volatile unsigned char strobe_ccas = 0;

static volatile uint16_t probe_rate = 0;
static volatile uint16_t probe_interval = 0;
static volatile uint16_t max_probe_interval = 0;
static volatile uint16_t ctimer_max_probe_interval = 0;
static volatile uint16_t listen_for_probe_timeout = 0;
static volatile rtimer_clock_t current_backoff_window = 0;

static volatile uint8_t num_probes_remaining = 0;

#define DEST_ADDR_OFFSET (2 + 3)
#if NETSTACK_CONF_WITH_IPV6
#define ACKBUF_SIZE 23
#define PROBEBUF_SIZE 17
#define SENDER_ADDR_LEN 8
#define DEST_ADDR_LEN 8
#define BCAST_DEST_ADDR_LEN 2
#else
#define ACKBUF_SIZE 11
#define PROBEBUF_SIZE 11
#define SENDER_ADDR_LEN 2
#define DEST_ADDR_LEN 2
#define BCAST_DEST_ADDR_LEN 2
#endif
#define PROBE_ID_POSITION (DEST_ADDR_OFFSET + BCAST_DEST_ADDR_LEN + SENDER_ADDR_LEN)
#define ACK_ID_POSITION (DEST_ADDR_OFFSET + DEST_ADDR_LEN + SENDER_ADDR_LEN)

#ifdef LPPRDC_CONF_INITIAL_PROBE_SIZE
#define INITPROBEBUF_SIZE LPPRDC_CONF_INITIAL_PROBE_SIZE
#else
#define INITPROBEBUF_SIZE 125
#endif

#define CCA_SLEEP_TIME_LONG                 (RTIMER_ARCH_SECOND / 275 * INITPROBEBUF_SIZE / 125)

#define PROBE_RECEIVE_DURATION            ((uint32_t)PROBEBUF_SIZE * RTIMER_ARCH_SECOND * 8 / 250000)
#define ACK_RECEIVE_DURATION              ((uint32_t)ACKBUF_SIZE * RTIMER_ARCH_SECOND * 8 / 250000)
#define PACKET_RECEIVE_DURATION           RTIMER_ARCH_SECOND / 250
#define INIT_PROBE_RECEIVE_DURATION       ((uint32_t)INITPROBEBUF_SIZE * RTIMER_ARCH_SECOND * 8 / 250000)
/*#define PROBE_RECEIVE_DURATION            RTIMER_ARCH_SECOND / 900
#define ACK_RECEIVE_DURATION              RTIMER_ARCH_SECOND / 512
#define PACKET_RECEIVE_DURATION           RTIMER_ARCH_SECOND / 250*/

#ifdef LPPRDC_CONF_RESET_BACKOFF_ON_PROBE
#define LPPRDC_RESET_BACKOFF_ON_PROBE LPPRDC_CONF_RESET_BACKOFF_ON_PROBE
#else
#define LPPRDC_RESET_BACKOFF_ON_PROBE 0
#endif

static int transmit_len = 0;
static uint8_t sendbuf[PACKETBUF_SIZE];
static uint8_t ackbuf[ACKBUF_SIZE];
static uint8_t probebuf[PROBEBUF_SIZE];
static uint8_t initprobebuf[INITPROBEBUF_SIZE];


PROCESS(lpprdc_probe_process, "probe process");
PROCESS(lpprdc_send_process, "send process");
PROCESS(lpprdc_cca_train_process, "cca train process");

struct probe_packet {
  uint8_t probe_id;
};
struct initprobe_packet {
  uint8_t probe_id;
  uint8_t filler[INITPROBEBUF_SIZE - PROBEBUF_SIZE];
};


#define DEBUG 0
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
    NETSTACK_RADIO.get_value(RADIO_PARAM_POWER_MODE, &radio_is_on);
    if(was_on && !radio_is_on) {
      compower_accumulate(&compower_idle_activity);
    }
#endif /* CONTIKIMAC_CONF_COMPOWER */
  } else {
    //PRINTF("lpprdc: not turning radio off %d %d\n", we_are_sending, NETSTACK_RADIO.receiving_packet());
    PRINTF("k\n");
  }
}
/*---------------------------------------------------------------------------*/
static void
powercycle_turn_radio_on(void)
{
  if(we_are_sending == 0) {
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
    //on();
    if(!NETSTACK_RADIO.channel_clear()) {
      if(with_restart) {
        start = RTIMER_NOW();
      } else {
        //off();
        return 0;
      }
    }
    if(!RTIMER_CLOCK_LT(RTIMER_NOW(), start + time_clear)) {
      break;
    } else {
      //off();
      watchdog_periodic();
      wt = RTIMER_NOW();
      while(RTIMER_CLOCK_LT(RTIMER_NOW(), wt + CCA_SLEEP_TIME)) { }
    }
  }
  return 1;
}
/*---------------------------------------------------------------------------*/
static void
powercycle_ctimer_wrapper(void *ptr)
{
  process_post(&lpprdc_probe_process, PROCESS_EVENT_CONTINUE, NULL);
}
/*---------------------------------------------------------------------------*/
/*
 * Send probes and listen for a response.
*/
PROCESS_THREAD(lpprdc_probe_process, ev, data)
{
  PROCESS_BEGIN();

  static rtimer_clock_t wt = 0;
  static uint8_t send_probe = 0;
  static uint8_t send_initprobe = 0;
  static rtimer_clock_t extra_wait = 0;

  PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_CONTINUE);

  while(1) {
    uint8_t collisions = 0;
  	uint8_t packets = 0;
  	
    if(!we_are_sending) {
      //Prep the initial probe. Backoff value is number of probes remaining in train. Starts with 0.
      
      if(channel_is_clear(PROBE_CHANNEL_CHECK_TIME, 0)) {
        send_initprobe = 1;
        send_probe = 1;
        we_are_probing = 1;
        current_backoff_window = 0;
        num_probes_remaining = INITIAL_NUM_PROBES; //2
        while(num_probes_remaining-- > 0) {
          if(send_probe && num_probes_remaining > -1) {
            probebuf[PROBE_ID_POSITION] = num_probes_remaining + 1;
            if((send_initprobe && NETSTACK_RADIO.prepare(initprobebuf, INITPROBEBUF_SIZE)) || 
                (!send_initprobe && NETSTACK_RADIO.prepare(probebuf, PROBEBUF_SIZE))) {
              we_are_probing = 0;
              PRINTF("lpprdc: radio locked while probing\n");
              break;
            } else {
              powercycle_turn_radio_on();
              if((send_initprobe && NETSTACK_RADIO.transmit(INITPROBEBUF_SIZE) != RADIO_TX_OK) ||
                  (!send_initprobe && NETSTACK_RADIO.transmit(PROBEBUF_SIZE) != RADIO_TX_OK)) {
                we_are_probing = 0;
                PRINTF("lpprdc: collision while transmitting probe.\n");
                break;
              }
            }
          } else if(num_probes_remaining < 0) {
            break;
          }
          if(send_initprobe) {
          	send_initprobe = 0;
          	continue;
          }
          /* Probe was sent - wait for softack_input_callback and listen for collision */
          activity_seen = 0;
          waiting_for_response = 1;
          /* Need to wait a little extra time if we didn't just send a probe (i.e. the send was handled by softack code) */
          extra_wait = send_probe == 1 ? 0 : ACK_RECEIVE_DURATION + INTER_PACKET_INTERVAL;
          uint8_t channel_clear = 0;
          receiving_packet = 0;
          wt = RTIMER_NOW();
          
          while(we_are_probing && waiting_for_response && RTIMER_CLOCK_LT(RTIMER_NOW(), 
                    wt + INTER_PACKET_INTERVAL + PACKET_RECEIVE_DURATION + extra_wait)) {
            if(RTIMER_CLOCK_DIFF(RTIMER_NOW(), wt) > AFTER_PROBE_SENT_WAIT_TIME + extra_wait) {
          		channel_clear = NETSTACK_RADIO.channel_clear();
          		activity_seen = channel_clear ? activity_seen : 1;
          		if(channel_clear) {
          			break;
          		}
        		}
    			}

          if(we_are_probing && !waiting_for_response) {
            /* we sent an ack */
            packets++;
            send_probe = 0;
#if LPPRDC_RESET_BACKOFF_ON_ACK
            if(current_backoff_window > 0) {
              num_probes_remaining = 1;
              current_backoff_window = 1;
            } else {
              num_probes_remaining = 0;
              //wt = RTIMER_NOW();
              //while(RTIMER_CLOCK_LT(RTIMER_NOW(), wt + ACK_RECEIVE_DURATION)) { }
            }
#endif
          } else if(we_are_probing && activity_seen) {
            /* collision detected - if probes remaining, wait for the channel to clear */
            if(current_backoff_window < MAX_BACKOFF_WINDOW) {
              if(current_backoff_window == 0) {
                current_backoff_window = MIN_BACKOFF_WINDOW;
              } else {
                current_backoff_window = MIN(current_backoff_window * 2, MAX_BACKOFF_WINDOW);
              }
              num_probes_remaining = current_backoff_window;
            }

            if(num_probes_remaining > 0) {
            	watchdog_periodic();
              while(!NETSTACK_RADIO.channel_clear()) { }
              send_probe = 1;
            }
            
            collisions++;
            //NETSTACK_DDC.collision();
          } else if(we_are_probing) {
            send_probe = 1;
          } else {
          	break;
        	}
        }
      }
      off();
      //powercycle_turn_radio_off();
    }
    current_backoff_window = 0;
    we_are_probing = 0;
    if(NETSTACK_DDC.use_ddc()) {
    	if(collisions) {
    		NETSTACK_DDC.collision();
    	}
    	if(!packets) {
    		NETSTACK_DDC.empty_wakeup();
    	}
    	probe_interval = CLOCK_SECOND * NETSTACK_DDC.multiplier() / NETSTACK_DDC.divisor();
    }
    int32_t rand = random_rand() % (probe_interval / NETSTACK_RDC_CHANNEL_CHECK_RANDOMNESS_FRACTION);
    rand -= (probe_interval / NETSTACK_RDC_CHANNEL_CHECK_RANDOMNESS_FRACTION) / 2;
    uint32_t probe_interval_rand = (uint32_t)probe_interval + rand;
    ctimer_set(&probe_timer, probe_interval_rand, powercycle_ctimer_wrapper, NULL);
    PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_CONTINUE);
  }

  PROCESS_END();
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
/* Timer callback triggered on timeout when listening for probe */
static void
listening_off(void *ptr)
{
  process_post(&lpprdc_send_process, PROCESS_EVENT_POLL, NULL);
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
static clock_time_t send_timeout_time;
PROCESS_THREAD(lpprdc_cca_train_process, ev, data)
{
  PROCESS_BEGIN();
  static rtimer_clock_t wt;
  PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_CONTINUE);
  PRINTF("c\n");
  off();
  strobe_ccas = 1;
  while(strobe_ccas) {
    if(!NETSTACK_RADIO.channel_clear()) {
        on();
        watchdog_periodic();
        wt = RTIMER_NOW();
        while(strobe_ccas && RTIMER_CLOCK_LT(RTIMER_NOW(), wt + INIT_PROBE_RECEIVE_DURATION + INTER_PACKET_INTERVAL + PROBE_RECEIVE_DURATION + AFTER_ACK_DETECTED_WAIT_TIME)) { }
        if(!strobe_ccas) {
        	break;
      	}
      	off();
    } else if(clock_time() > send_timeout_time) {// ctimer_expired(&send_timer)) {
    	break;
    }
    watchdog_periodic();
    wt = RTIMER_NOW();
    while(strobe_ccas && RTIMER_CLOCK_LT(RTIMER_NOW(), wt + CCA_SLEEP_TIME_LONG)) { }
  }
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(lpprdc_send_process, ev, data)
{
  PROCESS_BEGIN();

  static uint8_t retry = 0;
  static int ret = 0;
  static uint8_t contikimac_was_on = 1;
  static int packet_pushed = 0;
  static clock_time_t broadcast_start_time = 0;
  static struct ctimer send_timer;

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

    if(!retry) {
    /* Send the packet once if channel clear - similar to RI-MAC's 
       "beacon on request" but skipping the beacons */
      packet_pushed = 0;
      
      if(we_are_broadcasting) {
        /* If it's a broadcast, we won't push a packet */
        packet_pushed++;

        broadcast_start_time = clock_time();
        printf("lpprdc: send broadcast\n");

        if(broadcast_rate_drop()) {
          mac_call_sent_callback(curr_callback, curr_ptr, MAC_TX_COLLISION, 1);
          break;
        }
      } else {
#if NETSTACK_CONF_WITH_IPV6
        printf("lpprdc: send unicast to %02x%02x:%02x%02x:%02x%02x:%02x%02x\n",
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[0],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[1],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[2],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[3],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[4],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[5],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[6],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[7]);
#else /* NETSTACK_CONF_WITH_IPV6 */
    		printf("lpprdc: send unicast to %u.%u\n",
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[0],
               packetbuf_addr(PACKETBUF_ADDR_RECEIVER)->u8[1]);
#endif /* NETSTACK_CONF_WITH_IPV6 */
      } //broadcast or unicast
    } //!retry

  /* Set contikimac_is_on to one to allow the on() and off() functions
     to control the radio. We restore the old value of
     contikimac_is_on when we are done. */
    contikimac_was_on = contikimac_is_on;
    contikimac_is_on = 1;
  
    linkaddr_copy(&last_receiver_to_ack, &linkaddr_null);
    probe_for_sending = INITIAL_NUM_PROBES;
    prev_probe_id = 0;
    do {
      packet_was_sent = 0;
      packet_was_acked = 0;
      if(packet_pushed && !is_receiver_awake) {
        /* We need to wait for a probe before doing anything. */
        process_start(&lpprdc_cca_train_process, NULL);
        process_post(&lpprdc_cca_train_process, PROCESS_EVENT_CONTINUE, NULL);
        //on();
      
        if(we_are_broadcasting) {
          done_broadcasting = 0;
          ctimer_set(&send_timer, ctimer_max_probe_interval, listening_off, NULL);
          send_timeout_time = clock_time() + ctimer_max_probe_interval;
        } else if(retry && current_backoff_window > 0) {
          ctimer_set(&send_timer, (uint32_t)CLOCK_SECOND * current_backoff_window / RTIMER_ARCH_SECOND, listening_off, NULL);
          //send_timeout_time = clock_time() + (uint32_t)CLOCK_SECOND * current_backoff_window / RTIMER_ARCH_SECOND;
        } else {
          ctimer_set(&send_timer, listen_for_probe_timeout, listening_off, NULL);
          send_timeout_time = clock_time() + listen_for_probe_timeout;
        }
        /* Yield until a beacon is heard (input) or timeout */
        PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_POLL);
        ctimer_stop(&send_timer);
      }

      if(!we_are_broadcasting && packet_was_sent && packet_was_acked) {
        break;
      }
      PRINTF("timeout %d\n", is_receiver_awake);

      /* If we timed out listening for a probe, call it a noack (for unicast) or done (for broadcast) */
      if(!is_receiver_awake && packet_pushed) {
        //queuebuf_to_packetbuf(curr_packet_list->buf);
        //process_exit(&lpprdc_cca_train_process);
        strobe_ccas = 0;
        retry = 0;
        break;
      } else if(we_are_broadcasting) {
        /* This is for if our broadcast has timed out, and we're just waiting for the current receiver to ack */
        done_broadcasting = 1;
        PROCESS_WAIT_UNTIL(ev == PROCESS_EVENT_CONTINUE);
        retry = 0;
        break;
      }

      /* If we got a backoff beacon, we back off. */
      if(current_backoff_window > 0) {
        is_receiver_awake = 0;
        packet_was_sent = 0;
        packet_was_acked = 0;

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
        break;
      }
      
      on();

      ret = NETSTACK_RADIO.transmit(transmit_len);
      packet_was_sent = 1;
      is_receiver_awake = 0;

      if(we_are_broadcasting) {
        packet_pushed++;
        continue;
      }

      if(ret == RADIO_TX_OK) {
        //ctimer_set(&ct, CTIMER_TIMEOUT, listening_off, NULL);
        /* Wait for the ACK probe packet */
        static rtimer_clock_t wt;
        packet_was_acked = 0;
        watchdog_periodic();
        wt = RTIMER_NOW();
        while(RTIMER_CLOCK_LT(RTIMER_NOW(), wt + ACK_LISTEN_TIMEOUT)) { }
        //PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_POLL);
        //ctimer_stop(&ct);
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
            off();
          }
        }
      } else if(packet_pushed) {
        /* Unicast and radio didn't return OK... radio layer collision */
        retry = 0;
        packet_pushed = -1;
        current_backoff_window = 0;
      }
    } while (!packet_pushed++ || (we_are_broadcasting && clock_time() < broadcast_start_time + ctimer_max_probe_interval));

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
      //process_exit(&lpprdc_cca_train_process);
      strobe_ccas = 0;
      off();
      PRINTF("broadcast done\n");
      queuebuf_to_packetbuf(curr_packet_list->buf);
      mac_call_sent_callback(curr_callback, curr_ptr, MAC_TX_OK, 1);
      if(!load_next_packet(1)) {
        break;
      }
    } else if(packet_was_acked) {
    	off();
      PRINTF("!\n");
      //process_exit(&lpprdc_cca_train_process);
      strobe_ccas = 0;
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

  is_receiver_awake = 0;
  we_are_sending = 0;
  retry = 0;

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

    /* create and secure this frame */
    if(!packetbuf_attr(PACKETBUF_ATTR_IS_CREATED_AND_SECURED)) {
		  if(next != NULL) {
		    packetbuf_set_attr(PACKETBUF_ATTR_PENDING, 1);
		  }
	#if !NETSTACK_CONF_BRIDGE_MODE
		  /* If NETSTACK_CONF_BRIDGE_MODE is set, assume PACKETBUF_ADDR_SENDER is already set. */
		  packetbuf_set_addr(PACKETBUF_ADDR_SENDER, &linkaddr_node_addr);
	#endif

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

  curr_packet_list = buf_list;
  curr_callback = sent;
  curr_ptr = ptr;

  process_start(&lpprdc_send_process, NULL);
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
static void
input_packet(void)
{
  static int is_broadcast = 0;
  
  if(!we_are_probing && !we_are_sending) {
    off();
  }

  if(packetbuf_totlen() > 0 && NETSTACK_FRAMER.parse() >= 0) {
    is_broadcast = packetbuf_holds_broadcast();
    if(packetbuf_attr(PACKETBUF_ATTR_FRAME_TYPE) == PACKETBUF_ATTR_PACKET_TYPE_PROBE) {
      if(we_are_probing && is_broadcast) {
        PRINTF("lpprdc: failed to lock out channel while probing\n");
        we_are_probing = 0;
      }
      return;
    } else if (packetbuf_attr(PACKETBUF_ATTR_FRAME_TYPE) == PACKETBUF_ATTR_PACKET_TYPE_ACK_PROBE) {
      return;
    }

    if(packetbuf_datalen() > 0 &&
       packetbuf_totlen() > 0 &&
       (linkaddr_cmp(packetbuf_addr(PACKETBUF_ADDR_RECEIVER),
                     &linkaddr_node_addr) ||
        is_broadcast)) {
      /* This is a regular packet that is destined to us or to the
         broadcast address. */

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

  		PRINTF("in\n");
#if RDC_WITH_DUPLICATE_DETECTION
		  /* Check for duplicate packet. */
		  uint8_t duplicate = mac_sequence_is_duplicate();
		  if(duplicate) {
		    /* Drop the packet. */
		    PRINTF("dupe\n");
		  } else {
		    mac_sequence_register_seqno();
		    NETSTACK_DDC.input();
		  	NETSTACK_MAC.input();
		  }
#else
			NETSTACK_DDC.input();
			NETSTACK_MAC.input();
#endif /* RDC_WITH_DUPLICATE_DETECTION */

      return;
    } else {
      //PRINTDEBUG("lpprdc: data not for us\n");
    }
  } else {
    PRINTF("lpprdc: failed to parse (%u)\n", packetbuf_totlen());
  }
}
/*---------------------------------------------------------------------------*/
#define DATA_PACKET_TYPE 1
#define PROBE_PACKET_TYPE 6
#define ACK_PACKET_TYPE 7

#define FCF_POSITION 0
#define SEQNO_POSITION 2
/*---------------------------------------------------------------------------*/
/* The frame was not acked because it was invalid. */
static void
softack_invalid_frame_callback(const uint8_t *frame, uint8_t framelen)
{
  uint8_t fcf = frame[FCF_POSITION];
  PRINTF("lpprdc: invalid frame callback\n");
  if((fcf & 7) == DATA_PACKET_TYPE) {
    /* Data packet was invalid */
    if(we_are_probing) {
      //process_post(&lpprdc_probe_process, PROCESS_EVENT_POLL, NULL);
    }
  }
}
/*---------------------------------------------------------------------------*/
/* The frame was acked (i.e. we wanted to ack it AND it was not corrupt).
 * Store the last acked sequence number to avoid repeatedly acking in case
 * we're not duty cycled (e.g. border router) */
static void
softack_acked_callback(const uint8_t *frame, uint8_t framelen)
{
  uint8_t fcf;

  fcf = frame[FCF_POSITION];

  if((fcf & 7) == DATA_PACKET_TYPE) {
    /* Data packet */
    /* We just acked a data packet. If we're probing, we should continue probing.
       If we heard the packet while sending, we shouldn't do anything. */
    if(we_are_probing) {
    	if(current_backoff_window == 0) {
    		off();
    	}
      waiting_for_response = 0;
      //process_post(&lpprdc_probe_process, PROCESS_EVENT_POLL, NULL);
    }
  } else if ((fcf & 7) == PROBE_PACKET_TYPE) {
    /* Broadcast probe */
    /* We just sent a data packet as a response to a probe. We're expecting an
       ack (if unicast), but there's nothing to do until it arrives. */
    packet_was_sent = 1;
    //process_exit(&lpprdc_cca_train_process);
    strobe_ccas = 0;
  } else if ((fcf & 7) == ACK_PACKET_TYPE) {
    /* Ack probe */
    /* This occurs if we had data to send and the ack wasn't to us */
    packet_was_sent = 1;
    //process_exit(&lpprdc_cca_train_process);
    strobe_ccas = 0;
  }
}
/*---------------------------------------------------------------------------*/
static void
probe_timeout(void *ptr)
{
	if(we_are_sending && !(clock_time() > send_timeout_time)) {
		if(!process_is_running(&lpprdc_cca_train_process)) {
			PRINTF("lpprdc: Restarting CCA train\n");
			process_start(&lpprdc_cca_train_process, NULL);
		  process_post(&lpprdc_cca_train_process, PROCESS_EVENT_CONTINUE, NULL);
	  } else {
	  	strobe_ccas = 1;
  	}
	}
}
/*---------------------------------------------------------------------------*/
/* Called for every incoming frame from interrupt. We check if we want to ack the
 * frame and prepare an ACK if needed. Return 1 if frame should be passed up the
 	 stack. */
static uint8_t
softack_input_callback(const uint8_t *frame, uint8_t framelen, uint8_t **ackbufptr, uint8_t *acklen)
{
  uint8_t fcf, fcf2, dest_addr_mode, seqno, dest_addr_len;

  int i;
  
  static struct ctimer ct;
  
  //strobe_ccas = 0;

  fcf = frame[FCF_POSITION];
  fcf2 = frame[FCF_POSITION + 1];
  dest_addr_mode = (fcf2 >> 2) & 3;
  seqno = frame[SEQNO_POSITION];

  dest_addr_len = dest_addr_mode == FRAME802154_SHORTADDRMODE ? 2:8;

  activity_seen = 1;

  if((fcf & 7) == DATA_PACKET_TYPE) {
    receiving_packet = 0;
    /* Data packet */
  	//PRINTF("p\n");
    const uint8_t *dest_addr = frame + DEST_ADDR_OFFSET;
    uint8_t dest_addr_big_endian[dest_addr_len];
    for(i = 0; i < dest_addr_len; i++) {
      dest_addr_big_endian[i] = dest_addr[dest_addr_len - 1 - i];
    }
    //TODO: cheating and deciding it's a broadcast if short addr mode is used for dest
    if(linkaddr_cmp((linkaddr_t *)dest_addr_big_endian, &linkaddr_node_addr) || (dest_addr_len == 2 && we_are_probing)) {
      /* The data packet was unicast to us. Respond with ack probe. */
      const uint8_t *sender_addr = frame + DEST_ADDR_OFFSET + dest_addr_len; /* 2 for short addr mode */
      /*uint8_t sender_addr_big_endian[SENDER_ADDR_LEN];
      for(i = 0; i < SENDER_ADDR_LEN; i++) {
        sender_addr_big_endian[i] = sender_addr[SENDER_ADDR_LEN - 1 - i];
      }*/
      ackbuf[SEQNO_POSITION] = seqno;
      memcpy(ackbuf + DEST_ADDR_OFFSET, sender_addr, SENDER_ADDR_LEN);
      if(we_are_probing && current_backoff_window > 0) {
#if LPPRDC_RESET_BACKOFF_ON_ACK
        ackbuf[ACK_ID_POSITION] = 1;
#else
				ackbuf[ACK_ID_POSITION] = num_probes_remaining;
#endif
      } else {
        ackbuf[ACK_ID_POSITION] = 0;
      }

      *ackbufptr = ackbuf;
      *acklen = ACKBUF_SIZE;
      return 1;
    }
  } else if((fcf & 7) == PROBE_PACKET_TYPE) {
    /* Broadcast probe */
    if(framelen > PROBEBUF_SIZE) {
    	*acklen = 0;
    	return 0;
    }
    if(we_are_probing) {
      *acklen = 0;
      return 0;
    } else if(we_are_sending) {
    	ctimer_stop(&ct);
  	  ctimer_set(&ct, CTIMER_TIMEOUT, probe_timeout, NULL);
    }
    uint8_t they_are_probing = 0;

    uint8_t probe_id = 0;
    if(framelen >= PROBE_ID_POSITION + 1) {
      probe_id = frame[PROBE_ID_POSITION];
      unsigned short rand = 0;
      if(probe_id > prev_probe_id) {
      	current_backoff_window = probe_id;
      	rand = random_rand();
        probe_for_sending = rand / (RANDOM_RAND_MAX / probe_id) + 1;
      } else if (probe_id == 1) {
      	probe_for_sending = 1;
      }
      //printf("%d %d %d %u\n", probe_id, prev_probe_id, probe_for_sending, rand);
      prev_probe_id = probe_id;
    } else {
      PRINTF("lpprdc: couldn't get probe id\n");
      *acklen = 0;
      return 0;
    }
    if(probe_for_sending != probe_id) {
      /*if(probe_id == 1) {
        probe_for_sending = 1;
      }*/
      *acklen = 0;
      return 0;
    }
    they_are_probing = probe_id > 0 ? 1 : 0;

    /* If we're sending, send a packet as response (if we don't need to back off) */
    if(we_are_sending && they_are_probing) {
      const uint8_t *sender_addr = frame + DEST_ADDR_OFFSET + BCAST_DEST_ADDR_LEN;
      uint8_t sender_addr_big_endian[SENDER_ADDR_LEN];
      for(i = 0; i < SENDER_ADDR_LEN; i++) {
        sender_addr_big_endian[i] = sender_addr[SENDER_ADDR_LEN - 1 - i];
      }
      if(!we_are_broadcasting) {
#if RPL_CONF_OPP_ROUTING
        if((we_are_anycasting && rpl_node_in_forwarder_set(sender_addr_big_endian)) ||
           (!we_are_anycasting && linkaddr_cmp((linkaddr_t *)sender_addr_big_endian, &current_receiver_addr))) {
#else
        if(linkaddr_cmp((linkaddr_t *)sender_addr_big_endian, &current_receiver_addr)) {
#endif
          memcpy(sendbuf + DEST_ADDR_OFFSET, sender_addr, SENDER_ADDR_LEN);
        } else {
          if(process_is_running(&lpprdc_cca_train_process)) {
          	strobe_ccas = 1;
            //process_post(&lpprdc_cca_train_process, PROCESS_EVENT_CONTINUE, NULL);
          }
          prev_probe_id = 0;
          *acklen = 0;
          return 0;
        }
      } else {
        /* We're broadcasting */
        if(linkaddr_cmp((linkaddr_t *)sender_addr_big_endian, &last_receiver_to_ack)) {
          *acklen = 0;
          if(process_is_running(&lpprdc_cca_train_process)) {
          	strobe_ccas = 1;
            //process_post(&lpprdc_cca_train_process, PROCESS_EVENT_CONTINUE, NULL);
          }
          return 0;
        }
        is_receiver_awake = 1;
      }
			strobe_ccas = 0;
      *ackbufptr = sendbuf;
      *acklen = transmit_len;
      return 0;
    } else {
      if(process_is_running(&lpprdc_cca_train_process)) {
      	strobe_ccas = 1;
        //process_post(&lpprdc_cca_train_process, PROCESS_EVENT_CONTINUE, NULL);
      }
    }
  } else if ((fcf & 7) == ACK_PACKET_TYPE) {
    /* Ack probe */
    if(we_are_probing) {
      *acklen = 0;
      return 0;
    } else if(we_are_sending) {
    	ctimer_stop(&ct);
  	  ctimer_set(&ct, CTIMER_TIMEOUT, probe_timeout, NULL);
    }
    /* If we have data to send, we can. But if this ack was to us, it won't be ready. */
    const uint8_t *dest_addr = frame + DEST_ADDR_OFFSET;
    uint8_t dest_addr_big_endian[DEST_ADDR_LEN];
    uint8_t they_are_probing;
    for(i = 0; i < DEST_ADDR_LEN; i++) {
      dest_addr_big_endian[i] = dest_addr[DEST_ADDR_LEN - 1 - i];
    }
    uint8_t probe_id = 0;
    if(framelen >= ACK_ID_POSITION + 1) {
      probe_id = frame[ACK_ID_POSITION]; 
#if LPPRDC_RESET_BACKOFF_ON_ACK
      if(probe_id > 0) {
        //probe_for_sending = random_rand() % probes_remaining + 1;
        probe_for_sending = probe_id;
      }
#endif
      PRINTF("%d %d %d\n", probe_id, prev_probe_id, probe_for_sending);
      prev_probe_id = probe_id;
    } else {
      PRINTF("lpprdc: couldn't get probes remaining\n");
      *acklen = 0;
      return 0;
    }
    /* If this ack was to a pushed unicast packet, the node might not be probing */
    they_are_probing = probe_id > 0 ? 1 : 0;

    const uint8_t *sender_addr = frame + DEST_ADDR_OFFSET + SENDER_ADDR_LEN;
    uint8_t sender_addr_big_endian[SENDER_ADDR_LEN];
    for(i = 0; i < SENDER_ADDR_LEN; i++) {
      sender_addr_big_endian[i] = sender_addr[SENDER_ADDR_LEN - 1 - i];
    }
    if(linkaddr_cmp((linkaddr_t *)dest_addr_big_endian, &linkaddr_node_addr) && seqno == current_seqno) {
			/* This ack was to us */
      if(we_are_broadcasting) {
        is_receiver_awake = 0;
        if(!done_broadcasting) {
          probe_for_sending = INITIAL_NUM_PROBES;
          prev_probe_id = 0;
          if(!process_is_running(&lpprdc_cca_train_process)) {
            process_start(&lpprdc_cca_train_process, NULL);
            process_post(&lpprdc_cca_train_process, PROCESS_EVENT_CONTINUE, NULL);
          } else {
          	strobe_ccas = 1;
            //process_post(&lpprdc_cca_train_process, PROCESS_EVENT_CONTINUE, NULL);
          }
        } else {
          process_post(&lpprdc_send_process, PROCESS_EVENT_CONTINUE, NULL);
        }
        linkaddr_copy(&last_receiver_to_ack, (linkaddr_t *)sender_addr_big_endian);
      }

#if LPPRDC_RESET_BACKOFF_ON_ACK
      if(they_are_probing) {
        current_backoff_window = 0;
      }
#endif

      packet_was_acked = 1;
      if(!we_are_broadcasting) {
        process_post(&lpprdc_send_process, PROCESS_EVENT_POLL, NULL);
      }
#if LPPRDC_RESET_BACKOFF_ON_ACK
    } else if(we_are_sending && they_are_probing) {
#else
		} else if(we_are_sending && they_are_probing && (probe_id == probe_for_sending || probe_id == 1)) {
#endif
      /* If this wasn't an ack to us and we have data to send, we can do that */
      if(we_are_broadcasting) {
        if(linkaddr_cmp((linkaddr_t *)sender_addr_big_endian, &last_receiver_to_ack)) {
          *acklen = 0;
          if(process_is_running(&lpprdc_cca_train_process)) {
          	strobe_ccas = 1;
            //process_post(&lpprdc_cca_train_process, PROCESS_EVENT_CONTINUE, NULL);
          }
          return 0;
        }
        is_receiver_awake = 1;
      } else {
      	/*Unicasting */
#if RPL_CONF_OPP_ROUTING
        if((we_are_anycasting && rpl_node_in_forwarder_set(sender_addr_big_endian)) ||
           (!we_are_anycasting && linkaddr_cmp(sender_addr_big_endian, &current_receiver_addr))) {
#else
        if(linkaddr_cmp((linkaddr_t *)sender_addr_big_endian, &current_receiver_addr)) {
#endif
					/* Unicasting to the sender */
#if LPPRDC_RESET_BACKOFF_ON_ACK
          current_backoff_window = 0;
#endif
          memcpy(sendbuf + DEST_ADDR_OFFSET, sender_addr, SENDER_ADDR_LEN);
        } else { 
          *acklen = 0;
          if(process_is_running(&lpprdc_cca_train_process)) {
          	strobe_ccas = 1;
            //process_post(&lpprdc_cca_train_process, PROCESS_EVENT_CONTINUE, NULL);
          }
          return 0;
        }
      }
      strobe_ccas = 0;
      *ackbufptr = sendbuf;
      *acklen = transmit_len;
      return 0;
    }
  } else {
  	if(process_is_running(&lpprdc_cca_train_process)) {
  		strobe_ccas = 1;
			//process_post(&lpprdc_cca_train_process, PROCESS_EVENT_CONTINUE, NULL);
    }
  }
  *acklen = 0;
  return 0;
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

  struct initprobe_packet initprobe;
  initprobe.probe_id = 2;
  int i = 0;
  for(i = 0; i < INITPROBEBUF_SIZE - PROBEBUF_SIZE; i++) {
  	if(i % 5 == 4) {
    	initprobe.filler[i] = 0x7A;
  	} else {
  	  initprobe.filler[i] = 0x00;	
	  }
  }

  packetbuf_copyfrom(&initprobe, sizeof(struct initprobe_packet));
  packetbuf_set_addr(PACKETBUF_ADDR_RECEIVER, &linkaddr_null);
  packetbuf_set_addr(PACKETBUF_ADDR_SENDER, &linkaddr_node_addr);
  packetbuf_set_attr(PACKETBUF_ATTR_FRAME_TYPE, PACKETBUF_ATTR_PACKET_TYPE_BEACON);
  if(NETSTACK_FRAMER.create() < 0) {
    PRINTDEBUG("lpprdc: Failed to create init probe packet.\n");
  }
  PRINTF("lpprdc: init probe size %d", packetbuf_totlen());
  packetbuf_copyto(initprobebuf);

  struct probe_packet probe;
  probe.probe_id = 0;

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
  PRINTF("lpprdc: ack probe size %d %d", packetbuf_totlen(), ACK_RECEIVE_DURATION);
  packetbuf_copyto(ackbuf);

  probe_rate = NETSTACK_RDC_CHANNEL_CHECK_RATE;
  
  if(NETSTACK_DDC.use_ddc()) {
  	probe_interval = CLOCK_SECOND * NETSTACK_DDC.multiplier() / NETSTACK_DDC.divisor();
  } else {
  	probe_interval = CLOCK_SECOND / probe_rate;
  }
  //TODO: add another config option for max - or get from DDC?
  ctimer_max_probe_interval = probe_interval + probe_interval / NETSTACK_RDC_CHANNEL_CHECK_RANDOMNESS_FRACTION;
  max_probe_interval = RTIMER_ARCH_SECOND / CLOCK_SECOND * ctimer_max_probe_interval;
#ifdef SLEEP_RANGE
	listen_for_probe_timeout = SLEEP_RANGE;
#else
  listen_for_probe_timeout = 3*ctimer_max_probe_interval;
#endif

  process_start(&lpprdc_probe_process, NULL);

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
	ctimer_stop(&probe_timer);
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
	if(NETSTACK_DDC.use_ddc()) {
		return (1ul * CLOCK_SECOND * NETSTACK_DDC.multiplier() / NETSTACK_DDC.divisor());
	} else {
  	return (1ul * CLOCK_SECOND / probe_rate);
  }
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
