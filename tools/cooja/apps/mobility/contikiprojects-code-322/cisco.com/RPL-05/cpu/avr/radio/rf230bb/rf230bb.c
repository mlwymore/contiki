/*
 * Copyright (c) 2007, Swedish Institute of Computer Science
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
 * @(#)$Id: rf230bb.c,v 1.9 2010/03/02 16:29:59 dak664 Exp $
 */
/*
 * This code is almost device independent and should be easy to port.
 * Ported to Atmel RF230 21Feb2010 by dak
 */

#include <stdio.h>
#include <string.h>

#include "contiki.h"

#if defined(__AVR__)
#include <avr/io.h>
#include <util/delay.h>
//#include <avr/pgmspace.h>
#elif defined(__MSP430__)
#include <io.h>
#endif

#include "dev/leds.h"
#include "dev/spi.h"
#include "rf230bb.h"

#include "net/rime/packetbuf.h"
#include "net/rime/rimestats.h"
#include "net/netstack.h"

#if JACKDAW
#include "sicslow_ethernet.h"
#include "uip.h"
#include "uip_arp.h" //For ethernet header structure
#define ETHBUF(x) ((struct uip_eth_hdr *)x)
#define UIP_IP_BUF   ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
  /* 6lowpan max size + ethernet header size + 1 */
static uint8_t raw_buf[127+ UIP_LLH_LEN +1];
extern uint8_t mac_createEthernetAddr(uint8_t * ethernet, uip_lladdr_t * lowpan);
extern rimeaddr_t macLongAddr;
#include "rndis/rndis_task.h"
#endif


#include "sys/timetable.h"

#define WITH_SEND_CCA 0

#if RF230_CONF_TIMESTAMPS
#include "net/rime/timesynch.h"
#define TIMESTAMP_LEN 3
#else /* RF230_CONF_TIMESTAMPS */
#define TIMESTAMP_LEN 0
#endif /* RF230_CONF_TIMESTAMPS */
//#define FOOTER_LEN 2
#define FOOTER_LEN 0

#ifndef RF230_CONF_CHECKSUM
#define RF230_CONF_CHECKSUM 0
#endif /* RF230_CONF_CHECKSUM */

#ifndef RF230_CONF_AUTOACK
#define RF230_CONF_AUTOACK 1
#endif /* RF230_CONF_AUTOACK */

#ifndef RF230_CONF_AUTORETRIES
#define RF230_CONF_AUTORETRIES 2
#endif /* RF230_CONF_AUTOACK */

//Automatic and manual CRC both append 2 bytes to packets 
#if RF230_CONF_CHECKSUM
#include "lib/crc16.h"
#define CHECKSUM_LEN 2
#else
#define CHECKSUM_LEN 2
#endif /* RF230_CONF_CHECKSUM */

#define AUX_LEN (CHECKSUM_LEN + TIMESTAMP_LEN + FOOTER_LEN)

struct timestamp {
  uint16_t time;
  uint8_t authority_level;
};

#define FOOTER1_CRC_OK      0x80
#define FOOTER1_CORRELATION 0x7f

/* Leave radio on for testing low power protocols */
#if JACKDAW
#define RADIOALWAYSON 1
#else
#define RADIOALWAYSON 1
#endif

#define DEBUG 0
#if DEBUG
#define PRINTF(...) //printf(__VA_ARGS__)
#define PRINTSHORT(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#define PRINTSHORT(...) //printf(__VA_ARGS__)
#endif

/* See clock.c and httpd-cgi.c for RADIOSTATS code */
#if WEBSERVER
#define RADIOSTATS 1
#endif
#if RADIOSTATS
uint8_t RF230_rsigsi;
uint16_t RF230_sendpackets,RF230_receivepackets,RF230_sendfail,RF230_receivefail;
#endif

/* Track flow through driver, see contiki-raven-main.c for example of use */
#define DEBUGFLOWSIZE 64
#if DEBUGFLOWSIZE
uint8_t debugflowsize,debugflow[DEBUGFLOWSIZE];
#define DEBUGFLOW(c) if (debugflowsize<(DEBUGFLOWSIZE-1)) debugflow[debugflowsize++]=c
#else
#define DEBUGFLOW(c)
#endif

/* XXX hack: these will be made as Chameleon packet attributes */
rtimer_clock_t rf230_time_of_arrival, rf230_time_of_departure;

int rf230_authority_level_of_sender;

#if RF230_CONF_TIMESTAMPS
static rtimer_clock_t setup_time_for_transmission;
static unsigned long total_time_for_transmission, total_transmission_len;
static int num_transmissions;
#endif /* RF230_CONF_TIMESTAMPS */

int rf230_packets_seen, rf230_packets_read;

static uint8_t volatile pending;

/* RF230 hardware delay times, from datasheet */
typedef enum{
    TIME_TO_ENTER_P_ON               = 510, /**<  Transition time from VCC is applied to P_ON - most favorable case! */
    TIME_P_ON_TO_TRX_OFF             = 510, /**<  Transition time from P_ON to TRX_OFF. */
    TIME_SLEEP_TO_TRX_OFF            = 880, /**<  Transition time from SLEEP to TRX_OFF. */
    TIME_RESET                       = 6,   /**<  Time to hold the RST pin low during reset */
    TIME_ED_MEASUREMENT              = 140, /**<  Time it takes to do a ED measurement. */
    TIME_CCA                         = 140, /**<  Time it takes to do a CCA. */
    TIME_PLL_LOCK                    = 150, /**<  Maximum time it should take for the PLL to lock. */
    TIME_FTN_TUNING                  = 25,  /**<  Maximum time it should take to do the filter tuning. */
    TIME_NOCLK_TO_WAKE               = 6,   /**<  Transition time from *_NOCLK to being awake. */
    TIME_CMD_FORCE_TRX_OFF           = 1,   /**<  Time it takes to execute the FORCE_TRX_OFF command. */
    TIME_TRX_OFF_TO_PLL_ACTIVE       = 180, /**<  Transition time from TRX_OFF to: RX_ON, PLL_ON, TX_ARET_ON and RX_AACK_ON. */
    TIME_STATE_TRANSITION_PLL_ACTIVE = 1,   /**<  Transition time from PLL active state to another. */
}radio_trx_timing_t;
/*---------------------------------------------------------------------------*/
PROCESS(rf230_process, "RF230 driver");
/*---------------------------------------------------------------------------*/

