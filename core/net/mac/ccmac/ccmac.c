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
 *         CC-MAC implementation
 * \author
 *         Mat Wymore <mlwymore@iastate.edu>
 */

#define LEDS 0
#define COMPOWER_ON 0
#define TRACE_ON 1
#if TRACE_ON
#include <stdio.h>
#include "sys/clock.h"
#define TRACE(format, ...) printf("TRACE " format, __VA_ARGS__)
#else
#define TRACE(...)
#endif

#define LOG_DELAY 1

#include "contiki-conf.h"
#include "net/mac/mac.h"
#include "net/linkaddr.h"
#include "sys/rtimer.h"
//#include "dev/radio.h"
#include "net/netstack.h"
#include "net/mac/ccmac/ccmac.h"
#if LEDS
#include "dev/leds.h"
#endif
#include "sys/process.h"


#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINTDEBUG(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#define PRINTDEBUG(...)
#endif

#define LIMITED_DEBUG 1
#if LIMITED_DEBUG
#include <stdio.h>
#define LIM_PRINTF(...) printf(__VA_ARGS__)
#else
#define LIM_PRINTF(...)
#endif

#ifdef CCMAC_CONF_IS_SINK
#define IS_SINK CCMAC_CONF_IS_SINK
#else
#define IS_SINK 0
#endif

#ifdef CCMAC_CONF_INITIAL_TBEACON
#define INITIAL_TBEACON CCMAC_CONF_INITIAL_TBEACON
#else
#define INITIAL_TBEACON RTIMER_SECOND/10
#endif

/* INTER_PACKET_DEADLINE is the maximum time a receiver waits for the
   next packet. Longer if DEBUG is enabled because the PRINTFs take time. */
#ifdef CCMAC_CONF_INTER_PACKET_DEADLINE
#define INTER_PACKET_DEADLINE               CCMAC_CONF_INTER_PACKET_DEADLINE
#else
#if DEBUG || LIMITED_DEBUG
#define INTER_PACKET_DEADLINE               RTIMER_SECOND / 65
//#elif LIMITED_DEBUG
//#define INTER_PACKET_DEADLINE               RTIMER_SECOND / 100
#else
#define INTER_PACKET_DEADLINE               RTIMER_SECOND / 200
#endif
#endif

#if COMPOWER_ON
static struct compower_activity current_packet_compower;
#endif

/* Same as channel check interval */
static clock_time_t _Tbeacon;

/* rtimer for beaconing */
static struct rtimer _beaconTimer;
/* rtimer for turning radio off after sending */
static struct rtimer _offTimer;

enum {
  ROFF_CANCEL,
  ROFF_SEND_BEACON,
  ROFF_INPUT_PARSE_FAIL,
  ROFF_INPUT_LAST_ACK_SENT,
};

static volatile uint8_t radio_is_on = 0;
//static volatile uint8_t radio_off_arg = 0;
//static volatile uint8_t last_radio_off_caller = 0;
//static volatile uint8_t do_retry_packet = 0;

static volatile uint8_t sink_is_beaconing = 0;
static volatile uint8_t sink_is_awake = 0;
static volatile uint8_t packetbuf_locked = 0;
static volatile uint8_t sending_burst;
static volatile uint8_t receiving_burst;

static uint8_t tx_counter = 0;

/* Used to allow process to call MAC callback */
static mac_callback_t _sent_callback;
static void * _ptr;

static uint8_t _backupPacketbuf[PACKETBUF_SIZE];
static uint8_t _backupPacketbufLen;

PROCESS(wait_to_send_process, "Process that waits for a go-ahead signal");
static int off(int keep_radio_on);
static void send_beacon(struct rtimer * rt, void * ptr);

