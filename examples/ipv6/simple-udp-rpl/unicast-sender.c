/*
 * Copyright (c) 2011, Swedish Institute of Computer Science.
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

#include "contiki.h"
#include "lib/random.h"
#include "sys/ctimer.h"
#include "sys/etimer.h"
#include "net/ip/uip.h"
#include "net/ipv6/uip-ds6.h"
#include "net/ip/uip-debug.h"

#include "sys/node-id.h"

#include "simple-udp.h"
#include "servreg-hack.h"

#include <stdio.h>
#include <string.h>

#define UDP_PORT 1234
#define SERVICE_ID_BASE 190

#define SEND_INTERVAL		(CLOCK_SECOND / 2)
#define SEND_TIME		(SEND_INTERVAL + (random_rand() % CLOCK_SECOND))

#define NUM_PACKETS_TO_SEND 600

static struct simple_udp_connection unicast_connection;

/*---------------------------------------------------------------------------*/
PROCESS(unicast_sender_process, "Unicast sender example process");
AUTOSTART_PROCESSES(&unicast_sender_process);
/*---------------------------------------------------------------------------*/
static void
receiver(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
  printf("Data received on port %d from port %d with length %d\n",
         receiver_port, sender_port, datalen);
}
/*---------------------------------------------------------------------------*/
static void
set_global_address(uip_ipaddr_t *ipaddr)
{
  int i;
  uint8_t state;

  uip_ip6addr(ipaddr, UIP_DS6_DEFAULT_PREFIX, 0, 0, 0, 0, 0, 0, 0);
  uip_ds6_set_addr_iid(ipaddr, &uip_lladdr);
  uip_ds6_addr_add(ipaddr, 0, ADDR_AUTOCONF);

  printf("IPv6 addresses: ");
  for(i = 0; i < UIP_DS6_ADDR_NB; i++) {
    state = uip_ds6_if.addr_list[i].state;
    if(uip_ds6_if.addr_list[i].isused &&
       (state == ADDR_TENTATIVE || state == ADDR_PREFERRED)) {
      uip_debug_ipaddr_print(&uip_ds6_if.addr_list[i].ipaddr);
      printf("\n");
    }
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(unicast_sender_process, ev, data)
{
  static struct etimer periodic_timer;
  static struct etimer send_timer;
  //uip_ipaddr_t *addr;
  static uip_ipaddr_t addr;
  static uint8_t service_id;
  static unsigned int message_number = 0;

  PROCESS_BEGIN();

  //servreg_hack_init();

  set_global_address(&addr);
        uip_debug_ipaddr_print(&addr);
  uip_ipaddr_copy(&addr, &uip_ds6_get_link_local(1)->ipaddr);
  addr.u8[15] -= 1;
        uip_debug_ipaddr_print(&addr);

  simple_udp_register(&unicast_connection, UDP_PORT,
                      NULL, UDP_PORT, receiver);
                      
  service_id = SERVICE_ID_BASE + uip_lladdr.addr[7] - 1;
  printf("Service ID: %d\n", service_id);

  //etimer_set(&periodic_timer, SEND_INTERVAL);
  etimer_set(&send_timer, 10*CLOCK_SECOND);
  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&send_timer));
  while(1) {

    //PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
    //etimer_reset(&periodic_timer);
    etimer_set(&send_timer, SEND_TIME);

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&send_timer));
    //addr = servreg_hack_lookup(service_id);
    //uip_gethostaddr(&addr);
    
    if(&addr != NULL) {
      char buf[20];

      printf("Sending unicast %d to ", message_number);
      uip_debug_ipaddr_print(&addr);
      printf("\n");
      printf("SEND %d\n", message_number);
      sprintf(buf, "Message %d", message_number);
      message_number++;
      simple_udp_sendto(&unicast_connection, buf, strlen(buf) + 1, &addr);
    } else {
      printf("Service %d not found\n", service_id);
    }
    if(message_number >= NUM_PACKETS_TO_SEND) {
    	printf("DONE\n");
    	break;
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