int rf230_on(void);
int rf230_off(void);

static int rf230_read(void *buf, unsigned short bufsize);

static int rf230_prepare(const void *data, unsigned short len);
static int rf230_transmit(unsigned short len);
static int rf230_send(const void *data, unsigned short len);

static int rf230_receiving_packet(void);
static int pending_packet(void);
static int rf230_cca(void);

signed char rf230_last_rssi;
uint8_t rf230_last_correlation;

const struct radio_driver rf230_driver =
  {
    rf230_init,
    rf230_prepare,
    rf230_transmit,
    rf230_send,
    rf230_read,
    rf230_cca,
    rf230_receiving_packet,
    pending_packet,
    rf230_on,
    rf230_off
  };

uint8_t RF230_receive_on,RF230_sleeping;
static int channel;

/* Received frames are buffered to rxframe in the interrupt routine in hal.c */
hal_rx_frame_t rxframe;

/*----------------------------------------------------------------------------*/
/** \brief  This function return the Radio Transceivers current state.
 *
 *  \retval     P_ON               When the external supply voltage (VDD) is
 *                                 first supplied to the transceiver IC, the
 *                                 system is in the P_ON (Poweron) mode.
 *  \retval     BUSY_RX            The radio transceiver is busy receiving a
 *                                 frame.
 *  \retval     BUSY_TX            The radio transceiver is busy transmitting a
 *                                 frame.
 *  \retval     RX_ON              The RX_ON mode enables the analog and digital
 *                                 receiver blocks and the PLL frequency
 *                                 synthesizer.
 *  \retval     TRX_OFF            In this mode, the SPI module and crystal
 *                                 oscillator are active.
 *  \retval     PLL_ON             Entering the PLL_ON mode from TRX_OFF will
 *                                 first enable the analog voltage regulator. The
 *                                 transceiver is ready to transmit a frame.
 *  \retval     BUSY_RX_AACK       The radio was in RX_AACK_ON mode and received
 *                                 the Start of Frame Delimiter (SFD). State
 *                                 transition to BUSY_RX_AACK is done if the SFD
 *                                 is valid.
 *  \retval     BUSY_TX_ARET       The radio transceiver is busy handling the
 *                                 auto retry mechanism.
 *  \retval     RX_AACK_ON         The auto acknowledge mode of the radio is
 *                                 enabled and it is waiting for an incomming
 *                                 frame.
 *  \retval     TX_ARET_ON         The auto retry mechanism is enabled and the
 *                                 radio transceiver is waiting for the user to
 *                                 send the TX_START command.
 *  \retval     RX_ON_NOCLK        The radio transceiver is listening for
 *                                 incomming frames, but the CLKM is disabled so
 *                                 that the controller could be sleeping.
 *                                 However, this is only true if the controller
 *                                 is run from the clock output of the radio.
 *  \retval     RX_AACK_ON_NOCLK   Same as the RX_ON_NOCLK state, but with the
 *                                 auto acknowledge module turned on.
 *  \retval     BUSY_RX_AACK_NOCLK Same as BUSY_RX_AACK, but the controller
 *                                 could be sleeping since the CLKM pin is
 *                                 disabled.
 *  \retval     STATE_TRANSITION   The radio transceiver's state machine is in
 *                                 transition between two states.
 */
static uint8_t
radio_get_trx_state(void)
{
    return hal_subregister_read(SR_TRX_STATUS);
}

/*----------------------------------------------------------------------------*/
/** \brief  This function checks if the radio transceiver is sleeping.
 *
 *  \retval     true    The radio transceiver is in SLEEP or one of the *_NOCLK
 *                      states.
 *  \retval     false   The radio transceiver is not sleeping.
 */
static bool radio_is_sleeping(void)
{
    bool sleeping = false;

    /* The radio transceiver will be at SLEEP or one of the *_NOCLK states only if */
    /* the SLP_TR pin is high. */
    if (hal_get_slptr() != 0){
        sleeping = true;
    }

    return sleeping;
}

/*----------------------------------------------------------------------------*/
/** \brief  This function will reset the state machine (to TRX_OFF) from any of
 *          its states, except for the SLEEP state.
 */
static void
radio_reset_state_machine(void)
{
    hal_set_slptr_low();
    delay_us(TIME_NOCLK_TO_WAKE);
    hal_subregister_write(SR_TRX_CMD, CMD_FORCE_TRX_OFF);
    delay_us(TIME_CMD_FORCE_TRX_OFF);
}
/*---------------------------------------------------------------------------*/
static char
rf230_isidle(void)
{
  uint8_t radio_state;
  radio_state = hal_subregister_read(SR_TRX_STATUS);
  if (radio_state != BUSY_TX_ARET &&
      radio_state != BUSY_RX_AACK &&
      radio_state != STATE_TRANSITION &&
      radio_state != BUSY_RX && 
      radio_state != BUSY_TX) {
    return(1);
  } else {
//    printf(".%u",radio_state);
    return(0);
  }
}
  
static void
rf230_waitidle(void)
{
  while (1) {
    if (rf230_isidle()) break;
  }
}

/*----------------------------------------------------------------------------*/
/** \brief  This function will change the current state of the radio
 *          transceiver's internal state machine.
 *
 *  \param     new_state        Here is a list of possible states:
 *             - RX_ON        Requested transition to RX_ON state.
 *             - TRX_OFF      Requested transition to TRX_OFF state.
 *             - PLL_ON       Requested transition to PLL_ON state.
 *             - RX_AACK_ON   Requested transition to RX_AACK_ON state.
 *             - TX_ARET_ON   Requested transition to TX_ARET_ON state.
 *
 *  \retval    RADIO_SUCCESS          Requested state transition completed
 *                                  successfully.
 *  \retval    RADIO_INVALID_ARGUMENT Supplied function parameter out of bounds.
 *  \retval    RADIO_WRONG_STATE      Illegal state to do transition from.
 *  \retval    RADIO_BUSY_STATE       The radio transceiver is busy.
 *  \retval    RADIO_TIMED_OUT        The state transition could not be completed
 *                                  within resonable time.
 */
