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
 *         BladeMAC implementation
 * \author
 *         Mat Wymore <mlwymore@iastate.edu>
 */

#define LOG_DELAY 1
#define LOG_WINDOW 1
#define DEBUG 0
#define LIMITED_DEBUG 0
#define TRACE_ON 0

#include "sys/clock.h"

#define LEDS 0
#define COMPOWER_ON 0

#if TRACE_ON
#include <stdio.h>
#define TRACE(format, ...) printf("TRACE " format, __VA_ARGS__)
#else
#define TRACE(...)
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
//uint8_t delay_seqnos[MAX_QUEUED_PACKETS];
//clock_time_t delay_timestamp;
#define PRINT_DELAY(format, ...) printf("DELAY " format, __VA_ARGS__)
#endif

#if LOG_WINDOW
#include <stdio.h>
#define WINDOW(format, ...) printf("WINDOW " format, __VA_ARGS__)
#else
#define WINDOW(...)
#endif

#include "contiki-conf.h"
#include "net/mac/mac.h"
#include "net/linkaddr.h"
#include "sys/rtimer.h"
//#include "dev/radio.h"
#include "net/netstack.h"
#include "net/mac/ccmac/blademac.h"
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

#ifdef CCMAC_CONF_IS_SINK
#define IS_SINK CCMAC_CONF_IS_SINK
#else
#define IS_SINK 0
#endif

#ifdef CCMAC_CONF_INITIAL_TBEACON
#define INITIAL_TBEACON CCMAC_CONF_INITIAL_TBEACON
#else
#define INITIAL_TBEACON RTIMER_SECOND/4
#endif

/* INTER_PACKET_DEADLINE is the maximum time a receiver waits for the
   next packet. Longer if DEBUG is enabled because the PRINTFs take time. */
#ifdef CCMAC_CONF_INTER_PACKET_DEADLINE
#define INTER_PACKET_DEADLINE               CCMAC_CONF_INTER_PACKET_DEADLINE
#else
#if DEBUG
#define INTER_PACKET_DEADLINE               RTIMER_SECOND / 65
//#define RTIMER_WAKEUP_BUFFER_TIME RTIMER_SECOND / 200
//#elif LIMITED_DEBUG
//#define INTER_PACKET_DEADLINE               RTIMER_SECOND / 100
#elif defined(TRACE_ON) || defined(LIMITED_DEBUG)
#define INTER_PACKET_DEADLINE               RTIMER_SECOND / 100
//#define RTIMER_WAKEUP_BUFFER_TIME RTIMER_SECOND / 333
#else
#define INTER_PACKET_DEADLINE               RTIMER_SECOND / 200
//#define RTIMER_WAKEUP_BUFFER_TIME RTIMER_SECOND / 333
#endif
#endif

#define RTIMER_WAKEUP_BUFFER_TIME RTIMER_SECOND / 250
//#define CTIMER_WAKEUP_BUFFER_TIME CLOCK_SECOND / 128

#define RTIMER_MAX_TICKS 65536

#if COMPOWER_ON
static struct compower_activity current_packet_compower;
#endif

/* Same as channel check interval */
static rtimer_clock_t _Tbeacon;

/* rtimer for beaconing */
static struct rtimer _beaconTimer;
/* rtimer for turning radio off after sending */
static struct rtimer _offTimer;

static volatile uint8_t radio_is_on = 0;

static volatile uint8_t sink_is_beaconing = 0;
static volatile uint8_t sink_is_awake = 0;
static volatile uint8_t packetbuf_locked = 0;
static volatile uint8_t sending_burst = 0;
static volatile uint8_t receiving_burst = 0;

/* cpccmac specific */
static rtimer_clock_t last_known_beacon = 0;
//static clock_time_t period_estimate = 0;
static clock_time_t last_known_rendezvous = 0;
//static uint16_t beacon_intervals_napped = 0;
static volatile uint8_t estimating_period = 0;
static volatile uint8_t beacon_gap_seen = 0;
static struct rtimer _wakeupTimer;
/* ---------------- */

