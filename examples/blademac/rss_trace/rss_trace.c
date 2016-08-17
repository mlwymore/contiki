/*
 * Copyright (c) 2007, Swedish Institute of Computer Science.
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

/*Adapted from "example-unicast.c" to read rssi from packets. Intended to be
used with nullrdc and nullmac.*/
#define MYRINTERVAL RTIMER_SECOND / 250
#include "contiki.h"
#include "net/rime/rime.h"
#include "dev/radio.h"
#include "net/netstack.h"
#include <stdio.h>

/*---------------------------------------------------------------------------*/
PROCESS(sim_p, "Learning Processes");
AUTOSTART_PROCESSES(&sim_p);
/*---------------------------------------------------------------------------*/
static void
recv_uc(struct broadcast_conn *c, const linkaddr_t *from)
{
  //unsigned long sec = clock_seconds();
  rtimer_clock_t tick = RTIMER_NOW();
  packetbuf_attr_t rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);
  //packetbuf_attr_t txpwr = packetbuf_attr(PACKETBUF_ATTR_RADIO_TXPOWER);
  //radio_value_t txpwr;
  //NETSTACK_RADIO.get_value(RADIO_PARAM_TXPOWER,&txpwr);
  //printf("%lu %lu %d\n",sec,(unsigned long)tick,rssi);
  //printf("%lu %d %d\n",(unsigned long)tick,rssi,txpwr);
  printf("RSS %lu %d\n",(unsigned long)tick,rssi);
  //printf("%d\n",rssi);
}
/*---------------------------------------------------------------------------*/
static void
sent_uc(struct broadcast_conn *c, int status, int num_tx)
{
  const linkaddr_t *dest = packetbuf_addr(PACKETBUF_ADDR_RECEIVER);
//  const linkaddr_t *src = packetbuf_addr(PACKETBUF_ADDR_SENDER);
  if(linkaddr_cmp(dest, &linkaddr_null)) {
    return;
  }
}
/*---------------------------------------------------------------------------*/
static const struct broadcast_callbacks broadcast_callbacks = {recv_uc};
static struct broadcast_conn bc;

static struct rtimer r_timer;
static linkaddr_t addr;
/*---------------------------------------------------------------------------*/
static uint8_t r_func(struct rtimer *rt, void *ptr){
  uint8_t ret;
  //linkaddr_t addr;
  //addr.u8[0] = 212;
  //addr.u8[1] = 5;
  //addr.u8[0] = 1;
  //addr.u8[1] = 0;
  //printf("My address is: %d.%d\n",linkaddr_node_addr.u8[0],linkaddr_node_addr.u8[1]);
  //if(!linkaddr_cmp(&addr, &linkaddr_node_addr)) {
    //packetbuf_set_attr(PACKETBUF_ATTR_RADIO_TXPOWER,65536);
    packetbuf_copyfrom("Hello", 5);
    broadcast_send(&bc);
    //unicast_send(&uc, &addr);
  //}

  rtimer_clock_t time_now = RTIMER_NOW();
  ret = rtimer_set(&r_timer, time_now + MYRINTERVAL, 1,
    (void (*)(struct rtimer *,void *))r_func,NULL);
  return ret;
}
/*---------------------------------------------------------------------------*/


PROCESS_THREAD(sim_p, ev, data){
  PROCESS_EXITHANDLER(broadcast_close(&bc);)
  //addr.u8[0] = 212;
  //addr.u8[1] = 5;
  addr.u8[0] = 2;
  addr.u8[1] = 0;

  PROCESS_BEGIN();

  //NETSTACK_RADIO.set_value(RADIO_PARAM_TXPOWER,-21);
  broadcast_open(&bc,146,&broadcast_callbacks);
  uint8_t ret = 0;
  if(!linkaddr_cmp(&addr, &linkaddr_node_addr)){

    rtimer_clock_t time_now = RTIMER_NOW();
    ret = rtimer_set(&r_timer, time_now + MYRINTERVAL, 1,
            (void (*)(struct rtimer *,void *))r_func,NULL);
  }

  if(ret){
    printf("Error Timer: %u\n", ret);
  }

  while(1){
    PROCESS_WAIT_EVENT_UNTIL(ev==PROCESS_EVENT_MSG);

  }
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