static radio_status_t
radio_set_trx_state(uint8_t new_state)
{
    uint8_t original_state;

    /*Check function paramter and current state of the radio transceiver.*/
    if (!((new_state == TRX_OFF)    ||
          (new_state == RX_ON)      ||
          (new_state == PLL_ON)     ||
          (new_state == RX_AACK_ON) ||
          (new_state == TX_ARET_ON))){
        return RADIO_INVALID_ARGUMENT;
    }

    if (radio_is_sleeping() == true){
        return RADIO_WRONG_STATE;
    }

    /* Wait for radio to finish previous operation */
    rf230_waitidle();
 //   for(;;)
 //   {
        original_state = radio_get_trx_state();
  //      if (original_state != BUSY_TX_ARET &&
  //          original_state != BUSY_RX_AACK &&
  //          original_state != BUSY_RX && 
  //          original_state != BUSY_TX)
  //          break;
  //  }

    if (new_state == original_state){
        return RADIO_SUCCESS;
    }


    /* At this point it is clear that the requested new_state is: */
    /* TRX_OFF, RX_ON, PLL_ON, RX_AACK_ON or TX_ARET_ON. */

    /* The radio transceiver can be in one of the following states: */
    /* TRX_OFF, RX_ON, PLL_ON, RX_AACK_ON, TX_ARET_ON. */
    if(new_state == TRX_OFF){
        radio_reset_state_machine(); /* Go to TRX_OFF from any state. */
    } else {
        /* It is not allowed to go from RX_AACK_ON or TX_AACK_ON and directly to */
        /* TX_AACK_ON or RX_AACK_ON respectively. Need to go via RX_ON or PLL_ON. */
        if ((new_state == TX_ARET_ON) &&
            (original_state == RX_AACK_ON)){
            /* First do intermediate state transition to PLL_ON, then to TX_ARET_ON. */
            /* The final state transition to TX_ARET_ON is handled after the if-else if. */
            hal_subregister_write(SR_TRX_CMD, PLL_ON);
            delay_us(TIME_STATE_TRANSITION_PLL_ACTIVE);
        } else if ((new_state == RX_AACK_ON) &&
                 (original_state == TX_ARET_ON)){
            /* First do intermediate state transition to RX_ON, then to RX_AACK_ON. */
            /* The final state transition to RX_AACK_ON is handled after the if-else if. */
            hal_subregister_write(SR_TRX_CMD, RX_ON);
            delay_us(TIME_STATE_TRANSITION_PLL_ACTIVE);
        }

        /* Any other state transition can be done directly. */
        hal_subregister_write(SR_TRX_CMD, new_state);

        /* When the PLL is active most states can be reached in 1us. However, from */
        /* TRX_OFF the PLL needs time to activate. */
        if (original_state == TRX_OFF){
            delay_us(TIME_TRX_OFF_TO_PLL_ACTIVE);
        } else {
            delay_us(TIME_STATE_TRANSITION_PLL_ACTIVE);
        }
    } /*  end: if(new_state == TRX_OFF) ... */

    /*Verify state transition.*/
    radio_status_t set_state_status = RADIO_TIMED_OUT;

    if (radio_get_trx_state() == new_state){
        set_state_status = RADIO_SUCCESS;
    }

    return set_state_status;
}

static void
flushrx(void)
{
  rxframe.length=0;
}
/*---------------------------------------------------------------------------*/
static uint8_t locked, lock_on, lock_off;