/* blademac specific */
static rtimer_clock_t _Tsleep;
static packetbuf_attr_t prev_beacon_rss = 0;
static volatile uint8_t prev_beacon_seen = 0;
static volatile uint8_t beacon_phase_known = 0;
enum {
  BLADEMAC_STATE_HIBERNATION,
  BLADEMAC_STATE_WAIT,
  BLADEMAC_STATE_SEND
};
static uint8_t current_state = 0;
#define USE_EXTRA_BEACONS 1
#define FAVORABLE_THRESHOLD -80

struct rss_sample {
  struct rss_sample *next;
  clock_time_t timestamp;
  packetbuf_attr_t rss;
  //packetbuf_attr_t packet_type;
};

struct window_estimate {
  struct window_estimate *next;
  clock_time_t val;
};

#define MAX_RSS_SAMPLES 100
#define NUM_WINDOW_ESTIMATES 10
MEMB(rss_sample_memb, struct rss_sample, MAX_RSS_SAMPLES);
MEMB(window_estimate_memb, struct window_estimate, NUM_WINDOW_ESTIMATES);
LIST(beacon_rss_sample_list);
LIST(ack_rss_sample_list);
LIST(window_estimate_list);

static struct rdc_buf_list *curr_packet;
static struct rdc_buf_list *next_packet;

/* ----------------- */

static uint8_t tx_counter = 0;

/* Used to allow process to call MAC callback */
static mac_callback_t _sent_callback;
static void * _ptr;

static uint8_t _backupPacketbuf[PACKETBUF_SIZE];
static uint8_t _backupPacketbufLen;

PROCESS(wait_to_send_process, "Process that waits for a go-ahead signal");
static int off(int keep_radio_on);
static void send_beacon(struct rtimer * rt, void * ptr);
static void wake_up(struct rtimer * rt, void * ptr);
//static void wake_up_wrapper(void * ptr);
static void hibernate(struct rtimer * rt, void * ptr);
static void clean_radio(void);

static void clear_rss_samples() {
  struct rss_sample *sample;
  PRINTF("clear_rss_samples: Deleting samples\n");
  sample = list_head(ack_rss_sample_list);
  while (list_length(ack_rss_sample_list) > 0) {
    memb_free(&rss_sample_memb, sample);
    list_remove(ack_rss_sample_list, sample);
    sample = list_head(ack_rss_sample_list);
  }
  sample = list_head(beacon_rss_sample_list);
  while (list_length(beacon_rss_sample_list) > 0) {
    memb_free(&rss_sample_memb, sample);
    list_remove(beacon_rss_sample_list, sample);
    sample = list_head(beacon_rss_sample_list);
  }
  LIM_PRINTF("clear_rss_samples: cleared samples, list lengths %d %d\n", list_length(ack_rss_sample_list), list_length(beacon_rss_sample_list));
}

