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
 *         Implementation of opportunistic routing for RPL.
 *
 * \author Mat Wymore <mlwymore@iastate.edu>
 */

/**
 * \addtogroup uip6
 * @{
 */

#include "contiki.h"
#include "contiki-net.h"
#include "net/link-stats.h"
#include "net/nbr-table.h"
#include "net/rpl/rpl.h"
#include "net/rpl/rpl-private.h"
#include "lib/list.h"
#include "lib/memb.h"
#include "sys/ctimer.h"

#include <string.h>

#define DEBUG DEBUG_NONE
#include "net/ip/uip-debug.h"

#if RPL_CONF_OPP_ROUTING

PROCESS(rpl_forwarder_set_update_process, "forwarder set updating process");

#define RPL_MAX_FORWARDER_SET_MEMBERS NBR_TABLE_MAX_NEIGHBORS
MEMB(forwarder_set_memb, struct rpl_forwarder_set_member, RPL_MAX_FORWARDER_SET_MEMBERS);
LIST(forwarder_set_list);
LIST(current_set_list);

NBR_TABLE(uint8_t, forwarder_set);

static rpl_rank_t current_rank = INFINITE_RANK;
static volatile uint8_t we_are_root = 0;

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(rpl_forwarder_set_update_process, ev, instance)
{
  static rpl_rank_t best_rank;
  static rpl_rank_t best_forwarder_rank;
  static rpl_rank_t curr_rank;
  static rpl_forwarder_set_member_t *curr_forwarder;
  static rpl_forwarder_set_member_t *next_forwarder;
  static rpl_forwarder_set_member_t *best_forwarder;
  static rpl_forwarder_set_member_t *last_forwarder;
  static nbr_table_item_t *nbr_table_parent;
  static uint8_t *membership_status;
  PROCESS_BEGIN();
  while(1) {
    PROCESS_WAIT_EVENT();
    if(ev == PROCESS_EVENT_CONTINUE) {
      nbr_table_parent = nbr_table_head(rpl_parents);
      best_rank = 0xFFFF;
      if(nbr_table_parent != NULL) {
        list_init(forwarder_set_list);
        list_init(current_set_list);

        /* Build a list of potential forwarders */
        int is_potential_forwarder;
        PRINTF("RPL: Potential forwarders: ");
        do {
          is_potential_forwarder = 0;
          curr_forwarder = memb_alloc(&forwarder_set_memb);
          if(curr_forwarder == NULL) {
            PRINTF("RPL: unable to allocate forwarder set member\n");
            return 0;
          }
          curr_forwarder->forwarder = (rpl_parent_t *)nbr_table_parent;
          if(((rpl_instance_t *)instance)->of->parent_path_cost(curr_forwarder->forwarder) < 0xFFFF) {
            list_add(forwarder_set_list, curr_forwarder);
            PRINTF("%u ", rpl_get_parent_lladdr(curr_forwarder->forwarder)->u8[7]);
            is_potential_forwarder = 1;
          }
          membership_status = (uint8_t *)nbr_table_add_lladdr(forwarder_set, rpl_get_parent_lladdr(curr_forwarder->forwarder), 
            NBR_TABLE_REASON_UNDEFINED, NULL);
          if(membership_status == NULL) {
            PRINTF("RPL: error setting membership status in forwarder set\n");
            return 0;
          } else {
            *membership_status = 0;
          }
          if(!is_potential_forwarder) {
            memb_free(&forwarder_set_memb, curr_forwarder);
          }
          nbr_table_parent = nbr_table_next(rpl_parents, nbr_table_parent);
        } while(nbr_table_parent != NULL);
        PRINTF("\n");

        last_forwarder = NULL;
        /* Try all incremental combinations in ascending order according to rank */
        while(list_head(forwarder_set_list) != NULL) {
          next_forwarder = list_head(forwarder_set_list);
          best_forwarder = next_forwarder;
          best_forwarder_rank = ((rpl_instance_t *)instance)->of->parent_path_cost(best_forwarder->forwarder);
          /* Instead of sorting, we'll go through and find the best forwarder each time */
          do {
            curr_forwarder = next_forwarder;
            curr_rank = ((rpl_instance_t *)instance)->of->parent_path_cost(curr_forwarder->forwarder);
            if(curr_rank < best_forwarder_rank) {
              best_forwarder = curr_forwarder;
              best_forwarder_rank = curr_rank;
            }
            next_forwarder = list_item_next(curr_forwarder);
          } while(next_forwarder != NULL);
          /* Add the best forwarder to the current list and remove it from the candidate list */
          //PRINTF("RPL: adding %u with rank %u\n", rpl_get_parent_lladdr(best_forwarder->forwarder)->u8[7], best_forwarder_rank);
          list_remove(forwarder_set_list, best_forwarder);
          list_add(current_set_list, best_forwarder);

          /* Find the routing metric with the current list and compare to the best so far */
          curr_rank = ((rpl_instance_t *)instance)->of->rank_via_set(list_head(current_set_list));
          //PRINTF("RPL: resulting rank %u, current best rank %u\n", curr_rank, best_rank);
          if(curr_rank <= best_rank) {
            best_rank = curr_rank;
            last_forwarder = list_tail(current_set_list);
          }
        }

        /* Trim the list back to the best combination. */
        if(last_forwarder != NULL) {
          while(list_head(current_set_list) != NULL) {
            curr_forwarder = list_chop(current_set_list);
            if(curr_forwarder == last_forwarder) {
              list_add(current_set_list, last_forwarder);
              break;
            }
            memb_free(&forwarder_set_memb, curr_forwarder);
          }
          if(list_head(current_set_list) == NULL) {
            PRINTF("RPL: error trimming forwarder set list\n");
          }

          PRINTF("RPL: best rank %u\n", best_rank);
#if DEBUG
          PRINTF("RPL: best set ");
          next_forwarder = list_head(current_set_list);
          do {
            curr_forwarder = next_forwarder;
            PRINTF("%u ", rpl_get_parent_lladdr(curr_forwarder->forwarder)->u8[7]);
            next_forwarder = list_item_next(curr_forwarder);
          } while(next_forwarder != NULL);
          PRINTF("\n");
#endif

          /* Now we have the best combination - store it in a neighbor table */
          while(list_head(current_set_list) != NULL) {
            curr_forwarder = list_pop(current_set_list);
            membership_status = nbr_table_get_from_lladdr(forwarder_set, rpl_get_parent_lladdr(curr_forwarder->forwarder));
            *membership_status = 1;
            memb_free(&forwarder_set_memb, curr_forwarder);
          }
        } else {
          PRINTF("RPL: empty forwarder set\n");
        }
      }
      PRINTF("RPL: updating opp routing rank from %d to %d\n", current_rank, best_rank);
      if(best_rank != 0xFFFF) {
        current_rank = best_rank;
      } else {
        current_rank = INFINITE_RANK;
      }
    }
  }
  PROCESS_END();
}

