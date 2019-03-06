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
 *         Example of how the collect primitive works.
 * \author
 *         Adam Dunkels <adam@sics.se>
 */

#include "contiki.h"
#include "lib/random.h"
#include "net/rime/rime.h"
#include "dev/leds.h"
#include "dev/button-sensor.h"

#include "net/netstack.h"

#include <stdio.h>

#define NUM_PACKETS_TO_SEND 30
#define DATA_INTERVAL 10

static linkaddr_t next_hop_addr;

static struct multihop_conn tc;

struct message {
	uint16_t seqno;
	uint8_t fluff[26];
};

/*---------------------------------------------------------------------------*/
PROCESS(example_collect_process, "Test collect process");
AUTOSTART_PROCESSES(&example_collect_process);
/*---------------------------------------------------------------------------*/
static void
recv(struct multihop_conn *c, const linkaddr_t *sender, const linkaddr_t *prevhop, uint8_t hops)
{
	printf("RECEIVE %d %d\n", sender->u8[0], (((uint8_t *)packetbuf_dataptr())[1] << 8) + ((uint8_t *)packetbuf_dataptr())[0]);
}
/*---------------------------------------------------------------------------*/
static linkaddr_t *
forward(struct multihop_conn *c,
	const linkaddr_t *originator, const linkaddr_t *dest,
	const linkaddr_t *prevhop, uint8_t hops)
{
  return &next_hop_addr;
}
/*---------------------------------------------------------------------------*/
#define GRID_SIZE 7
uint8_t id_grid[GRID_SIZE][GRID_SIZE] = {
	{ 43, 44, 45, 46, 47, 48, 49 },
	{ 42, 21, 22, 23, 24, 25, 26 },
	{ 41, 20,  7,  8,  9, 10, 27 },
	{ 40, 19,  6,  1,  2, 11, 28 },
	{ 39, 18,  5,  4,  3, 12, 29 },
	{ 38, 17, 16, 15, 14, 13, 30 },
	{ 37, 36, 35, 34, 33, 32, 31 }
};
uint8_t get_grid_position(uint8_t id, uint8_t *y, uint8_t *x) {
	uint8_t row;
	uint8_t col;
	for(row = 0; row < GRID_SIZE; row++) {
		for(col = 0; col < GRID_SIZE; col++) {
			if(id_grid[row][col] == id) {
				*y = row;
				*x = col;
				return 1;
			}
		}
	}
	return 0;
}
/*---------------------------------------------------------------------------*/
static void
set_next_hop_addr(void)
{
	uint8_t my_id = linkaddr_node_addr.u8[0];
	uint8_t hop_id = 0;
	uint16_t rand = 0;
	uint8_t row;
	uint8_t col;
	uint8_t center_row = GRID_SIZE / 2;
	uint8_t center_col = GRID_SIZE / 2;
	get_grid_position(my_id, &row, &col);
	
	if(row == center_row && col == center_col) {
	
	} else if(row == center_row) {
		if(col > center_col) {
			hop_id = id_grid[row][col - 1];
		} else {
			hop_id = id_grid[row][col + 1];
		}
	} else if(col == center_col) {
		if(row > center_row) {
			hop_id = id_grid[row - 1][col];
		} else {
			hop_id = id_grid[row + 1][col];
		}
	} else {
		/* We can either get closer by one row or one column */
		if(random_rand() % 2) {
			/* Route one row closer */
			if(row > center_row) {
				hop_id = id_grid[row - 1][col];
			} else {
				hop_id = id_grid[row + 1][col];
			}
		} else {
			/* Route one column closer */
			if(col > center_col) {
				hop_id = id_grid[row][col - 1];
			} else {
				hop_id = id_grid[row][col + 1];
			}
		}
	}
	
	/*if(my_id == 1) {
	
	} else if(my_id < 9 && my_id % 2 == 0) {
		hop_id = 1;
	} else if(my_id < 9) {
		rand = random_rand() % 2;
		if(rand) {
			hop_id = my_id + 1;
		} else {
			hop_id = my_id - 1;
		}
	} else if(my_id == 9) {
		rand = random_rand() % 2;
		if(rand) {
			hop_id = 2;
		} else {
			hop_id = my_id - 1;
		}
	} else if(my_id <= 25 && my_id % 4 != 1) {
		
	}*/
	next_hop_addr.u8[1] = 0;
	next_hop_addr.u8[0] = hop_id;
}
/*---------------------------------------------------------------------------*/
static const struct multihop_callbacks callbacks = { recv, forward };
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(example_collect_process, ev, data)
{
  static struct etimer et;
  
  static uint16_t message_count = 0;
  static struct message msg;
  
  static uint8_t i_am_sender = 0;

  PROCESS_BEGIN();

  multihop_open(&tc, 130, &callbacks);

  if(linkaddr_node_addr.u8[0] == 1 &&
     linkaddr_node_addr.u8[1] == 0) {
    printf("I am sink\n");
  } else {
  	i_am_sender = 1;
  	set_next_hop_addr();
  }

  /* Allow some time for the network to settle. */
  etimer_set(&et, 10 * CLOCK_SECOND);
  PROCESS_WAIT_UNTIL(etimer_expired(&et));

  while(1) {

    etimer_set(&et, (CLOCK_SECOND * DATA_INTERVAL) - CLOCK_SECOND / 2 + random_rand() % CLOCK_SECOND);

    PROCESS_WAIT_UNTIL(etimer_expired(&et));

    if(i_am_sender) {
    	linkaddr_t to;
    	to.u8[0] = 1;
    	to.u8[1] = 0;
			if(message_count++ < NUM_PACKETS_TO_SEND) {
				msg.seqno = message_count;
				packetbuf_copyfrom(&msg, sizeof(struct message));
				printf("SEND %d %d\n", linkaddr_node_addr.u8[0], message_count);
				multihop_send(&tc, &to);
			} else {
				printf("DONE\n");
			}
		}
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
