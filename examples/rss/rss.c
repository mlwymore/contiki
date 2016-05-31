#include "contiki.h"
#include "lib/random.h"
#include "net/rime/rime.h"
#include "net/netstack.h"
#include "cc2420.h"
#include "cc2420_const.h"


#include <stdio.h>

//static struct collect_conn tc;

PROCESS(example_rss_process, "RSS Measurement");
AUTOSTART_PROCESSES(&example_rss_process);

static void recv(const linkaddr_t *originator, uint8_t seqno, uint8_t hops)
{
  static int16_t rss;
  static signed char rss_val;
  static signed char rss_offset;
  printf("Sink got message from %d.%d, seqno %d, hops %d: len %d '%s'\n",originator->u8[0], originator->u8[1],seqno, hops,packetbuf_datalen(), (char *)packetbuf_dataptr());
  rss_val = cc2420_last_rssi;
  rss_offset=-45;
  rss=rss_val + rss_offset;
  printf("RSSI of Last Packet Received is %d\n",rss);
}

//static const struct collect_callbacks callbacks = { recv };
//static const struct broadcast_callbacks broadcast_call = {recv};
//static struct broadcast_conn broadcast;
static const struct unicast_callbacks unicast_callbacks = { recv };
static struct unicast_conn uc;

PROCESS_THREAD(example_rss_process, ev, data)
{
  static struct etimer periodic;
  static struct etimer et;
  static uint8_t iAmSink = 0;
  
  PROCESS_EXITHANDLER(unicast_close(&uc);)
  PROCESS_BEGIN();

  //collect_open(&tc, 130, COLLECT_ROUTER, &callbacks);
  //broadcast_open(&broadcast, 129, &broadcast_call);
  unicast_open(&uc, 146, &unicast_callbacks);

  if(linkaddr_node_addr.u8[0] == 1 && linkaddr_node_addr.u8[1] == 0) 
  {
    printf("I am sink\n");
    iAmSink = 1;
    //collect_set_sink(&tc, 1);
  }

  /* Allow some time for the network to settle. */
  etimer_set(&et, 10 * CLOCK_SECOND);
  PROCESS_WAIT_UNTIL(etimer_expired(&et));

  while(1) 
  {

    /* Send a packet every 1 seconds. */
    if(etimer_expired(&periodic)) 
    {
      etimer_set(&periodic, 10 );
      //etimer_set(&et, random_rand() % (CLOCK_SECOND * 1));
    }

    PROCESS_WAIT_EVENT();


    if(etimer_expired(&periodic)) 
    {
      //static linkaddr_t oldparent;
      //const linkaddr_t *parent;
      linkaddr_t addr;
      addr.u8[0] = 1;
      addr.u8[1] = 0;
      
      if(iAmSink == 0)
      {
        printf("Sending\n");
        packetbuf_clear();
        packetbuf_set_datalen(sprintf(packetbuf_dataptr(),"%s", "Fight On") + 1);
        //collect_send(&tc, 15);
        //broadcast_send(&broadcast);
	unicast_send(&uc, &addr);
      }
    }

  } //end of while

  PROCESS_END();
} //end of process thread 