static void turn_radio_off(struct rtimer * rt, void * ptr) {
  rtimer_clock_t now;
  uint8_t beacon_intervals;
  int32_t temp_time;

  PRINTF("turn_radio_off: Turning off.\n");
  process_exit(&wait_to_send_process);
  packetbuf_locked = 0;
  NETSTACK_RADIO.off();
  TRACE("%lu RADIO_OFF 0\n", (unsigned long)clock_time());
#if COMPOWER_ON
  if (radio_is_on) {
    compower_accumulate(&compower_idle_activity);
  }
#endif
  radio_is_on = 0;
  sink_is_awake = 0;
  sending_burst = 0;
  receiving_burst = 0;
  tx_counter = 0;
  _backupPacketbufLen = 0;
  
  rtimer_unset();
  /* Set timer for the next beacon */
  if (IS_SINK && sink_is_beaconing) {
    /* Possible we have to go multiple beacon intervals forward from the last beacon timer expiration*/
    beacon_intervals = 1;
    temp_time = 0;
    do {
      temp_time = RTIMER_TIME(&_beaconTimer) + _Tbeacon * beacon_intervals;
      /* Since rtimer_clock_t is 16-bit, we can easily have rollover problems */
      /* This solution is kind of hacky. */
      if (temp_time > 65535) {
        //LIM_PRINTF("cc-mac: Beacon timer rollover\n");
        temp_time -= 65535;
        now = RTIMER_NOW();
        /* If rtimer is currently at a higher tick than the last time the beacon timer expired,
           then almost certainly that means the next beacon time is after the rollover, but
           the rollover hasn't happened yet. Otherwise, the rollover has already happened and we
           need to keep adding beacon intervals if temp_time is less than now.
           There may be some really weird edge case where all this isn't true - not sure. */
        if (now > RTIMER_TIME(&_beaconTimer)) {
          break;
        }
      }
      beacon_intervals++;
      now = RTIMER_NOW();
    } while (temp_time < now);
    //LIM_PRINTF("cc-mac: Setting beacon timer for %u (now is %u)\n", (uint16_t)temp_time, now);
    rtimer_set(&_beaconTimer, (rtimer_clock_t)temp_time, 1, send_beacon, NULL);
  }
}

static void retry_packet(struct rtimer * rt, void * ptr) {
  uint8_t max_txs;

  if (packetbuf_attr(PACKETBUF_ATTR_MAX_MAC_TRANSMISSIONS) == 0) {
    max_txs = 1;
  }
  else {
    max_txs = packetbuf_attr(PACKETBUF_ATTR_MAX_MAC_TRANSMISSIONS);
  }
  if (tx_counter >= max_txs) {
    PRINTF("retry_packet: max transmissions have failed, canceling\n");
    turn_radio_off(NULL, NULL);
    mac_call_sent_callback(_sent_callback, _ptr, MAC_TX_NOACK, tx_counter);
    return;
  }

  process_post_synch(&wait_to_send_process, PROCESS_EVENT_CONTINUE, NULL);
  return;
}

static int send_packet() {
  PRINTF("send_packet: Sending packet.\n");
  if (!packetbuf_attr(PACKETBUF_ATTR_IS_CREATED_AND_SECURED)) {
    if (NETSTACK_FRAMER.create() < 0) {
      PRINTF("send_packet: framer failed.\n");
      return MAC_TX_ERR_FATAL;
    }
  }

  if (!NETSTACK_RADIO.channel_clear() || NETSTACK_RADIO.receiving_packet() ||
      NETSTACK_RADIO.pending_packet()) {
    PRINTF("send_packet: other activity detected.\n");
    return MAC_TX_COLLISION;
  }

  if (NETSTACK_RADIO.send(packetbuf_hdrptr(), packetbuf_totlen()) == RADIO_TX_OK) {
    return MAC_TX_OK;
  }

   return MAC_TX_ERR;
}

