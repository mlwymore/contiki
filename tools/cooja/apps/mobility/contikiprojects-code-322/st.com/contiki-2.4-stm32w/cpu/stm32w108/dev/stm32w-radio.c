/*
 * Copyright (c) 2010, STMicroelectronics.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This file is part of the Contiki OS
 *
 */
/*---------------------------------------------------------------------------*/
/**
* \file
*					Machine dependent STM32W radio code.
* \author
*					Salvatore Pitrulli
* \version
*					1.1
*/
/*---------------------------------------------------------------------------*/

#include PLATFORM_HEADER
#include "hal/error.h"
#include "hal/hal.h"

#include "contiki.h"

#include "net/mac/frame802154.h"

#include "dev/stm32w-radio.h"

#include "net/rime/packetbuf.h"
#include "net/rime/rimestats.h"



#define DEBUG 0
#include "dev/leds.h"

#if DEBUG > 0
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...) do {} while (0)
#endif


#ifndef MAC_RETRIES
#define MAC_RETRIES 0
#endif

#if MAC_RETRIES > 0

 int8_t mac_retries_left;

 #define INIT_RETRY_CNT() (mac_retries_left = MAC_RETRIES)
 #define DEC_RETRY_CNT() (mac_retries_left--)
 #define RETRY_CNT_GTZ() (mac_retries_left > 0)

#else

 #define INIT_RETRY_CNT()
 #define DEC_RETRY_CNT()
 #define RETRY_CNT_GTZ() 0

#endif

const RadioTransmitConfig radioTransmitConfig = {
  TRUE,  // waitForAck;
  TRUE, // checkCca;
  4,     // ccaAttemptMax;
  2,     // backoffExponentMin;
  6,     // backoffExponentMax;
  TRUE   // appendCrc;
};

/*
 * The buffers which hold incoming data.
 */
#ifndef RADIO_RXBUFS
#define RADIO_RXBUFS 1
#endif

static uint8_t stm32w_rxbufs[RADIO_RXBUFS][STM32W_MAX_PACKET_LEN+1]; // +1 because of the first byte, which will contain the length of the packet.

#if RADIO_RXBUFS > 1
static volatile int8_t first = -1, last=0;
#else
static const int8_t first=0, last=0;
#endif

#if RADIO_RXBUFS > 1
 #define CLEAN_RXBUFS() do{first = -1; last = 0;}while(0)
 #define RXBUFS_EMPTY() (first == -1)

int RXBUFS_FULL(){

    int8_t first_tmp = first;
    return first_tmp == last;
}

#else /* RADIO_RXBUFS > 1 */
 #define CLEAN_RXBUFS() (stm32w_rxbufs[0][0] = 0)
 #define RXBUFS_EMPTY() (stm32w_rxbufs[0][0] == 0)
 #define RXBUFS_FULL() (stm32w_rxbufs[0][0] != 0)
#endif /* RADIO_RXBUFS > 1 */

int add_to_rxbuf(uint8_t * src);
int read_from_rxbuf(void * dest, unsigned short len);

static uint8_t __attribute__(( aligned(2) )) stm32w_txbuf[STM32W_MAX_PACKET_LEN+1];


#define CLEAN_TXBUF() (stm32w_txbuf[0] = 0)
#define TXBUF_EMPTY() (stm32w_txbuf[0] == 0)

#define CHECKSUM_LEN 2

/*
 * The transceiver state.
 */
#define ON     0
#define OFF    1

static volatile uint8_t onoroff = OFF;

static s8 last_rssi;

/*---------------------------------------------------------------------------*/
PROCESS(stm32w_radio_process, "STM32W radio driver");
/*---------------------------------------------------------------------------*/

static void (* receiver_callback)(const struct radio_driver *);

int stm32w_radio_on(void);
int stm32w_radio_off(void);

int stm32w_radio_read(void *buf, unsigned short bufsize);

int stm32w_radio_send(const void *data, unsigned short len);

void stm32w_radio_set_receiver(void (* recv)(const struct radio_driver *d));


const struct radio_driver stm32w_radio_driver =
  {
    stm32w_radio_send,
    stm32w_radio_read,
    stm32w_radio_set_receiver,
    stm32w_radio_on,
    stm32w_radio_off,
  };