static void estimate_window() {
  struct window_estimate *estimate;
  struct rss_sample *sample;
  struct rss_sample *next_sample;
  packetbuf_attr_t max_rss;
  int32_t data_group_rss;
  uint8_t using_data_group_peak = 1;
  clock_time_t t_first;
  clock_time_t t_peak;
  clock_time_t t_next;
  clock_time_t t_last;
  uint64_t moving_avg;

  /* Covers "single packet" case */
  if (list_length(ack_rss_sample_list) == 0) {
    using_data_group_peak = 0;
  }
  if (using_data_group_peak + list_length(beacon_rss_sample_list) <= 1) {
    return;
  }
  
  /* Create space for the new estimate */
  if (list_length(window_estimate_list) == NUM_WINDOW_ESTIMATES) {
    estimate = list_head(window_estimate_list);
    memb_free(&window_estimate_memb, estimate);
    list_remove(window_estimate_list, estimate);
  }

  /* Find data group RSS */
  data_group_rss = 0;
  sample = list_head(ack_rss_sample_list);
  while (sample != NULL) {
    data_group_rss += (int32_t)(sample->rss);
    sample = list_item_next(sample);
  }

  next_sample = NULL;
  sample = list_head(beacon_rss_sample_list);
  if (using_data_group_peak) {
    data_group_rss /= list_length(ack_rss_sample_list);
    max_rss = (packetbuf_attr_t)data_group_rss;
    t_peak = ((struct rss_sample *)list_tail(ack_rss_sample_list))->timestamp;
  }
  else {
    max_rss = sample->rss;
    t_peak = sample->timestamp;
    //using_data_group_peak = 0;
  }
  PRINTF("estimate_window: data group rss %d\n", (packetbuf_attr_t)data_group_rss);

  t_first = t_peak;
  t_last = t_peak;
  t_next = 0;

  while (sample != NULL) {
    next_sample = list_item_next(sample);
    if (sample->timestamp < t_first) {
      t_first = sample->timestamp;
    }
    if (sample->timestamp > t_last) {
      t_last = sample->timestamp;
    }
    if (sample->rss >= max_rss) {
      max_rss = sample->rss;
      t_peak = sample->timestamp;
      using_data_group_peak = 0;
      if (next_sample != NULL) {
        t_next = next_sample->timestamp;
      }
    }
    else if (next_sample != NULL && using_data_group_peak && sample->timestamp <= t_peak && next_sample->timestamp > t_peak) {
      t_next = next_sample->timestamp;
      PRINTF("estimate_window: t_next found\n");
    }
    sample = next_sample;
  }
  if (t_next == 0) {
    t_next = t_last;
  }
  LIM_PRINTF("estimate_window: t_first %lu, t_peak %lu, t_next %lu, t_last %lu, max RSS %d\n",
    (unsigned long)t_first, (unsigned long)t_peak, (unsigned long)t_next, (unsigned long)t_last, max_rss);

  estimate = memb_alloc(&window_estimate_memb);

  /* Two remaining cases */
  if (max_rss < FAVORABLE_THRESHOLD) {
    estimate->val = 2*(t_last - t_peak);
  }
  else {
    //estimate->val = (t_last - t_peak) + (t_last - t_next);
    estimate->val = 2*(t_last - t_next);
  }

  /* Check the lower bound */
  if (estimate->val < t_last - t_first) {
    estimate->val = t_last - t_first;
  }

  LIM_PRINTF("estimate_window: this estimate is %lu\n", (unsigned long)estimate->val);

  list_add(window_estimate_list, estimate);

  /* Calculate moving average for the window */
  estimate = list_head(window_estimate_list);
  moving_avg = 0;
  while (estimate != NULL) {
    moving_avg += estimate->val;
    estimate = list_item_next(estimate);
  }
  moving_avg /= list_length(window_estimate_list);
  
  /* Set Tsleep! */
  _Tsleep = (moving_avg / 2) * RTIMER_SECOND / CLOCK_SECOND;
  LIM_PRINTF("estimate_window: tsleep %u, tbeacon %u, %d\n", _Tsleep, _Tbeacon, list_length(window_estimate_list));
  WINDOW("%lu %lu\n", (unsigned long)((struct window_estimate *)list_tail(window_estimate_list))->val, (unsigned long)_Tsleep);
  /* Delete RSS samples */
  //clear_rss_samples();
}

static void sleep(void) {
  uint64_t temp_time;

  clean_radio();
  NETSTACK_RADIO.off();
  TRACE("%lu RADIO_OFF 0\n", (unsigned long)clock_time());
  radio_is_on = 0;

  sending_burst = 0;
  tx_counter = 0;

  rtimer_unset();

  if (_Tsleep < _Tbeacon) {
    PRINTF("sleep: initializing tsleep\n");
    _Tsleep = _Tbeacon;
  }
  PRINTF("sleep: sleeping for %lu\n", _Tsleep);

  //ctimer_stop(&_wakeupTimer);
  //ctimer_set(&_wakeupTimer, _Tsleep, wake_up_wrapper, NULL);
  temp_time = (uint64_t) RTIMER_NOW() + (uint64_t) _Tsleep;
  temp_time %= RTIMER_MAX_TICKS;
  rtimer_set(&_wakeupTimer, temp_time, 1, wake_up, NULL);
  prev_beacon_seen = 0;
  beacon_phase_known = 0;

  LIM_PRINTF("blademac: Setting sleep timer for %u (now is %u), curr state %d\n", _Tsleep, RTIMER_NOW(), current_state);

  clear_rss_samples();
}