/* Send a beacon packet and start listening for a response. Callback to rtimer.set */
static void send_beacon(struct rtimer * rt, void * ptr) {
  int ret;
  ccmac_beacon_payload_t beacon;
  beacon.beacon_interval = _Tbeacon;

  // off() sets sink_is_beaconing to 0. In that case, return without sending or resetting rtimer
  if (!IS_SINK || !sink_is_beaconing) {
    PRINTF("send_beacon: Skipping beacon and stopping.\n");
    return;
  }

  if (receiving_burst) {
    PRINTF("send_beacon: Skipping beacon - receiving a burst.\n");
    // Reset timer for next beacon
    rtimer_set(&_beaconTimer, RTIMER_TIME(&_beaconTimer) + _Tbeacon, 1, send_beacon, NULL);
    return;
  }

  // If the radio is in use, don't send a beacon
  if (packetbuf_locked) {
    PRINTF("send_beacon: Skipping beacon - packetbuf locked.\n");
    // Reset timer for next beacon
    rtimer_set(&_beaconTimer, RTIMER_TIME(&_beaconTimer) + _Tbeacon, 1, send_beacon, NULL);
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
  memcpy(packetbuf_dataptr(), &beacon, sizeof(ccmac_beacon_payload_t));

  ret = send_packet();
  TRACE("%lu BEACON 0\n", (unsigned long)clock_time());
  packetbuf_locked = 0;

  if (ret != MAC_TX_OK) {
    PRINTF("send_beacon: Beacon failed, skipping.\n");
    turn_radio_off(NULL, NULL);
    return;
  }

  PRINTF("send_beacon: Beacon sent.\n");
  #if LEDS
  leds_toggle(LEDS_GREEN);
  #endif

  rtimer_set(&_offTimer, RTIMER_NOW() + INTER_PACKET_DEADLINE, 1, turn_radio_off, NULL);

  return;
}

PROCESS_THREAD(wait_to_send_process, ev, data) {
  PROCESS_BEGIN();

  static struct rdc_buf_list *curr;
  static struct rdc_buf_list *next;
  static int ret;

  static int old_packet;
  static uint16_t *pktSeqno;

  if (data != NULL) {
    curr = data;
  }

  old_packet = 0;

    do {
      PROCESS_WAIT_EVENT_UNTIL(sink_is_awake && ev == PROCESS_EVENT_CONTINUE);

      rtimer_unset();

      /* if we've tried this packet already and the tx count is 0, 
         then we must got an ack, so we ready a new packet */
      if (old_packet && tx_counter == 0) {
        if (next != NULL) {
          curr = next;
          old_packet = 0;
        }
        else {
          break;
        }
      }

      tx_counter++;
      old_packet = 1;

      PRINTF("wait_to_send_process: Time to send!\n");
      #if LEDS
      leds_toggle(LEDS_RED);
      #endif

      if (packetbuf_locked) {
        PRINTF("wait_to_send_process: Packetbuf locked, waiting.\n");
        rtimer_set(&_offTimer, RTIMER_NOW() + INTER_PACKET_DEADLINE, 1, retry_packet, NULL);

        continue;
      }

      packetbuf_locked = 1;
      next = list_item_next(curr);
      queuebuf_to_packetbuf(curr->buf);

      ret = send_packet();
      TRACE("%lu DATA 0 %u\n", (unsigned long)clock_time(), packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO));
      packetbuf_locked = 0;

      if (ret == MAC_TX_ERR_FATAL || ret == MAC_TX_ERR) {
        pktSeqno = (uint16_t *)_ptr;
        *pktSeqno = packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO);
        mac_call_sent_callback(_sent_callback, _ptr, MAC_TX_ERR_FATAL, 1);
        off(0);
        break;
      }

      rtimer_set(&_offTimer, RTIMER_NOW() + INTER_PACKET_DEADLINE, 1, retry_packet, NULL);
      
    } while (1);

  turn_radio_off(NULL, NULL);
  PROCESS_END();
}

/*------------------RDC driver functions---------------------*/
static int on(void);
static void send_list(mac_callback_t sent_callback, void *ptr, struct rdc_buf_list *buf_list);


