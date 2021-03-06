/*
 * Copyright (c) 2010, University of Luebeck, Germany.
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
 * Author: Carlo Alberto Boano <cboano@iti.uni-luebeck.de>
 *
 * Histogram of noise + channel vacancy
 *
 */

#include "contiki.h"
#include <math.h>
#include <stdio.h>
#include <signal.h>
#include "dev/leds.h"
#include "dev/cc2420.h"
#include "dev/watchdog.h"
#include "interferer_settings.h"

/*---------------------------------------------------------------------------*/
#define CHANNEL 24				// Channel to be used

// Buffer
#define BUFFER_SIZE 100
unsigned long buffer0[BUFFER_SIZE+1] = {0};

// Periodicity of the scan
#define MAX_VALUE 2000000
#define PERIOD_TIME (CLOCK_SECOND*2)

#define THRESHOLD_RSSI -77		// Threshold for marking an instant as interfered or not

/*---------------------------------------------------------------------------*/
// CPU Boosting 
uint16_t cpu1, cpu2;

void boost_cpu() // Temporarily boost CPU speed
{
 cpu1 = DCOCTL;
 cpu2 = BCSCTL1;
 DCOCTL = 0xff;
 BCSCTL1 |= 0x07;
}	
void restore_cpu() // Restore CPU speed
{
 DCOCTL = cpu1;
 BCSCTL1 = cpu2;
}
/*---------------------------------------------------------------------------*/

PROCESS(scanning, "Scanning @ full precision");
AUTOSTART_PROCESSES(&scanning);
PROCESS_THREAD(scanning, ev, data)
{
 PROCESS_BEGIN();
 
 // Initial operations
 leds_off(LEDS_ALL);
 watchdog_stop(); 
 
 // Avoiding wrong RSSI readings
 unsigned temp;
 CC2420_READ_REG(CC2420_AGCTST1, temp);
 CC2420_WRITE_REG(CC2420_AGCTST1, (temp + (1 << 8) + (1 << 13))); 
 
 // Selecting the channel	
 SPI_SETCHANNEL_SUPERFAST(357+((CHANNEL-11)*5));
 
 // Avoiding the initial wrong readings by discarding the wrong readings
 CC2420_SPI_ENABLE();
 unsigned long k=0;
 for (k=0; k<=15; k++) {MY_FASTSPI_GETRSSI(temp);}
 CC2420_SPI_DISABLE(); 
 
 static struct etimer et;
 while(1){
	 
	 printf("#START (dBm: occurrencies)\n");
	 
	 // Resetting everything
	 for(k=0;k<BUFFER_SIZE;k++){	
		buffer0[k] = 0;
	 }
	 
	 dint();				// Disable interrupts
	 boost_cpu(); 			// Temporarily boost CPU speed
	 CC2420_SPI_ENABLE(); 	// Enable SPI
	
	 // Actual scanning 
	 static signed char rssi;
	 for(k=0; k<MAX_VALUE; k++){		
		MY_FASTSPI_GETRSSI(rssi);	
		buffer0[rssi+55]++;
	 }	
 
	 CC2420_SPI_DISABLE();	// Disable SPI
	 restore_cpu();			// Restore CPU speed
	 eint(); 				// Re-enable interrupts
 
	 // Printing the stored values in compressed form 
	 unsigned long sum_cca = 0;
	 for(temp=0; temp<BUFFER_SIZE; temp++) {
	 	float f = ((float) buffer0[temp]*1.0000) / MAX_VALUE;	
		#define NO_ZEROS 1
		#if NO_ZEROS
		if(buffer0[temp]>0){
		#endif
			printf("%d: %ld.%04u (%ld)\n", temp-100, (long) f, (unsigned)((f-floor(f))*10000), buffer0[temp]);
			clock_delay(30000);
			// Performing CCA
			if((temp-100) < THRESHOLD_RSSI){
				sum_cca += buffer0[temp];
			}	
		#if NO_ZEROS
		}
		#endif
	 }
	 
	 // Printing the results of the CCA
	 float f_cca = ((float) sum_cca*1.0000) / MAX_VALUE;		
	 printf("Vacancy: %ld.%04u (%ld)\n", (long) f_cca, (unsigned)((f_cca-floor(f_cca))*10000), sum_cca);
	 
	 printf("#END\n");
	 
	 // Waiting for timer
	 etimer_set(&et, PERIOD_TIME);
	 PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));	
 }
 
 PROCESS_WAIT_EVENT();
 PROCESS_END();
}
/*---------------------------------------------------------------------------*/
