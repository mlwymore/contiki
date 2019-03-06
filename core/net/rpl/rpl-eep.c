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
 *         An implementation of EEP (based on rpl-of0.c)
 *
 * \author Mat Wymore <mlwymore@iastate.edu>
 */

/**
 * \addtogroup uip6
 * @{
 */

#include "net/rpl/rpl.h"
#include "net/rpl/rpl-private.h"
#include "net/nbr-table.h"
#include "net/link-stats.h"

#define DEBUG DEBUG_NONE
#include "net/ip/uip-debug.h"

#define MAX_ACCEPTABLE_ETX LINK_STATS_ETX_DIVISOR * 8

/*---------------------------------------------------------------------------*/
static void
reset(rpl_dag_t *dag)
{
  PRINTF("RPL: Reset EEP\n");
#if RPL_CONF_OPP_ROUTING
  rpl_opp_routing_reset();
  rpl_update_forwarder_set(dag->instance);
#else
  PRINTF("RPL: warning: opportunistic routing not enabled. Set RPL_CONF_OPP_ROUTING = 1.\n");
#endif
}
/*---------------------------------------------------------------------------*/
#if RPL_WITH_DAO_ACK
static void
dao_ack_callback(rpl_parent_t *p, int status)
{
  if(status == RPL_DAO_ACK_UNABLE_TO_ADD_ROUTE_AT_ROOT) {
    return;
  }
  /* here we need to handle failed DAO's and other stuff */
  PRINTF("RPL: EEP - DAO ACK received with status: %d\n", status);
  if(status >= RPL_DAO_ACK_UNABLE_TO_ACCEPT) {
    /* punish the ETX as if this was 10 packets lost */
    link_stats_packet_sent(rpl_get_parent_lladdr(p), MAC_TX_OK, 10);
  } else if(status == RPL_DAO_ACK_TIMEOUT) { /* timeout = no ack */
    /* punish the total lack of ACK with a similar punishment */
    link_stats_packet_sent(rpl_get_parent_lladdr(p), MAC_TX_OK, 10);
  }
}
#endif /* RPL_WITH_DAO_ACK */
/*---------------------------------------------------------------------------*/
static uint16_t
parent_link_metric(rpl_parent_t *p)
{
  const struct link_stats *stats = rpl_get_parent_link_stats(p);
  return stats != NULL ? stats->etx : 0xffff;
}
/*---------------------------------------------------------------------------*/
static uint16_t
parent_path_cost(rpl_parent_t *p)
{
  if(p == NULL) {
    return 0xffff;
  }
  /* path cost upper bound: 0xffff */
  //PRINTF("RPL: Parent %u rank %u, link metric %u\n", rpl_get_parent_lladdr(p)->u8[7], p->rank, parent_link_metric(p));
  return MIN((uint32_t)p->rank + 2 * parent_link_metric(p) / LINK_STATS_ETX_DIVISOR, 0xffff);
}
/*---------------------------------------------------------------------------*/
static rpl_rank_t
rank_via_parent(rpl_parent_t *p)
{
#if RPL_CONF_OPP_ROUTING
  if(p != NULL && rpl_node_in_forwarder_set(rpl_get_parent_lladdr(p))) {
    return rpl_get_opp_routing_rank();
  } else {
    return INFINITE_RANK;
  }
#else
  if(p == NULL) {
    PRINTF("RPL: eep: null parent\n");
    return INFINITE_RANK;
  } else {
    return MIN(parent_path_cost(p), INFINITE_RANK);
  }
#endif
}
/*---------------------------------------------------------------------------*/
static int
parent_is_acceptable(rpl_parent_t *p)
{
  return parent_link_metric(p) <= MAX_ACCEPTABLE_ETX;
}
/*---------------------------------------------------------------------------*/
static int
parent_has_usable_link(rpl_parent_t *p)
{
  return parent_is_acceptable(p);
}
/*---------------------------------------------------------------------------*/
static rpl_parent_t *
best_parent(rpl_parent_t *p1, rpl_parent_t *p2)
{
  rpl_dag_t *dag;
  uint16_t p1_cost;
  uint16_t p2_cost;
  int p1_is_acceptable;
  int p2_is_acceptable;

  p1_is_acceptable = p1 != NULL && parent_is_acceptable(p1);
  p2_is_acceptable = p2 != NULL && parent_is_acceptable(p2);

  if(!p1_is_acceptable) {
    return p2_is_acceptable ? p2 : NULL;
  }
  if(!p2_is_acceptable) {
    return p1_is_acceptable ? p1 : NULL;
  }

  dag = p1->dag; /* Both parents are in the same DAG. */
  p1_cost = parent_path_cost(p1);
  p2_cost = parent_path_cost(p2);

  /* Paths costs coarse-grained (multiple of min_hoprankinc), we operate without hysteresis */
  if(p1_cost != p2_cost) {
    /* Pick parent with lowest path cost */
    return p1_cost < p2_cost ? p1 : p2;
  } else {
    /* We have a tie! */
    /* Stick to current preferred parent if possible */
    if(p1 == dag->preferred_parent || p2 == dag->preferred_parent) {
      return dag->preferred_parent;
    }
    /* None of the nodes is the current preferred parent,
     * choose parent with best link metric */
    return parent_link_metric(p1) < parent_link_metric(p2) ? p1 : p2;
  }
}
/*---------------------------------------------------------------------------*/
static rpl_dag_t *
best_dag(rpl_dag_t *d1, rpl_dag_t *d2)
{
  if(d1->grounded != d2->grounded) {
    return d1->grounded ? d1 : d2;
  }

  if(d1->preference != d2->preference) {
    return d1->preference > d2->preference ? d1 : d2;
  }

  return d1->rank < d2->rank ? d1 : d2;
}
/*---------------------------------------------------------------------------*/
static void
update_metric_container(rpl_instance_t *instance)
{
  instance->mc.type = RPL_DAG_MC_NONE;
}
#if RPL_CONF_OPP_ROUTING
/*---------------------------------------------------------------------------*/
static rpl_rank_t
rank_via_set(rpl_forwarder_set_member_t *forwarder_set)
{
  rpl_forwarder_set_member_t *curr;
  rpl_forwarder_set_member_t *next;
  rpl_rank_t rank = 0;
  uint8_t num_forwarders = 0;

  if(forwarder_set == NULL) {
    return 0;
  }

  next = forwarder_set;
  do {
    curr = next;
    num_forwarders++;
    next = list_item_next(curr);

    rank += parent_path_cost(curr->forwarder);
  } while(next != NULL);

  rank = rank / num_forwarders;
  //PRINTF("RPL: num forwarders %u with avg rank %u\n", num_forwarders, rank);
  rank += (1000 * NETSTACK_RDC.channel_check_interval() / CLOCK_SECOND) / NETSTACK_CONF_RDC_FRAME_DURATION / (num_forwarders + 1);
  //PRINTF("RPL: my rank %u\n", rank);
  return rank;
}
#endif
/*---------------------------------------------------------------------------*/
rpl_of_t rpl_eep = {
  reset,
#if RPL_WITH_DAO_ACK
  dao_ack_callback,
#endif
  parent_link_metric,
  parent_has_usable_link,
  parent_path_cost,
  rank_via_parent,
  best_parent,
  best_dag,
  update_metric_container,
#if RPL_CONF_OPP_ROUTING
  rank_via_set,
#endif
  RPL_OCP_EEP
};

/** @}*/