/*---------------------------------------------------------------------------*/
void stm32w_radio_set_receiver(void (* recv)(const struct radio_driver *))
{
  receiver_callback = recv;
}
/*---------------------------------------------------------------------------*/
void stm32w_radio_init(void)
{
  
  // A channel needs also to be setted.
  ST_RadioSetChannel(RF_CHANNEL);

  // Initialize radio (analog section, digital baseband and MAC).
  // Leave radio powered up in non-promiscuous rx mode.
  ST_RadioInit(ST_RADIO_POWER_MODE_OFF);
  onoroff = OFF;
  ST_RadioSetNodeId(STM32W_NODE_ID);   // To be deleted.
  ST_RadioSetPanId(IEEE802154_PANID);
  
  ST_RadioEnableAutoAck(TRUE);   	
  
  
  CLEAN_RXBUFS();
  CLEAN_TXBUF();
  
  process_start(&stm32w_radio_process, NULL);
}
/*---------------------------------------------------------------------------*/
u8_t stm32w_radio_set_channel(u8_t channel)
{
  return ST_RadioSetChannel(channel);
}
/*---------------------------------------------------------------------------*/
int stm32w_radio_send(const void *payload, unsigned short payload_len)
{
  struct timer t;
  
  if(payload_len > STM32W_MAX_PACKET_LEN){
    PRINTF("stm32w: payload length=%d is too long.\r\n", payload_len);
    return RADIO_TX_ERR;
  }
  if(onoroff == OFF){
    PRINTF("stm32w: Radio is off!\r\n");
    return RADIO_TX_ERR;
  }
  
  /* Check if the txbuf is empty.
   *  Wait for a finite time.
   */
  timer_set(&t, CLOCK_SECOND/10);
  while(!TXBUF_EMPTY()){
    if(timer_expired(&t)){
      PRINTF("stm32w: tx buffer full.\r\n");
      return RADIO_TX_ERR;
    }
  }
  
  /* Copy to the txbuf. 
   * The first byte must be the packet length.
   */
  stm32w_txbuf[0] = payload_len + CHECKSUM_LEN;
  memcpy(stm32w_txbuf + 1, payload, payload_len);
  
  INIT_RETRY_CNT();
  
  if(ST_RadioTransmit(stm32w_txbuf)==ST_SUCCESS){
    
    ENERGEST_OFF(ENERGEST_TYPE_LISTEN);
    ENERGEST_ON(ENERGEST_TYPE_TRANSMIT);
    
    PRINTF("stm32w: sending %d bytes\r\n", payload_len);
    
#if DEBUG > 1
      for(u8_t c=1; c <= stm32w_txbuf[0]-2; c++){
        PRINTF("%x:",stm32w_txbuf[c]);
      }
      PRINTF("\r\n");
#endif   
    
    return RADIO_TX_OK;  
  }
  
  PRINTF("stm32w: transmission never started.\r\n");
  /* TODO: Do we have to retransmit? */
  
  CLEAN_TXBUF();
  
  return RADIO_TX_ERR;
  
}
/*---------------------------------------------------------------------------*/
int stm32w_radio_off(void)
{  
  /* Any transmit or receive packets in progress are aborted.
   * Waiting for end of transmission or reception have to be done.
   */
  if(onoroff == ON){
  ST_RadioSleep();
  onoroff = OFF;
    CLEAN_TXBUF();
    CLEAN_RXBUFS();  
  
  ENERGEST_OFF(ENERGEST_TYPE_LISTEN);
  }
  
  return 1;
}
/*---------------------------------------------------------------------------*/
int stm32w_radio_on(void)
{
  if(onoroff == OFF){
    ST_RadioWake();
    onoroff = ON;
  
    ENERGEST_ON(ENERGEST_TYPE_LISTEN);
  }
  
  return 1;
}

/*---------------------------------------------------------------------------*/


void ST_RadioReceiveIsrCallback(u8 *packet,
                                  boolean ackFramePendingSet,
                                  u32 time,
                                  u16 errors,
                                  s8 rssi)
{
  
  /* Copy packet into the buffer. It is better to do this here. */
  if(add_to_rxbuf(packet)){
    process_poll(&stm32w_radio_process);
    last_rssi = rssi;
  }
}