static void nap(void) {
  static volatile uint64_t temp_time;
  static volatile rtimer_clock_t now;
  uint16_t beacon_intervals;

  rtimer_unset();

  //packetbuf_locked = 0;
  clean_radio();
  NETSTACK_RADIO.off();
  TRACE("%lu RADIO_OFF 0\n", (unsigned long)clock_time());
  radio_is_on = 0;

  sending_burst = 0;

  /* Possible we have to go multiple beacon intervals forward from the last beacon timer expiration*/
  /* Don't listen for the first beacon following data/ack. This first covers an edge case
     where the sink and source are both finishing data/ack, but the sink takes longer to schedule
     the next beacon, so the source wakes for a beacon that isn't sent. */
  /*if (estimating_period && current_state == BLADEMAC_STATE_SEND) {
    current_state = BLADEMAC_STATE_HIBERNATION;
    beacon_intervals = 2;
  }
  else {
    beacon_intervals = 1;
  }*/
  beacon_intervals = 1;
  temp_time = 0;
  do {
    temp_time = (uint64_t)last_known_beacon + (uint64_t)(_Tbeacon * beacon_intervals) - RTIMER_WAKEUP_BUFFER_TIME;
    /* Since rtimer_clock_t is 16-bit for Sky, we can easily have rollover problems */
    /* This solution is kind of hacky, especially if the clock isn't 16-bit... */
    if (temp_time >= RTIMER_MAX_TICKS) {
      LIM_PRINTF("cpcc-mac: Nap timer rollover\n");
      temp_time %= RTIMER_MAX_TICKS;
      now = RTIMER_NOW();
      /* If rtimer is currently at a higher tick than the last time the beacon timer expired,
           then almost certainly that means the next beacon time is after the rollover, but
           the rollover hasn't happened yet. Otherwise, the rollover has already happened and we
           need to keep adding beacon intervals if temp_time is less than now.
           There may be some really weird edge case where all this isn't true - not sure. */
      if (now > last_known_beacon) {
        break;
      }
    }
    beacon_intervals++;
    now = RTIMER_NOW();
  } while (temp_time < now);
  LIM_PRINTF("cpcc-mac: Setting nap timer for %u (now is %u) %d %u\n", (uint16_t)temp_time, now, beacon_intervals, RTIMER_TIME(&_beaconTimer));
  rtimer_set(&_beaconTimer, (rtimer_clock_t)temp_time, 1, wake_up, NULL);
}

static void beacon_timed_out(struct rtimer * rt, void * ptr) {
  sending_burst = 0;
  if (estimating_period) {
    PRINTF("beacon_timed_out: beacon gap detected\n");
    //beacon_gap_seen = 1;
    estimating_period = 0;
    //nap();
    estimate_window();
    clear_rss_samples();
  }
  if (current_state == BLADEMAC_STATE_WAIT && !prev_beacon_seen) {
    sleep();
  }
  /* If a beacon timed out after a predicted wakeup, redo the period estimate */
  else if (current_state == BLADEMAC_STATE_WAIT && prev_beacon_seen) {
    //PRINTF("beacon_timed_out: period estimation redo required\n");
    //period_estimate = 0;
    prev_beacon_seen = 0;
    nap();
  }
  else {
    //LIM_PRINTF("beacon_timed_out: how did I get here?\n");
    hibernate(NULL, NULL);
  }
}