static void
on(void)
{
  ENERGEST_ON(ENERGEST_TYPE_LISTEN);
#if JACKDAW
//blue=0  red=1 green=2 yellow=3
#define Led0_on()                   (PORTD |=  0x80)
#define Led1_on()                   (PORTD &= ~0x20)
#define Led2_on()                   (PORTE &= ~0x80)
#define Led3_on()                   (PORTE &= ~0x40)
#define Led0_off()                  (PORTD &= ~0x80)
#define Led1_off()                  (PORTD |=  0x20)
#define Led2_off()                  (PORTE |=  0x80)
#define Led3_off()                  (PORTE |=  0x40)
  Led1_on();
#else
// PRINTSHORT("o");
//  PRINTF("on\n");
#endif

  if (RF230_sleeping) {
    hal_set_slptr_low();
    delay_us(TIME_SLEEP_TO_TRX_OFF);
//  delay_us(TIME_SLEEP_TO_TRX_OFF);//extra delay for now, wake time depends on board capacitance
    RF230_sleeping=0;
  }
  rf230_waitidle();

#if RF230_CONF_AUTOACK
  radio_set_trx_state(RX_AACK_ON);
#else
  radio_set_trx_state(RX_ON);
#endif
 // flushrx();
 //  DEBUGFLOW('O');
  RF230_receive_on = 1;
}
static void
off(void)
{
 //     rtimer_set(&rt, RTIMER_NOW()+ RTIMER_ARCH_SECOND*1UL, 1,(void *) rtimercycle, NULL);
  RF230_receive_on = 0;

#if JACKDAW
  Led1_off();
#else
// PRINTSHORT("f");
#endif

//   DEBUGFLOW('F');
#if !RADIOALWAYSON
  
  /* Wait any transmission to end */
  rf230_waitidle(); 

  /* Force the device into TRX_OFF. */   
  radio_reset_state_machine();

#if 0            //optionally sleep
  /* Sleep Radio */
  hal_set_slptr_high();
  RF230_sleeping = 1;
#endif

#endif /* !RADIOALWAYSON */

  ENERGEST_OFF(ENERGEST_TYPE_LISTEN);
}
/*---------------------------------------------------------------------------*/
#define GET_LOCK() locked = 1
static void RELEASE_LOCK(void) {
  if(lock_on) {
    on();
    lock_on = 0;
  }
  if(lock_off) {
    off();
    lock_off = 0;
  }
  locked = 0;
}
/*---------------------------------------------------------------------------*/
static void
set_txpower(uint8_t power)
{
  if (power > TX_PWR_17_2DBM){
    power=TX_PWR_17_2DBM;
  }
  if (radio_is_sleeping() ==true) {
    PRINTF("rf230_set_txpower:Sleeping");  //happens with cxmac
  } else {
    hal_subregister_write(SR_TX_PWR, power);
  }
}
/*---------------------------------------------------------------------------*/
int
rf230_init(void)
{
  DEBUGFLOW('I');

    /* Wait in case VCC just applied */
  delay_us(TIME_TO_ENTER_P_ON);

  /* Calibrate oscillator */
 calibrate_rc_osc_32k();

  /* Initialize Hardware Abstraction Layer. */
  hal_init();
  
  /* Do full rf230 Reset */
  hal_set_rst_low();
  hal_set_slptr_low();
  delay_us(TIME_RESET);
  hal_set_rst_high();

  /* Force transition to TRX_OFF. */
  hal_subregister_write(SR_TRX_CMD, CMD_FORCE_TRX_OFF);
  delay_us(TIME_P_ON_TO_TRX_OFF);
  
  /* Verify that it is a supported version */
  uint8_t tvers = hal_register_read(RG_VERSION_NUM);
  uint8_t tmanu = hal_register_read(RG_MAN_ID_0);

  if ((tvers != RF230_REVA) && (tvers != RF230_REVB))
    PRINTF("rf230: Unsupported version %u\n",tvers);
  if (tmanu != SUPPORTED_MANUFACTURER_ID) 
    PRINTF("rf230: Unsupported manufacturer ID %u\n",tmanu);

  PRINTF("rf230: Version %u, ID %u\n",tvers,tmanu);
  hal_register_write(RG_IRQ_MASK, RF230_SUPPORTED_INTERRUPT_MASK);

  /* Set up number of automatic retries */
  hal_subregister_write(SR_MAX_FRAME_RETRIES, RF230_CONF_AUTORETRIES );

  /* Use automatic CRC unless manual is specified */
#if RF230_CONF_CHECKSUM
  hal_subregister_write(SR_TX_AUTO_CRC_ON, 0);
#else
  hal_subregister_write(SR_TX_AUTO_CRC_ON, 1);
#endif

  /* Start the packet receive process */
  process_start(&rf230_process, NULL);
  
  /* Leave radio in on state (?)*/
  on();
  return 1;
}
/*---------------------------------------------------------------------------*/
static uint8_t buffer[RF230_MAX_TX_FRAME_LENGTH];
static int
rf230_transmit(unsigned short payload_len)
{
  int txpower;
  uint8_t total_len;
  uint8_t radiowason;
#if RF230_CONF_TIMESTAMPS
  struct timestamp timestamp;
#endif /* RF230_CONF_TIMESTAMPS */
#if RF230_CONF_CHECKSUM
  uint16_t checksum;
#endif /* RF230_CONF_CHECKSUM */

  GET_LOCK();
//  DEBUGFLOW('T');

  /* Save receiver state */
  radiowason=RF230_receive_on;

  /* If radio is sleeping we have to turn it on first */
  if (RF230_sleeping) {
    hal_set_slptr_low();
 //   delay_us(TIME_SLEEP_TO_TRX_OFF);
    RF230_sleeping=0;
  }
 
  /* Wait for any previous operation or state transition to finish */
  rf230_waitidle();

  /* Prepare to transmit */
#if RF230_CONF_AUTORETRIES
  radio_set_trx_state(TX_ARET_ON);
#else
  radio_set_trx_state(PLL_ON);
#endif

  txpower = 0;
  
  if(packetbuf_attr(PACKETBUF_ATTR_RADIO_TXPOWER) > 0) {
    /* Remember the current transmission power */
    txpower = rf230_get_txpower();
    /* Set the specified transmission power */
    set_txpower(packetbuf_attr(PACKETBUF_ATTR_RADIO_TXPOWER) - 1);
  }

  total_len = payload_len + AUX_LEN;

#if RF230_CONF_TIMESTAMPS
  rtimer_clock_t txtime = timesynch_time();
#endif /* RF230_CONF_TIMESTAMPS */

 /* Toggle the SLP_TR pin to initiate the frame transmission */
  hal_set_slptr_high();
  hal_set_slptr_low();
  hal_frame_write(buffer, total_len);

  if(RF230_receive_on) {
    ENERGEST_OFF(ENERGEST_TYPE_LISTEN);
  }
  ENERGEST_ON(ENERGEST_TYPE_TRANSMIT);

#if RADIOSTATS
  RF230_sendpackets++;
#endif
 
 /* We wait until transmission has ended so that we get an
     accurate measurement of the transmission time.*/
  rf230_waitidle();


#ifdef ENERGEST_CONF_LEVELDEVICE_LEVELS
  ENERGEST_OFF_LEVEL(ENERGEST_TYPE_TRANSMIT,rf230_get_txpower());
#endif

 /* Restore the transmission power */
 if(packetbuf_attr(PACKETBUF_ATTR_RADIO_TXPOWER) > 0) {
    set_txpower(txpower & 0xff);
  }
 
  /* Restore receive mode */
  if(radiowason) {
 //       DEBUGFLOW('m');
    on();
  }

#if RF230_CONF_TIMESTAMPS
  setup_time_for_transmission = txtime - timestamp.time;

  if(num_transmissions < 10000) {
    total_time_for_transmission += timesynch_time() - txtime;
    total_transmission_len += total_len;
    num_transmissions++;
  }

#endif /* RF230_CONF_TIMESTAMPS */

  ENERGEST_OFF(ENERGEST_TYPE_TRANSMIT);
  if(RF230_receive_on) {
    ENERGEST_ON(ENERGEST_TYPE_LISTEN);
  } else {
#if RADIOALWAYSON
    /* Enable reception */
    on();
#else
    /* Go to lower power TRX_OFF state (turn off PLL) */
    hal_subregister_write(SR_TRX_CMD, CMD_FORCE_TRX_OFF);
#endif
  }

  RELEASE_LOCK();
  return 0;

  /* If we are using WITH_SEND_CCA, we get here if the packet wasn't
     transmitted because of other channel activity. */
 // RIMESTATS_ADD(contentiondrop);
 // PRINTF("rf230: do_send() transmission never started\n");

  //if(packetbuf_attr(PACKETBUF_ATTR_RADIO_TXPOWER) > 0) {
    /* Restore the transmission power */
  //  set_txpower(txpower & 0xff);
 // }

 // RELEASE_LOCK();
 // return -3;			/* Transmission never started! */
}
/*---------------------------------------------------------------------------*/
static int
rf230_prepare(const void *payload, unsigned short payload_len)
{
  uint8_t total_len,*pbuf;
#if RF230_CONF_TIMESTAMPS
  struct timestamp timestamp;
#endif /* RF230_CONF_TIMESTAMPS */
#if RF230_CONF_CHECKSUM
  uint16_t checksum;
#endif /* RF230_CONF_CHECKSUM */

  GET_LOCK();
//    DEBUGFLOW('P');

//  PRINTF("rf230: sending %d bytes\n", payload_len);
//  PRINTSHORT("s%d ",payload_len);

  RIMESTATS_ADD(lltx);

#if RF230_CONF_CHECKSUM
  checksum = crc16_data(payload, payload_len, 0);
#endif /* RF230_CONF_CHECKSUM */
 
  /* Copy payload to RAM buffer */
  total_len = payload_len + AUX_LEN;
  if (total_len > RF230_MAX_TX_FRAME_LENGTH){
#if RADIOSTATS
    RF230_sendfail++;
#endif   
    return -1;
  }
  pbuf=&buffer[0];
  memcpy(pbuf,payload,payload_len);
  pbuf+=payload_len;

#if RF230_CONF_CHECKSUM
  memcpy(pbuf,&checksum,CHECKSUM_LEN);
  pbuf+=CHECKSUM_LEN;
#endif /* RF230_CONF_CHECKSUM */

#if RF230_CONF_TIMESTAMPS
  timestamp.authority_level = timesynch_authority_level();
  timestamp.time = timesynch_time();
  memcpy(pbuf,&timestamp,TIMESTAMP_LEN);
  pbuf+=TIMESTAMP_LEN;
#endif /* RF230_CONF_TIMESTAMPS */
/*------------------------------------------------------------*/  
/* If jackdaw report frame to ethernet interface */
#if JACKDAW
// mac_logTXtoEthernet(&params, &result);
  if (usbstick_mode.raw != 0) {
    uint8_t sendlen;

  /* Get the raw frame */
    memcpy(&raw_buf[UIP_LLH_LEN], buffer, total_len);
    sendlen = total_len;

  /* Setup generic ethernet stuff */
    ETHBUF(raw_buf)->type = htons(0x809A);  //UIP_ETHTYPE_802154 0x809A
  
//  uint64_t tempaddr;
 
  /* Check for broadcast message */
    if(rimeaddr_cmp(packetbuf_addr(PACKETBUF_ADDR_RECEIVER), &rimeaddr_null)) {
      ETHBUF(raw_buf)->dest.addr[0] = 0x33;
      ETHBUF(raw_buf)->dest.addr[1] = 0x33;
      ETHBUF(raw_buf)->dest.addr[2] = UIP_IP_BUF->destipaddr.u8[12];
      ETHBUF(raw_buf)->dest.addr[3] = UIP_IP_BUF->destipaddr.u8[13];
      ETHBUF(raw_buf)->dest.addr[4] = UIP_IP_BUF->destipaddr.u8[14];
      ETHBUF(raw_buf)->dest.addr[5] = UIP_IP_BUF->destipaddr.u8[15];
    } else {
  /* Otherwise we have a real address */  
//    tempaddr = p->dest_addr.addr64;
//    byte_reverse((uint8_t *)&tempaddr, 8);
      mac_createEthernetAddr((uint8_t *) &(ETHBUF(raw_buf)->dest.addr[0]),
          (uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_RECEIVER));
    }

//  tempaddr = p->src_addr.addr64;
//  memcpy(&tempaddr,packetbuf_addr(PACKETBUF_ADDR_SENDER),sizeof(tempaddr));
//  byte_reverse((uint8_t *)&tempaddr, 8);
    mac_createEthernetAddr((uint8_t *) &(ETHBUF(raw_buf)->src.addr[0]),(uip_lladdr_t *)&uip_lladdr.addr);

//  printf("Low2Eth: Sending 802.15.4 packet to ethernet\n\r");

    sendlen += UIP_LLH_LEN;
    usb_eth_send(raw_buf, sendlen, 0);
//  rndis_stat.rxok++;
  }
#endif /* JACKDAW */

  RELEASE_LOCK();
  return 0;
}
/*---------------------------------------------------------------------------*/
static int
rf230_send(const void *payload, unsigned short payload_len)
{
  rf230_prepare(payload, payload_len);
#if JACKDAW
// In sniffer mode we don't ever send anything
  if (usbstick_mode.sendToRf == 0) {
    uip_len = 0;
    return 0;
  }
#endif /* JACKDAW */
  return rf230_transmit(payload_len);
}
/*---------------------------------------------------------------------------*/
int
rf230_off(void)
{
  /* Don't do anything if we are already turned off. */
  if(RF230_receive_on == 0) {
    return 1;
  }

  /* If we are called when the driver is locked, we indicate that the
     radio should be turned off when the lock is unlocked. */
  if(locked) {
    lock_off = 1;
    return 1;
  }

  /* If we are currently receiving a packet
     we don't actually switch the radio off now, but signal that the
     driver should switch off the radio once the packet has been
     received and processed, by setting the 'lock_off' variable. */
  if (!rf230_isidle()) {
// if (radio_get_trx_state()==BUSY_RX) {
    lock_off = 1;
    return 1;
  }

  off();
  return 1;
}
/*---------------------------------------------------------------------------*/
int
rf230_on(void)
{
  if(RF230_receive_on) {
    return 1;
  }
  if(locked) {
    DEBUGFLOW('L');
    lock_on = 1;
    return 1;
  }

  on();
  return 1;
}
/*---------------------------------------------------------------------------*/
int
rf230_get_channel(void)
{
//jackdaw reads zero channel, raven reads correct channel?
//return hal_subregister_read(SR_CHANNEL);
  return channel;
}
/*---------------------------------------------------------------------------*/
void
rf230_set_channel(int c)
{
 /* Wait for any transmission to end. */
//  PRINTF("rf230: Set Channel %u\n",c);
  rf230_waitidle();
  channel=c;
  hal_subregister_write(SR_CHANNEL, c);
}
/*---------------------------------------------------------------------------*/
void
rf230_set_pan_addr(unsigned pan,
                    unsigned addr,
                    const uint8_t *ieee_addr)