void ST_RadioTransmitCompleteIsrCallback(StStatus status,
                                           u32 txSyncTime,
                                           boolean framePending)
{

  ENERGEST_OFF(ENERGEST_TYPE_TRANSMIT); 
  ENERGEST_ON(ENERGEST_TYPE_LISTEN);
  
  if(status == ST_SUCCESS || status == ST_PHY_ACK_RECEIVED){
      CLEAN_TXBUF();
  }
  else {

      if(RETRY_CNT_GTZ()){
          // Retransmission
          if(ST_RadioTransmit(stm32w_txbuf)==ST_SUCCESS){
              
              ENERGEST_OFF(ENERGEST_TYPE_LISTEN);
              ENERGEST_ON(ENERGEST_TYPE_TRANSMIT);
               
              PRINTF("stm32w: retransmission.\r\n");
              
              DEC_RETRY_CNT();
          }
          else {
              CLEAN_TXBUF();
              PRINTF("stm32w: retransmission failed.\r\n");
          }
      }
      else {
          CLEAN_TXBUF();
      }      
  }
  
  /* Debug outputs. */
  if(status == ST_SUCCESS || status == ST_PHY_ACK_RECEIVED){
      PRINTF("TX_END");
  }
  else if (status == ST_MAC_NO_ACK_RECEIVED){
      PRINTF("TX_END_NOACK!!!");
  }
  else if (status == ST_PHY_TX_CCA_FAIL){
      PRINTF("TX_END_CCA!!!");
  }
  else if(status == ST_PHY_TX_UNDERFLOW){
      PRINTF("TX_END_UFL!!!");
  }
  else {
      PRINTF("TX_END_INCOMPL!!!");
  }
}


boolean ST_RadioDataPendingShortIdIsrCallback(int16u shortId) {
  return FALSE;
}

boolean ST_RadioDataPendingLongIdIsrCallback(int8u* longId) {
  return FALSE;
}


/*---------------------------------------------------------------------------*/
PROCESS_THREAD(stm32w_radio_process, ev, data)
{
  PROCESS_BEGIN();

  PRINTF("stm32w_radio_process: started\r\n");
  
  while(1) {
    
    PROCESS_YIELD_UNTIL(ev == PROCESS_EVENT_POLL);
    
    if(receiver_callback != NULL) {
      PRINTF("stm32w_radio_process: calling receiver callback\r\n");
      
#if DEBUG > 1
      for(u8_t c=1; c <= RCVD_PACKET_LEN; c++){
        PRINTF("%x",stm32w_rxbuf[c]);
      }
      PRINTF("\r\n");
#endif
      
      
      receiver_callback(&stm32w_radio_driver);
      if(!RXBUFS_EMPTY()){
          // Some data packet still in rx buffer (this appens because process_poll doesn't queue requests),
          // so stm32w_radio_process need to be called again.
          process_poll(&stm32w_radio_process);
      }    
    } else {
      PRINTF("stm32w_radio_process not receiving function\r\n");
      CLEAN_RXBUFS();
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
int stm32w_radio_read(void *buf, unsigned short bufsize)
{  
  return read_from_rxbuf(buf,bufsize);  
}

/*---------------------------------------------------------------------------*/
void ST_RadioOverflowIsrCallback(void)
{
  PRINTF("OVERFLOW\r\n");
}
/*---------------------------------------------------------------------------*/
void ST_RadioSfdSentIsrCallback(u32 sfdSentTime)
{
}
/*---------------------------------------------------------------------------*/
void ST_RadioMacTimerCompareIsrCallback(void)
{
}
/*---------------------------------------------------------------------------*/
int add_to_rxbuf(uint8_t * src)
{
    if(RXBUFS_FULL()){
        return 0;
    } 
    
    memcpy(stm32w_rxbufs[last], src, src[0] + 1);
#if RADIO_RXBUFS > 1
    last = (last + 1) % RADIO_RXBUFS; 
    if(first == -1){
        first = 0;
    }
#endif
    
    return 1;
}
/*---------------------------------------------------------------------------*/
int read_from_rxbuf(void * dest, unsigned short len)
{
    
    if(RXBUFS_EMPTY()){ // Buffers are all empty
        return 0;
    }        
    
    if(stm32w_rxbufs[first][0] > len){  // Too large packet for dest.
        len = 0;
    }
    else {        
        len = stm32w_rxbufs[first][0];            
        memcpy(dest,stm32w_rxbufs[first]+1,len);
        packetbuf_set_attr(PACKETBUF_ATTR_RSSI, last_rssi);
    }
    
#if RADIO_RXBUFS > 1    
    ATOMIC(
           first = (first + 1) % RADIO_RXBUFS;
           int first_tmp = first;
           if(first_tmp == last){
               CLEAN_RXBUFS();
           }
    )
#else
    CLEAN_RXBUFS();
#endif
    
    return len;
}
/*---------------------------------------------------------------------------*/
short last_packet_rssi(){
    return last_rssi;
}