static void wake_up(struct rtimer * rt, void * ptr) {
  static volatile uint64_t timeout_time;

  radio_is_on = 1;
  NETSTACK_RADIO.on();

  if (beacon_phase_known) {
    //beacon_intervals_napped++;
    timeout_time = RTIMER_NOW() + INTER_PACKET_DEADLINE;
  }
  else {
    timeout_time = RTIMER_NOW() + _Tbeacon + RTIMER_WAKEUP_BUFFER_TIME;
  }

  timeout_time %= RTIMER_MAX_TICKS;

  rtimer_unset();
  rtimer_set(&_offTimer, (rtimer_clock_t)timeout_time, 1, beacon_timed_out, NULL);
  sending_burst = 1;

  TRACE("%lu RADIO_ON 0\n", (unsigned long)clock_time());
  return;
}

/*static void wake_up_wrapper(void * ptr) {
  wake_up(NULL, ptr);
}*/

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
  LIM_PRINTF("set_periodic_rtimer: Setting timer for %u (now is %u)\n", (uint16_t)b, (uint16_t) now);
}

static void clean_radio(void) {
  /* Throw away anything still stuck in the radio */
  if (NETSTACK_RADIO.pending_packet()) {
    LIM_PRINTF("clean_radio: removing pending packet\n");
    NETSTACK_RADIO.read(_backupPacketbuf, PACKETBUF_SIZE);
  }
}

static void hibernate(struct rtimer * rt, void * ptr) {
  //rtimer_clock_t now;
  //uint8_t beacon_intervals;
  //uint32_t temp_time;

  PRINTF("hibernate: Turning off.\n");
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
  sink_is_awake = 0;
  sending_burst = 0;
  receiving_burst = 0;
  tx_counter = 0;
  //_backupPacketbufLen = 0;

  prev_beacon_seen = 0;
  beacon_phase_known = 0;
  current_state = BLADEMAC_STATE_HIBERNATION;
  estimating_period = 0;
  
  //ctimer_stop(&_wakeupTimer);
  rtimer_unset();
  /* Set timer for the next beacon */
  if (IS_SINK && sink_is_beaconing) {
    set_periodic_rtimer(&_beaconTimer, _Tbeacon, send_beacon);
    /* Possible we have to go multiple beacon intervals forward from the last beacon timer expiration*/
//    now = RTIMER_NOW();
 //   temp_time = 65536;
 //   if (now > RTIMER_TIME(&_beaconTimer)) {
 //     beacon_intervals = (now - RTIMER_TIME(&_beaconTimer)) / _Tbeacon;
 //   }
 //   else {
  //    temp_time += now;
  //    beacon_intervals = (temp_time - RTIMER_TIME(&_beaconTimer)) / _Tbeacon;;
//    }
//    temp_time = 0;
//    do {
//      temp_time = (uint32_t)RTIMER_TIME(&_beaconTimer) + (uint32_t)_Tbeacon * beacon_intervals;
      /* Since rtimer_clock_t is 16-bit for Sky, we can easily have rollover problems */
      /* This solution is kind of hacky, especially if the clock isn't 16-bit... */
//      if (temp_time > 65535) {
//        LIM_PRINTF("cc-mac: Beacon timer rollover\n");
//        temp_time -= 65536;
//        now = RTIMER_NOW();
        /* If rtimer is currently at a higher tick than the last time the beacon timer expired,
           then almost certainly that means the next beacon time is after the rollover, but
           the rollover hasn't happened yet. Otherwise, the rollover has already happened and we
           need to keep adding beacon intervals if temp_time is less than now.
           There may be some really weird edge case where all this isn't true - not sure. */
//        if (now > RTIMER_TIME(&_beaconTimer)) {
//          break;
//        }
//      }
//      beacon_intervals++;
//      now = RTIMER_NOW();
//    } while (temp_time + 300 < now);
//    LIM_PRINTF("cc-mac: Setting beacon timer for %u (now is %u) %d %u\n", (uint16_t)temp_time, now, beacon_intervals, RTIMER_TIME(&_beaconTimer));
//    rtimer_set(&_beaconTimer, (rtimer_clock_t)temp_time, 1, send_beacon, NULL);
  }
  else if (!IS_SINK) {
    clear_rss_samples();
  }
  
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
  if (tx_counter >= max_txs) {
    curr_txs = tx_counter;
    LIM_PRINTF("retry_packet: max transmissions have failed, sleeping\n");
    //hibernate(NULL, NULL);
    if (estimating_period) {
      estimating_period = 0;
      estimate_window();
      clear_rss_samples();
    }
    current_state = BLADEMAC_STATE_WAIT;
    sleep();
    mac_call_sent_callback(_sent_callback, _ptr, MAC_TX_NOACK, curr_txs);
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
    hibernate(NULL, NULL);
    return;
  }

  PRINTF("send_beacon: Beacon sent.\n");
  #if LEDS
  leds_toggle(LEDS_GREEN);
  #endif

  rtimer_set(&_offTimer, RTIMER_NOW() + INTER_PACKET_DEADLINE, 1, hibernate, NULL);

  return;
}