//rf230_set_pan_addr(uint16_t pan,uint16_t addr,uint8_t *ieee_addr)
{
  PRINTF("rf230: PAN=%x Short Addr=%x\n",pan,addr);
  
  uint8_t abyte;
  abyte = pan & 0xFF;
  hal_register_write(RG_PAN_ID_0,abyte);
  abyte = (pan >> 8*1) & 0xFF;
  hal_register_write(RG_PAN_ID_1, abyte);

  abyte = addr & 0xFF;
  hal_register_write(RG_SHORT_ADDR_0, abyte);
  abyte = (addr >> 8*1) & 0xFF;
  hal_register_write(RG_SHORT_ADDR_1, abyte);  

  if (ieee_addr != NULL) {
    PRINTF("MAC=%x",*ieee_addr);
    hal_register_write(RG_IEEE_ADDR_7, *ieee_addr++);
    PRINTF(":%x",*ieee_addr);
    hal_register_write(RG_IEEE_ADDR_6, *ieee_addr++);
    PRINTF(":%x",*ieee_addr);
    hal_register_write(RG_IEEE_ADDR_5, *ieee_addr++);
    PRINTF(":%x",*ieee_addr);
    hal_register_write(RG_IEEE_ADDR_4, *ieee_addr++);
    PRINTF(":%x",*ieee_addr);
    hal_register_write(RG_IEEE_ADDR_3, *ieee_addr++);
    PRINTF(":%x",*ieee_addr);
    hal_register_write(RG_IEEE_ADDR_2, *ieee_addr++);
    PRINTF(":%x",*ieee_addr);
    hal_register_write(RG_IEEE_ADDR_1, *ieee_addr++);
    PRINTF(":%x",*ieee_addr);
    hal_register_write(RG_IEEE_ADDR_0, *ieee_addr);
    PRINTF("\n");
  }
}
/*---------------------------------------------------------------------------*/
/*
 * Interrupt leaves frame intact in FIFO.
 */
