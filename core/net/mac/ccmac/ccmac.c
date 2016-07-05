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

#include "contiki-conf.h"
#include "net/mac/mac.h"
#include "net/linkaddr.h"
#include "sys/rtimer.h"
//#include "dev/radio.h"
#include "net/netstack.h"
#include "net/mac/ccmac/ccmac.h"

#define DEBUG 1
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINTDEBUG(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#define PRINTDEBUG(...)
#endif

#ifdef CCMAC_CONF_IS_SINK
#define IS_SINK CCMAC_CONF_IS_SINK
#else
#define IS_SINK 0
#endif

#ifdef CCMAC_CONF_INITIAL_TBEACON
#define INITIAL_TBEACON CCMAC_CONF_INITIAL_TBEACON
#else
#define INITIAL_TBEACON RTIMER_ARCH_SECOND/10
#endif

/* Same as channel check interval */
static clock_time_t _Tbeacon;

/* rtimer for beaconing */
static struct rtimer _beaconTimer;


static volatile uint8_t sink_is_beaconing = 0;
static volatile uint8_t radio_is_on = 0;
static volatile uint8_t radio_is_in_use = 0;


/* Send a beacon packet and start listening for a response. Callback to rtimer.set */
static void send_beacon(struct rtimer * rt, void * ptr) {
  ccmac_beacon_packet_t beacon;
  linkaddr_copy(&(beacon.sink_addr), &linkaddr_node_addr);
  beacon.beacon_interval = _Tbeacon;

  // off() sets sink_is_beaconing to 0. In that case, return without sending or reseting rtimer
  if (!IS_SINK || !sink_is_beaconing) {
    PRINTF("send_beacon: Skipping beacon and stopping.\n");
    return;
  }

  // Reset timer for next beacon
  rtimer_set(&_beaconTimer, RTIMER_TIME(&_beaconTimer) + _Tbeacon, 1, send_beacon, NULL);

  // If the radio is in use, don't send a beacon
  if (radio_is_in_use) {
    PRINTF("send_beacon: Skipping beacon - radio busy.\n");
    return;
  }

  radio_is_in_use = 1;
  if (!radio_is_on) {
    NETSTACK_RADIO.on();
    radio_is_on = 1;
  }

  if (!NETSTACK_RADIO.channel_clear() || NETSTACK_RADIO.receiving_packet() ||
      NETSTACK_RADIO.pending_packet()) {
    PRINTF("send_beacon: Skipping beacon - other activity detected.\n");
    radio_is_in_use = 0;
    NETSTACK_RADIO.off();
    radio_is_on = 0;
    return;
  }

  if (NETSTACK_RADIO.send(&beacon, sizeof(ccmac_beacon_packet_t)) != RADIO_TX_OK) {
    PRINTF("send_beacon: Beacon send failed.\n");
  }
  else {
    PRINTF("send_beacon: Beacon sent.\n");
  }

  radio_is_in_use = 0;
  NETSTACK_RADIO.off();
  radio_is_on = 0;
  return;
}

/*------------------RDC driver functions---------------------*/
static int on(void);


/* Should do setup for source and sink. Establish:
    initial Tbeacon (source and sink)
*/
static void init(void) {
  PRINTF("init: Initializing CC-MAC\n");
  _Tbeacon = INITIAL_TBEACON;
  on();
}

/* Should send one packet (source node operation) 
   This function assumes that there is a listening sink within range.
   To have the node wait for a beacon, use send_list.
*/
static void send(mac_callback_t sent_callback, void *ptr) {
  return;
}

/* Should send a list of packets (source node operation).
   As part of this process, the node will wait for a suitable beacon from a sink.
 */
static void send_list(mac_callback_t sent_callback, void *ptr, struct rdc_buf_list *list) {
  return;
}

static void input(void) {
  return;
}

/* If sink, turn on regular beaconing (i.e. start beacon timer).
   If source, check length of packet queue and send if Q > 0.
*/
static int on(void) {
  if (IS_SINK) {
    PRINTF("on: Starting beaconing.\n");
    rtimer_set(&_beaconTimer, RTIMER_NOW() + _Tbeacon, 1, send_beacon, NULL);
    sink_is_beaconing = 1;
    return 0;
  }
  return 1;
}

/* If sink, stop regular beaconing (i.e. stop beacon timer).
   If source, just turn off radio. */
static int off(int keep_radio_on) {
  if (IS_SINK) {
    sink_is_beaconing = 0;
    return 0;
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