void
rpl_update_forwarder_set(rpl_instance_t *instance)
{
  if(!we_are_root) {
    process_post(&rpl_forwarder_set_update_process, PROCESS_EVENT_CONTINUE, instance);
  }
}

void
rpl_opp_routing_reset(void)
{
  rpl_dag_t *dag;
  dag = rpl_get_any_dag();
  if(uip_ds6_is_my_addr(&dag->dag_id)) {
    we_are_root = 1;
    current_rank = ROOT_RANK(dag->instance);
  } else {
    current_rank = INFINITE_RANK;
  }
}

rpl_rank_t
rpl_get_opp_routing_rank(void)
{
  return current_rank;
}

uint8_t
rpl_node_in_forwarder_set(const linkaddr_t *lladdr)
{
  uint8_t * val;
  val = (uint8_t *)nbr_table_get_from_lladdr(forwarder_set, lladdr);
  if(val != NULL) {
    return *val;
  }
  PRINTF("RPL: node not found in neighbor table\n");
  return 0;
}

uint8_t
rpl_is_addr_opp(uip_ipaddr_t *destipaddr)
{
  /* TODO: assumes one dag for now */
  rpl_dag_t *dag;
  dag = rpl_get_any_dag();
  return uip_ipaddr_cmp(destipaddr, &dag->dag_id);
}

void
rpl_opp_routing_init(void)
{
  nbr_table_register(forwarder_set, NULL);
  memb_init(&forwarder_set_memb);
  rpl_dag_t *dag;
  dag = rpl_get_any_dag();
  if(uip_ds6_is_my_addr(&dag->dag_id)) {
    we_are_root = 1;
    current_rank = ROOT_RANK(dag->instance);
  } else {
    process_start(&rpl_forwarder_set_update_process, NULL);
  }
}
/*---------------------------------------------------------------------------*/
#endif
/** @} */