#if RF230_CONF_TIMESTAMPS
static volatile rtimer_clock_t interrupt_time;
static volatile int interrupt_time_set;
#endif /* RF230_CONF_TIMESTAMPS */
#if RF230_TIMETABLE_PROFILING
#define rf230_timetable_size 16
TIMETABLE(rf230_timetable);
TIMETABLE_AGGREGATE(aggregate_time, 10);
#endif /* RF230_TIMETABLE_PROFILING */
int
rf230_interrupt(void)
{
  /* Poll the receive process, unless the stack thinks the radio is off */
#if RADIOALWAYSON
if (RF230_receive_on) {
  DEBUGFLOW('+');
#endif
#if RF230_CONF_TIMESTAMPS
  interrupt_time = timesynch_time();
  interrupt_time_set = 1;
#endif /* RF230_CONF_TIMESTAMPS */

  process_poll(&rf230_process);
  
#if RF230_TIMETABLE_PROFILING
  timetable_clear(&rf230_timetable);
  TIMETABLE_TIMESTAMP(rf230_timetable, "interrupt");
#endif /* RF230_TIMETABLE_PROFILING */

  pending = 1;
  
#if RADIOSTATS
  RF230_receivepackets++;
#endif
  rf230_packets_seen++;

#if RADIOALWAYSON
} else {
  DEBUGFLOW('-');
  rxframe.length=0;
}
#endif
  return 1;
}
/*---------------------------------------------------------------------------*/
/* Process to handle input packets
 * Receive interrupts cause this process to be polled
 * It calls the core MAC layer which calls rf230_read to get the packet
 */