/* Should do setup for source and sink. Establish:
    initial Tbeacon (source and sink)
*/
static void init(void) {
  PRINTF("init: Initializing CC-MAC\n");
  _Tbeacon = INITIAL_TBEACON;
  /* Call turn_radio_off to clear all flags and whatnot */
  turn_radio_off(NULL, NULL);
  on();
}

/* Sends one packet (source node operation) */
static void send(mac_callback_t sent_callback, void *ptr) {
  static struct rdc_buf_list list;
  list.next = NULL;
  list.buf = queuebuf_new_from_packetbuf();
  list.ptr = NULL;
  send_list(sent_callback, ptr, &list);
  return;
}

/* Sends a list of packets (source node operation).
   As part of this process, the node will wait for a suitable beacon from a sink. */
static void send_list(mac_callback_t sent_callback, void *ptr, struct rdc_buf_list *buf_list) {
  struct rdc_buf_list *curr;
  struct rdc_buf_list *next;
  PRINTF("send_list: Packets to send.\n");
  TRACE("%lu DATA_ARRIVAL 0\n", (unsigned long)clock_time());

  if (packetbuf_locked) {
    PRINTF("send_list: Packetbuf locked, can't secure packets.\n");
    mac_call_sent_callback(sent_callback, ptr, MAC_TX_COLLISION, 1);
    return;
  }
  packetbuf_locked = 1;
 
  curr = buf_list;
  do {
    next = list_item_next(curr);
    queuebuf_to_packetbuf(curr->buf);
    if(!packetbuf_attr(PACKETBUF_ATTR_IS_CREATED_AND_SECURED)) {
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
    curr = next;
  } while(next != NULL);

  packetbuf_locked = 0;

  _sent_callback = sent_callback;
  _ptr = ptr;
  
  sending_burst = 1;
  process_start(&wait_to_send_process, buf_list);
  radio_is_on = 1;
  NETSTACK_RADIO.on();
  TRACE("%lu RADIO_ON 0\n", (unsigned long)clock_time());
  
  return;
}

static void input(void) {
  int ret;
  uint16_t dataSeqno;
  uint16_t *ackSeqno;

  LIM_PRINTF("input: Handed packet from radio w/ RSSI %d.\n", packetbuf_attr(PACKETBUF_ATTR_RSSI));
  /* Stay awake for now - listen for another possible packet */
  rtimer_unset();

  /* Check if the packetbuf is locked, but I think we need to go ahead anyway */
  if (packetbuf_locked) {
    PRINTF("input: Receive might be interrupting another task.\n");
  }
  packetbuf_locked = 1;

  if (NETSTACK_FRAMER.parse() < 0) {
    PRINTF("input: Framer failed to parse packet.\n");
    packetbuf_locked = 0;
    rtimer_set(&_offTimer, RTIMER_NOW() + INTER_PACKET_DEADLINE, 1, turn_radio_off, NULL);
    return;
  }

  switch (packetbuf_attr(PACKETBUF_ATTR_PACKET_TYPE)) {
    case PACKETBUF_ATTR_PACKET_TYPE_BEACON:
      PRINTF("input: It's a beacon!\n");
      TRACE("%lu BEACON_RECEIVED 0\n", (unsigned long)clock_time());
      packetbuf_locked = 0;
      if (process_is_running(&wait_to_send_process)) {
        sink_is_awake = 1;
        process_post_synch(&wait_to_send_process, PROCESS_EVENT_CONTINUE, NULL);
      }
      break;
    case PACKETBUF_ATTR_PACKET_TYPE_DATA:
      dataSeqno = packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO);
      PRINTF("input: It's data! seqno %d\n", dataSeqno);
      TRACE("%lu DATA_RECEIVED 0 %u\n", (unsigned long)clock_time(), dataSeqno);

      receiving_burst = 1;

      /* We are sink, need to respond with ACK */
      linkaddr_t sourceAddr;
      linkaddr_copy(&sourceAddr, packetbuf_addr(PACKETBUF_ADDR_SENDER));
      
      if (_backupPacketbufLen != 0) {
        PRINTF("input: Overwriting saved packetbuf to send ack.\n");
      }
      //TODO: possibly get rid of backup packetbuf
      packetbuf_copyto(_backupPacketbuf);
      _backupPacketbufLen = packetbuf_totlen();

      packetbuf_clear();
      packetbuf_set_addr(PACKETBUF_ADDR_SENDER, &linkaddr_node_addr);
      packetbuf_set_addr(PACKETBUF_ADDR_RECEIVER, &sourceAddr);
      packetbuf_set_attr(PACKETBUF_ATTR_PACKET_TYPE, PACKETBUF_ATTR_PACKET_TYPE_ACK);
      packetbuf_set_attr(PACKETBUF_ATTR_MAC_SEQNO, dataSeqno);
      //memcpy(packetbuf_dataptr(), "ACK", 3);
      //memcpy(packetbuf_dataptr(), &beacon, sizeof(ccmac_beacon_payload_t));

      ret = send_packet();
      TRACE("%lu ACK 0 %u\n", (unsigned long)clock_time(), dataSeqno);

      if (ret == MAC_TX_OK) {
        PRINTF("input: Ack sent.\n");
        #if LEDS
        leds_toggle(LEDS_RED);
        #endif
      }
      else {
        PRINTF("input: Ack send failed.\n");
      }
      if (!packetbuf_attr(PACKETBUF_ATTR_PENDING)) {
        rtimer_set(&_offTimer, RTIMER_NOW() + INTER_PACKET_DEADLINE, 1, turn_radio_off, NULL);
      }

      packetbuf_copyfrom(_backupPacketbuf, _backupPacketbufLen);
      _backupPacketbufLen = 0;

      NETSTACK_MAC.input();

      packetbuf_locked = 0;
      break;
    case PACKETBUF_ATTR_PACKET_TYPE_ACK:
      PRINTF("input: It's an ack! _ptr is %d\n", (int)_ptr);
      TRACE("%lu ACK_RECEIVED 0 %u\n", (unsigned long)clock_time(), packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO));
      /* Use the seqno to ID the packet instead of loading it back into packetbuf */
      ackSeqno = (uint16_t *)_ptr;
      *ackSeqno = packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO);
      packetbuf_locked = 0;
      tx_counter = 0;

      mac_call_sent_callback(_sent_callback, _ptr, MAC_TX_OK, 1);

      if (!sending_burst) {
        turn_radio_off(NULL, NULL);
      }
      process_post_synch(&wait_to_send_process, PROCESS_EVENT_CONTINUE, NULL);
      break;
  }
  return;
}

/* If sink, turn on regular beaconing (i.e. start beacon timer).
   If source, check length of packet queue and send if Q > 0. */
static int on(void) {
  if (IS_SINK) {
    PRINTF("on: Sink, starting beaconing.\n");
    rtimer_set(&_beaconTimer, RTIMER_NOW() + _Tbeacon, 1, send_beacon, NULL);
    sink_is_beaconing = 1;
    return 1;
  }
  else {
    PRINTF("on: Source, doing nothing.\n");
    return 1;
  }
  return 0;
}

/* If sink, stop regular beaconing (i.e. stop beacon timer).
   If source, just turn off radio. */
static int off(int keep_radio_on) {
  sink_is_beaconing = 0;
  if (!keep_radio_on) {
    turn_radio_off(NULL, NULL);
  }
  return 1;
}

/* Return Tbeacon */
static unsigned short channel_check_interval(void) {
  return _Tbeacon;
}

const struct rdc_driver ccmac_driver = {
  "ccmac",
  init,
  send,
  send_list,
  input,
  on,
  off,
  channel_check_interval,
};