PROCESS_THREAD(wait_to_send_process, ev, data) {
  PROCESS_BEGIN();

//  static struct rdc_buf_list *curr;
//  static struct rdc_buf_list *next;
  static int ret;

  static int old_packet;
  //static uint16_t *pktSeqno;

  //if (data != NULL) {
  //  curr_ = data;
  //}

  old_packet = 0;

    do {
      PROCESS_WAIT_EVENT_UNTIL(sink_is_awake && ev == PROCESS_EVENT_CONTINUE);

      rtimer_unset();

      /* if we've tried this packet already and the tx count is 0, 
         then we must got an ack, so we ready a new packet */
      if (old_packet && tx_counter == 0) {
        if (next_packet != NULL) {
          curr_packet = next_packet;
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
      next_packet = list_item_next(curr_packet);
      queuebuf_to_packetbuf(curr_packet->buf);



      ret = send_packet();
#if TRACE_ON
      if (tx_counter <= 1) {
        TRACE("%lu DATA 0 %u\n", (unsigned long)clock_time(), packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO));
      }
      else {
        TRACE("%lu RETRY 0 %u\n", (unsigned long)clock_time(), packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO));
      }
#endif
      /* Also copy to backup buf so we can do send callback after ack */
      //packetbuf_copyto(_backupPacketbuf);
      //_backupPacketbufLen = packetbuf_totlen();

      if (ret == MAC_TX_ERR_FATAL || ret == MAC_TX_ERR) {
        //pktSeqno = (uint16_t *)_ptr;
        //*pktSeqno = packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO);
        mac_call_sent_callback(_sent_callback, _ptr, MAC_TX_ERR_FATAL, 1);
        off(0);
        break;
      }

      packetbuf_locked = 0;

      rtimer_set(&_offTimer, RTIMER_NOW() + INTER_PACKET_DEADLINE, 1, retry_packet, NULL);
      
    } while (1);

  hibernate(NULL, NULL);
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
#if LOG_DELAY
  memb_init(&delay_info_memb);
#endif
  memb_init(&rss_sample_memb);
  memb_init(&window_estimate_memb);
  /* Call hibernate to clear all flags and whatnot */
  hibernate(NULL, NULL);
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

#if LOG_DELAY
  struct delay_info *dinfo;
#endif

  //clock_time_t next_wakeup;

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
  //sending_burst = 1;
 
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

  _sent_callback = sent_callback;
  _ptr = ptr;

  
  /* If we're waiting for extra beacons, or if we're already in the wait state,
     we want to follow the existing wakeup schedule */
  if (current_state == BLADEMAC_STATE_HIBERNATION && !estimating_period) {
    wake_up(NULL, NULL);
  }
  current_state = BLADEMAC_STATE_WAIT;
  //process_start(&wait_to_send_process, buf_list);
  curr_packet = buf_list;
  process_start(&wait_to_send_process, NULL);
 
  
  return;
}

