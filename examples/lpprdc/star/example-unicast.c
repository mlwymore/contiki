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

/**
 * \file
 *         Best-effort single-hop unicast example
 * \author
 *         Adam Dunkels <adam@sics.se>
 */

#include "contiki.h"
#include "net/rime/rime.h"
#include <stdio.h>
#include "lib/random.h"

#define NUM_PACKETS_TO_SEND 300
#define DATA_INTERVAL 1

struct message {
	uint16_t seqno;
	uint8_t fluff[26];
};

/*---------------------------------------------------------------------------*/
PROCESS(example_unicast_process, "Example unicast");
AUTOSTART_PROCESSES(&example_unicast_process);
/*---------------------------------------------------------------------------*/
static void
recv_uc(struct unicast_conn *c, const linkaddr_t *from)
{
  printf("RECEIVE %d %d\n", from->u8[0], (((uint8_t *)packetbuf_dataptr())[1] << 8) + ((uint8_t *)packetbuf_dataptr())[0]);
}
/*---------------------------------------------------------------------------*/
static void
sent_uc(struct unicast_conn *c, int status, int num_tx)
{
  const linkaddr_t *dest = packetbuf_addr(PACKETBUF_ADDR_RECEIVER);
  if(linkaddr_cmp(dest, &linkaddr_null)) {
    return;
  }
  printf("unicast complete to %d.%d: status %d num_tx %d\n",
    dest->u8[0], dest->u8[1], status, num_tx);
}
/*---------------------------------------------------------------------------*/
static const struct unicast_callbacks unicast_callbacks = {recv_uc, sent_uc};
static struct unicast_conn uc;
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(example_unicast_process, ev, data)
{
  PROCESS_EXITHANDLER(unicast_close(&uc);)
    
  PROCESS_BEGIN();

  unicast_open(&uc, 146, &unicast_callbacks);
  
  static linkaddr_t addr;
  static uint8_t i_am_sender = 0;
  static struct etimer et;
  
  i_am_sender = linkaddr_node_addr.u8[0] == 1 ? 0 : 1;
  linkaddr_copy(&addr, &linkaddr_node_addr);
  addr.u8[0] = 1;
  
  etimer_set(&et, 10*CLOCK_SECOND);

  while(1) {
    static uint16_t message_count = 0;
    static struct message msg;
    
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    uint16_t rand = random_rand() % CLOCK_SECOND;

    etimer_set(&et, CLOCK_SECOND * DATA_INTERVAL - CLOCK_SECOND/2 + rand);
    
    if(i_am_sender) {
		  if(message_count++ < NUM_PACKETS_TO_SEND) {
		  	msg.seqno = message_count;
				packetbuf_copyfrom(&msg, sizeof(struct message));
				printf("SEND %d %d\n", linkaddr_node_addr.u8[0], message_count);
			  unicast_send(&uc, &addr);
			} else {
				printf("DONE\n");
			}
		}

  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
