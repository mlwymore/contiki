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
 *         Testing the broadcast layer in Rime
 * \author
 *         Adam Dunkels <adam@sics.se>
 */

#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"
#include "powertrace.h"

#include "dev/button-sensor.h"

#include "dev/leds.h"
#include "net/netstack.h"

#include <stdio.h>

static int sentCounter = 0;
static int attemptedCounter = 0;

/*---------------------------------------------------------------------------*/
PROCESS(example_broadcast_process, "Broadcast example");
AUTOSTART_PROCESSES(&example_broadcast_process);
/*---------------------------------------------------------------------------*/
static void
broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{
  //printf("message received from %d.%d: '%s'\n",
  //       from->u8[0], from->u8[1], (char *)packetbuf_dataptr());
}

static void
broadcast_sent(struct broadcast_conn *bc, int status, int num_tx)
{
  //printf("message sent\n");
  sentCounter++;
  /*if (sentCounter % 8 == 0) {
    printf("Reliability stats: %d attempted, %d sent\n", attemptedCounter, sentCounter);
  }*/
  
}

static const struct broadcast_callbacks broadcast_call = {broadcast_recv, broadcast_sent};
static struct broadcast_conn broadcast;
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(example_broadcast_process, ev, data)
{
  static struct etimer et;
  static int i = 0;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)

  PROCESS_BEGIN();

  /* Start powertracing, once every 1 seconds. */
  //powertrace_start(CLOCK_SECOND * 1);


  broadcast_open(&broadcast, 129, &broadcast_call);

  while(1) {

    /* Delay a few seconds */
    etimer_set(&et, CLOCK_SECOND * 1 + random_rand() % (CLOCK_SECOND * 1));

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    if (clock_time() < 128 * 60) {
      packetbuf_copyfrom("Hello", 6);
      for (i = 0; i < 1; i++) {
        broadcast_send(&broadcast);
        attemptedCounter++;
        //printf("message queued\n");
      }
    }
    else if (sentCounter == attemptedCounter || clock_time() > 128 * 70) {
      break;
    }
    else {
      //cheating to flush the packetqueue
      NETSTACK_MAC.on();
    }
  }

  printf("RELIABILITY %d %d\n", attemptedCounter, sentCounter);

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