char rf230processflag;      //for debugging process call problems
PROCESS_THREAD(rf230_process, ev, data)
{
  int len;
  PROCESS_BEGIN();
  rf230processflag=99;

  while(1) {
    PROCESS_YIELD_UNTIL(ev == PROCESS_EVENT_POLL);
    rf230processflag=42;
#if RF230_TIMETABLE_PROFILING
    TIMETABLE_TIMESTAMP(rf230_timetable, "poll");
#endif /* RF230_TIMETABLE_PROFILING */

    pending = 0;

    packetbuf_clear();
    len = rf230_read(packetbuf_dataptr(), PACKETBUF_SIZE);
    rf230processflag=1;
    if(len > 0) {
      packetbuf_set_datalen(len);
      rf230processflag=2;
      NETSTACK_RDC.input();
#if RF230_TIMETABLE_PROFILING
      TIMETABLE_TIMESTAMP(rf230_timetable, "end");
      timetable_aggregate_compute_detailed(&aggregate_time,
                                           &rf230_timetable);
      timetable_clear(&rf230_timetable);
#endif /* RF230_TIMETABLE_PROFILING */
    }
  }

  PROCESS_END();
}
// Get packet from Radio if any, else return zero.
// At present the last frame is buffered in the interrupt routine so it does
// not access the hardware or change its status
/*---------------------------------------------------------------------------*/
static int
rf230_read(void *buf, unsigned short bufsize)
{
  uint8_t len,*framep;
#if FOOTER_LEN
  uint8_t footer[FOOTER_LEN];
#endif /* FOOTER_LEN */
#if RF230_CONF_CHECKSUM
  uint16_t checksum;
#endif /* RF230_CONF_CHECKSUM */
#if RF230_CONF_TIMESTAMPS
  struct timestamp t;
#endif /* RF230_CONF_TIMESTAMPS */

 len=rxframe.length;
  if (len==0) {
#if RADIOALWAYSON
   if (RF230_receive_on==0) {if (debugflow[debugflowsize-1]!='z') DEBUGFLOW('z');} //cxmac calls with radio off?
#endif
    return 0;
  }

#if RADIOALWAYSON
if (RF230_receive_on) {
#endif

// PRINTSHORT("r%d",rxframe.length);  
  PRINTF("rf230_read: %u bytes lqi %u crc %u\n",rxframe.length,rxframe.lqi,rxframe.crc);
#if DEBUG>1
    for (len=0;len<rxframe.length;len++) PRINTF(" %x",rxframe.data[len]);PRINTF("\n");
#endif

#if RF230_CONF_TIMESTAMPS
  if(interrupt_time_set) {
    rf230_time_of_arrival = interrupt_time;
    interrupt_time_set = 0;
  } else {
    rf230_time_of_arrival = 0;
  }
  rf230_time_of_departure = 0;
#endif /* RF230_CONF_TIMESTAMPS */
// GET_LOCK();
  rf230_packets_read++;

//if(len > RF230_MAX_PACKET_LEN) {
  if(len > RF230_MAX_TX_FRAME_LENGTH) {
    /* Oops, we must be out of sync. */
    DEBUGFLOW('y');
    flushrx();
    RIMESTATS_ADD(badsynch);
//    RELEASE_LOCK();
    return 0;
  }

  if(len <= AUX_LEN) {
    DEBUGFLOW('s');
    PRINTF("len <= AUX_LEN\n");
    flushrx();
    RIMESTATS_ADD(tooshort);
 //   RELEASE_LOCK();
    return 0;
  }

  if(len - AUX_LEN > bufsize) {
    DEBUGFLOW('b');
    PRINTF("len - AUX_LEN > bufsize\n");
    flushrx();
    RIMESTATS_ADD(toolong);
//    RELEASE_LOCK();
    return 0;
  }
 /* Transfer the frame, stripping the footer */
  framep=&(rxframe.data[0]);
  memcpy(buf,framep,len-AUX_LEN);
  framep+=len-AUX_LEN;
  /* Clear the length field to allow buffering of the next packet */
  rxframe.length=0;
  
#if RF230_CONF_CHECKSUM
  memcpy(&checksum,framep,CHECKSUM_LEN);
  framep+=CHECKSUM_LEN;
#endif /* RF230_CONF_CHECKSUM */
#if RF230_CONF_TIMESTAMPS
  memcpy(&t,framep,TIMESTAMP_LEN);
  framep+=TIMESTAMP_LEN;
#endif /* RF230_CONF_TIMESTAMPS */
#if FOOTER_LEN
  memcpy(footer,framep,FOOTER_LEN);
#endif
#if RF230_CONF_CHECKSUM
  if(checksum != crc16_data(buf, len - AUX_LEN, 0)) {
    DEBUGFLOW('K');
    PRINTF("checksum failed 0x%04x != 0x%04x\n",
      checksum, crc16_data(buf, len - AUX_LEN, 0));
  }

  if(footer[1] & FOOTER1_CRC_OK &&
     checksum == crc16_data(buf, len - AUX_LEN, 0)) {
#else
  if (1) {
 // if(footer[1] & FOOTER1_CRC_OK) {
#endif /* RF230_CONF_CHECKSUM */
//  rf230_last_rssi = footer[0];
    rf230_last_rssi = hal_subregister_read(SR_RSSI);
//  rf230_last_correlation = footer[1] & FOOTER1_CORRELATION;
    rf230_last_correlation = rxframe.lqi;
    packetbuf_set_attr(PACKETBUF_ATTR_RSSI, rf230_last_rssi);
    packetbuf_set_attr(PACKETBUF_ATTR_LINK_QUALITY, rf230_last_correlation);

    RIMESTATS_ADD(llrx);

#if RF230_CONF_TIMESTAMPS
    rf230_time_of_departure =
      t.time +
      setup_time_for_transmission +
      (total_time_for_transmission * (len - 2)) / total_transmission_len;

    rf230_authority_level_of_sender = t.authority_level;

    packetbuf_set_attr(PACKETBUF_ATTR_TIMESTAMP, t.time);
#endif /* RF230_CONF_TIMESTAMPS */

  } else {
    DEBUGFLOW('X');
    PRINTF("bad crc");
    RIMESTATS_ADD(badcrc);
    len = AUX_LEN;
  }

  /* Clean up in case of FIFO overflow!  This happens for every full
   * length frame and is signaled by FIFOP = 1 and FIFO = 0.
   */
 // if(FIFOP_IS_1 && !FIFO_IS_1) {
    /*    printf("rf230_read: FIFOP_IS_1 1\n");*/
 //   flushrx();
 // } else if(FIFOP_IS_1) {
    /* Another packet has been received and needs attention. */
    /*    printf("attention\n");*/
 //   process_poll(&rf230_process);
//  }

 // RELEASE_LOCK();
  if(len < AUX_LEN) {
    return 0;
  }
#if JACKDAW
/*------------------------------------------------------------*/  
/* If jackdaw report frame to ethernet interface */
// mac_logRXtoEthernet(&params, &result);
  if (usbstick_mode.raw != 0) {
    uint8_t sendlen;

  /* Get the raw frame */
    memcpy(&raw_buf[UIP_LLH_LEN], buf, len);
    sendlen = len;

  /* Setup generic ethernet stuff */
    ETHBUF(raw_buf)->type = htons(0x809A);  //UIP_ETHTYPE_802154 0x809A
  
//   uint64_t tempaddr;
 
  /* Check for broadcast message */
    if(rimeaddr_cmp(packetbuf_addr(PACKETBUF_ADDR_RECEIVER), &rimeaddr_null)) {
      ETHBUF(raw_buf)->dest.addr[0] = 0x33;
      ETHBUF(raw_buf)->dest.addr[1] = 0x33;
      ETHBUF(raw_buf)->dest.addr[2] = UIP_IP_BUF->destipaddr.u8[12];
      ETHBUF(raw_buf)->dest.addr[3] = UIP_IP_BUF->destipaddr.u8[13];
      ETHBUF(raw_buf)->dest.addr[4] = UIP_IP_BUF->destipaddr.u8[14];
      ETHBUF(raw_buf)->dest.addr[5] = UIP_IP_BUF->destipaddr.u8[15];
    } else {
  /* Otherwise we have a real address */  
//    tempaddr = p->dest_addr.addr64;
//    byte_reverse((uint8_t *)&tempaddr, 8);
      mac_createEthernetAddr((uint8_t *) &(ETHBUF(raw_buf)->dest.addr[0]),
          (uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_RECEIVER));
    }

//  tempaddr = p->src_addr.addr64;
//  memcpy(&tempaddr,packetbuf_addr(PACKETBUF_ADDR_SENDER),sizeof(tempaddr));
//  byte_reverse((uint8_t *)&tempaddr, 8);
    mac_createEthernetAddr((uint8_t *) &(ETHBUF(raw_buf)->src.addr[0]),(uip_lladdr_t *)&uip_lladdr.addr);

//  printf("Low2Eth: Sending 802.15.4 packet to ethernet\n\r");

    sendlen += UIP_LLH_LEN;
    usb_eth_send(raw_buf, sendlen, 0);
  }
#endif /* JACKDAW */

  return len - AUX_LEN;

#if RADIOALWAYSON
} else {
   DEBUGFLOW('R');  //Stack thought radio was off
   return 0;
}
#endif
}
/*---------------------------------------------------------------------------*/
void
rf230_set_txpower(uint8_t power)
{
  GET_LOCK();
  set_txpower(power);
  RELEASE_LOCK();
}
/*---------------------------------------------------------------------------*/
int
rf230_get_txpower(void)
{
  uint8_t power;
  if (radio_is_sleeping() ==true) {
    PRINTF("rf230_get_txpower:Sleeping");
    return 0;
  } else {
//  return hal_subregister_read(SR_TX_PWR);
    power=hal_subregister_read(SR_TX_PWR);
    if (power==0) {
      PRINTSHORT("PZ");
      return 1;
    } else {
      return power;
    }
  }
}
/*---------------------------------------------------------------------------*/
int
rf230_rssi(void)
{
  int rssi;
  int radio_was_off = 0;

  /*The RSSI measurement should only be done in RX_ON or BUSY_RX.*/
  if(!RF230_receive_on) {
    radio_was_off = 1;
    rf230_on();
  }

  rssi = (int)((signed char)hal_subregister_read(SR_RSSI));
  if (rssi==0) {
    DEBUGFLOW('r');
    PRINTF("RSZ");
    rssi=1;
  }

  if(radio_was_off) {
    rf230_off();
  }
  return rssi;
}
/*---------------------------------------------------------------------------*/
static int
rf230_cca(void)
{
  int cca;
  int radio_was_off = 0;

  /* If the radio is locked by an underlying thread (because we are
     being invoked through an interrupt), we preted that the coast is
     clear (i.e., no packet is currently being transmitted by a
     neighbor). */
  if(locked) {
    return 1;
  }
  
  if(!RF230_receive_on) {
    radio_was_off = 1;
    rf230_on();
  }

 // cca = CCA_IS_1;
  DEBUGFLOW('c');
  cca=1;

  if(radio_was_off) {
    rf230_off();
  }
  return cca;
}
/*---------------------------------------------------------------------------*/
int
rf230_receiving_packet(void)
{
  uint8_t radio_state;
  radio_state = hal_subregister_read(SR_TRX_STATUS);
  if ((radio_state==BUSY_RX) || (radio_state==BUSY_RX_AACK)) {
    DEBUGFLOW('B');
    return 1;
  } else {
    return 0;
  }
}
/*---------------------------------------------------------------------------*/
static int
pending_packet(void)
{
  if (pending) DEBUGFLOW('p');
  return pending;
}
/*---------------------------------------------------------------------------*/
void
calibrate_rc_osc_32k(void)
{
    /* Calibrate RC Oscillator: The calibration routine is done by clocking TIMER2
     * from the external 32kHz crystal while running an internal timer simultaneously.
     * The internal timer will be clocked at the same speed as the internal RC
     * oscillator, while TIMER2 is running at 32768 Hz. This way it is not necessary
     * to use a timed loop, and keep track cycles in timed loop vs. optimization
     * and compiler.
     */
    uint8_t osccal_original = OSCCAL;
    volatile uint16_t temp;
        
    /* This is bad practice, but seems to work. */
    OSCCAL = 0x80;


  //    PRR0 &= ~((1 << PRTIM2)|(1 << PRTIM1)); /*  Enable Timer 1 and 2 */

    TIMSK2 = 0x00; /*  Disable Timer/Counter 2 interrupts. */
    TIMSK1 = 0x00; /*  Disable Timer/Counter 1 interrupts. */

    /* Enable TIMER/COUNTER 2 to be clocked from the external 32kHz clock crystal.
     * Then wait for the timer to become stable before doing any calibration.
     */
    ASSR |= (1 << AS2);
    while (ASSR & ((1 << TCN2UB)|(1 << OCR2AUB)|(1 << TCR2AUB)|(1 << TCR2BUB))) { ; }
    TCCR2B = 1 << CS20;   /* run timer 2 at divide by 1 (32KHz) */

    AVR_ENTER_CRITICAL_REGION();

    uint8_t counter = 128;
    bool cal_ok = false;
    do{
        /* wait for timer to be ready for updated config */
        TCCR1B = 1 << CS10;

        while (ASSR & ((1 << TCN2UB)|(1 << OCR2AUB)|(1 << TCR2AUB)|(1 << TCR2BUB))) { ; }

        TCNT2 = 0x80;
        TCNT1 = 0;

        TIFR2 = 0xFF;

        /* Wait for TIMER/COUNTER 2 to overflow. Stop TIMER/COUNTER 1 and 2, and
         * read the counter value of TIMER/COUNTER 1. It will now contain the
         * number of cpu cycles elapsed within the period.
         */
        while (!(TIFR2 & (1 << TOV2))){
            ;
            }
        temp = TCNT1;

        TCCR1B = 0;

#define cal_upper (31250*1.05) // 32812 = 0x802c
#define cal_lower (31250*0.95) // 29687 = 0x73f7
        /* Iteratively reduce the error to be within limits */
        if (temp < cal_lower) {
            /* Too slow. Put the hammer down. */
            OSCCAL++;
        } else if (temp > cal_upper) {
            /* Too fast, retard. */
            OSCCAL--;
        } else {
            /* The CPU clock frequency is now within +/- 0.5% of the target value. */
            cal_ok = true;
        }

        counter--;
    } while ((counter != 0) && (false == cal_ok));

    if (true != cal_ok) {
        /* We failed, therefore restore previous OSCCAL value. */
        OSCCAL = osccal_original;
    }

    TCCR2B = 0;

    ASSR &= ~(1 << AS2);

    /* Disable both timers again to save power. */
    //    PRR0 |= (1 << PRTIM2);/* |(1 << PRTIM1); */

    AVR_LEAVE_CRITICAL_REGION();
}
/*---------------------------------------------------------------------------*/