static void input(void) {
  int ret;
  uint8_t dataSeqno;
  //uint16_t *ackSeqno;
  uint16_t pending;
  static uint32_t temp_time;
  rtimer_clock_t rtimer_now;
  packetbuf_attr_t curr_rss;
  struct rss_sample *sample;
#if LOG_DELAY
  struct delay_info * dinfo;
#endif

  clock_time_t now;

  rtimer_now = RTIMER_NOW();
  now = clock_time();

  //LIM_PRINTF("input: Handed packet from radio w/ RSSI %d.\n", packetbuf_attr(PACKETBUF_ATTR_RSSI));

  /* Check if the packetbuf is locked, but I think we need to go ahead anyway */
  if (packetbuf_locked) {
    PRINTF("input: Receive might be interrupting another task.\n");
  }
  packetbuf_locked = 1;
  curr_rss = packetbuf_attr(PACKETBUF_ATTR_RSSI);

  if (NETSTACK_FRAMER.parse() < 0) {
    LIM_PRINTF("input: Framer failed to parse packet.\n");
    packetbuf_locked = 0;
    //rtimer_set(&_offTimer, RTIMER_NOW() + INTER_PACKET_DEADLINE, 1, hibernate, NULL);
    return;
  }

  /* Stay awake for now - listen for another possible packet */
  rtimer_unset();

  switch (packetbuf_attr(PACKETBUF_ATTR_PACKET_TYPE)) {
    case PACKETBUF_ATTR_PACKET_TYPE_BEACON:
      packetbuf_locked = 0;
      last_known_beacon = rtimer_now;

      LIM_PRINTF("input: It's a beacon! current state %d\n", current_state);
      TRACE("%lu BEACON_RECEIVED %d\n", (unsigned long)now, curr_rss);

      if (current_state == BLADEMAC_STATE_WAIT) {
        if (curr_rss >= FAVORABLE_THRESHOLD) {
          current_state = BLADEMAC_STATE_SEND;
        }
        else if (prev_beacon_seen && prev_beacon_rss > curr_rss) {
          PRINTF("input: panic mode - send data now!\n");
          current_state = BLADEMAC_STATE_SEND;
        }
        else if (!prev_beacon_seen || prev_beacon_rss <= curr_rss) {
          nap();
        }
      }
      prev_beacon_rss = curr_rss;
      prev_beacon_seen = 1;
      beacon_phase_known = 1;

      sample = memb_alloc(&rss_sample_memb);
      if (sample != NULL) {
        sample->timestamp = now;
        sample->rss = curr_rss;
        //sample->packet_type = PACKETBUF_ATTR_PACKET_TYPE_BEACON;
        if (current_state == BLADEMAC_STATE_SEND) {
          list_add(ack_rss_sample_list, sample);
        }
        else {
          list_add(beacon_rss_sample_list, sample);
        }
      }
      else {
        PRINTF("input: failed to alloc beacon rss sample\n");
      }

      /*if (estimating_period && beacon_gap_seen) {
        period_estimate = now - last_known_rendezvous;
        PRINTF("input: estimated period of %d\n", (int)period_estimate);
        estimating_period = 0;
        beacon_gap_seen = 0;
        last_known_rendezvous = now;
      }*/
      if (current_state == BLADEMAC_STATE_SEND) {
        sending_burst = 1;
        sink_is_awake = 1;
        if (!estimating_period) {
          last_known_rendezvous = now;
        }
        PRINTF("beacon received, process running: %d\n", process_is_running(&wait_to_send_process));
        process_post_synch(&wait_to_send_process, PROCESS_EVENT_CONTINUE, NULL);
      }
      else if (estimating_period) {
        PRINTF("input: Napping for extra beacons\n");
        nap();
      }
      break;
    case PACKETBUF_ATTR_PACKET_TYPE_DATA:
      if (!IS_SINK) {
        return;
      }
      dataSeqno = (uint8_t)packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO);
      pending = packetbuf_attr(PACKETBUF_ATTR_PENDING);
      PRINTF("input: It's data! seqno %d\n", dataSeqno);
      TRACE("%lu DATA_RECEIVED %d %u\n", (unsigned long)now, packetbuf_attr(PACKETBUF_ATTR_RSSI), dataSeqno);

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
      packetbuf_set_attr(PACKETBUF_ATTR_PENDING, pending);
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
      //if (!pending) {
        
      //}

      packetbuf_copyfrom(_backupPacketbuf, _backupPacketbufLen);
      //_backupPacketbufLen = 0;

      NETSTACK_MAC.input();


      temp_time = RTIMER_NOW() + INTER_PACKET_DEADLINE + INTER_PACKET_DEADLINE + INTER_PACKET_DEADLINE;
      LIM_PRINTF("temp_time %lu, %u, %d, %u\n", temp_time, RTIMER_NOW(), INTER_PACKET_DEADLINE, RTIMER_SECOND);
      temp_time %= RTIMER_MAX_TICKS;
      rtimer_set(&_offTimer, (rtimer_clock_t)temp_time, 1, hibernate, NULL);

      packetbuf_locked = 0;
      break;
    case PACKETBUF_ATTR_PACKET_TYPE_ACK:
      PRINTF("input: It's an ack! _ptr is %d\n", (int)_ptr);
      if (current_state != BLADEMAC_STATE_SEND) {
        LIM_PRINTF("ack discarded\n");
        packetbuf_locked = 0;
        sending_burst = 0;
        wake_up(NULL, NULL);
        return;
      }
      dataSeqno = (uint8_t)packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO);
      TRACE("%lu ACK_RECEIVED %d %u\n", (unsigned long)now, packetbuf_attr(PACKETBUF_ATTR_RSSI), dataSeqno);
      if (!estimating_period) {
        sample = memb_alloc(&rss_sample_memb);
        if (sample != NULL) {
          sample->timestamp = now;
          sample->rss = curr_rss;
          //sample->packet_type = PACKETBUF_ATTR_PACKET_TYPE_ACK;
          list_add(ack_rss_sample_list, sample);
        }
        else {
          PRINTF("input: failed to alloc ack rss sample\n");
        }
      }

      
      //packetbuf_copyfrom(_backupPacketbuf, _backupPacketbufLen);
      queuebuf_to_packetbuf(curr_packet->buf);

      if (dataSeqno != packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO)) {
        LIM_PRINTF("input: Wrong seqno in ack, discarding. %d, %d\n", dataSeqno, packetbuf_attr(PACKETBUF_ATTR_MAC_SEQNO));
        packetbuf_locked = 0;
        process_post_synch(&wait_to_send_process, PROCESS_EVENT_CONTINUE, NULL);
        return;
      }

      mac_call_sent_callback(_sent_callback, _ptr, MAC_TX_OK, 1);
      
      packetbuf_locked = 0;
      tx_counter = 0;

      

      if (!packetbuf_attr(PACKETBUF_ATTR_PENDING)) {
        if (current_state != BLADEMAC_STATE_SEND) {
          LIM_PRINTF("final ack discarded\n");
          return;
        }
        LIM_PRINTF("final ack received\n");
        process_exit(&wait_to_send_process);
        current_state = BLADEMAC_STATE_HIBERNATION;
        sending_burst = 0;
        //if (period_estimate == 0) {
        if (USE_EXTRA_BEACONS && !estimating_period) {
          estimating_period = 1;
          //beacon_intervals_napped = 0;
          nap();
        }
        else if (estimating_period) {
          nap();
        }
        else {
          hibernate(NULL, NULL);
        }
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
      } while (dinfo != NULL);
#endif
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
    LIM_PRINTF("on: Source, doing nothing.\n");
    current_state = BLADEMAC_STATE_HIBERNATION;
    return 1;
  }
  return 0;
}

/* If sink, stop regular beaconing (i.e. stop beacon timer).
   If source, just turn off radio. */
static int off(int keep_radio_on) {
  sink_is_beaconing = 0;
  if (!keep_radio_on) {
    hibernate(NULL, NULL);
  }
  return 1;
}

/* Return Tbeacon */
static unsigned short channel_check_interval(void) {
  return _Tbeacon;
}

const struct rdc_driver blademac_driver = {
  "blademac",
  init,
  send,
  send_list,
  input,
  on,
  off,
  channel_check_interval,
};
