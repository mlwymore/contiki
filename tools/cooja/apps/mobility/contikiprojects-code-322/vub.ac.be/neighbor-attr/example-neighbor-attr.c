/*
 * Copyright (c) 2009, Vrije Universiteit Brussel
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
 *
 * Author: Joris Borms <joris.borms@vub.ac.be>
 *
 */

#include "contiki.h"

#include "neighbor-attr.h"

PROCESS(example_neighbor_attr_process,"neighbor attributes example");
AUTOSTART_PROCESSES(&example_neighbor_attr_process);
/*---------------------------------------------------------------------------*/
/* alternatively, you could put both properties in a struct */
NEIGHBOR_ATTRIBUTE(clock_time_t, discovery_time, NULL);
NEIGHBOR_ATTRIBUTE(clock_time_t, last_comm_time, NULL);

void
broadcast_recv(struct broadcast_conn* conn, rimeaddr_t* from)
{
  clock_time_t now = clock_time();
  clock_time_t* time = neighbor_attr_get_data(&discovery_time, from);
  if ((time == NULL) || (*time == 0)){
    neighbor_attr_set_data(&discovery_time, from, &now);
  }
  neighbor_attr_set_data(&last_comm_time, from, &now);
}

struct broadcast_callbacks bcb = {broadcast_recv};
struct broadcast_conn bconn;
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(example_neighbor_attr_process, ev, data)
{
  static struct etimer et;

  PROCESS_BEGIN();

  neighbor_attr_register(&discovery_time);
  neighbor_attr_register(&last_comm_time);

  broadcast_open(&bconn, 0xAFDC, &bcb);

  while (1){
    etimer_set(&et, CLOCK_SECOND * 5 + ((uint16_t)rand()) % (CLOCK_SECOND * 5));
    PROCESS_YIELD_UNTIL(etimer_expired(&et));

    struct neighbor_addr* n = neighbor_attr_list_neighbors();
    while (n != NULL){
      printf("neighbor %u.%u discovered at time %u, last heard %u \n",
          n->addr.u8[0], n->addr.u8[1],
          *(clock_time_t*)neighbor_attr_get_data(&discovery_time, &n->addr),
          *(clock_time_t*)neighbor_attr_get_data(&last_comm_time, &n->addr));
      n = n->next;
    }

    packetbuf_clear();
    packetbuf_copyfrom("hello!",6);
    broadcast_send(&bconn);

  }

  PROCESS_END();
}
