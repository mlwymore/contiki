#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"

#include "dev/button-sensor.h"
#include "dev/light-sensor.h"
#include "dev/sht11/sht11-sensor.h"


#include "dev/leds.h"

#include <stdio.h>
/*---------------------------------------------------------------------------*/
PROCESS(example_broadcast_process, "Broadcast example");
//PROCESS(led_blinking_process, "Blinking leds");
AUTOSTART_PROCESSES(&example_broadcast_process);//w
/*---------------------------------------------------------------------------*/
static int count = 0; //declaring global variables for packet counter
int litState = 1;
int n=1, sum =0, avg =0;
uint16_t light=0,humidity=0,temperature=0;
int phase,sync=0,rphase;

struct moteData{ //Declaring structure for packet 
uint16_t humVal; //to determine mTorch is lit or not
uint8_t id;
uint16_t lVal; //whether synchronization needs to take place
uint16_t temp;
};

  
static void
broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{
		
	struct moteData *msg;  //initiailizing structure for recieving packet
        msg = packetbuf_dataptr();
 	//char *msg = (char *)packetbuf_dataptr();
	static struct etimer et; //initializing event timer

	int rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI); //getting the rssi using attribute from packet buffer		

	count++; //increment packet counter
	
	printf("rssi(%d):%d id:%u lval:%u hval:%u temp:%u \n",count,rssi,msg->id,msg->lVal,msg->humVal,msg->temp);
	//printf("msg:%s\n",msg);
	
	
	
}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static struct broadcast_conn broadcast;
/*---------------------------------------------------------------------------*/

PROCESS_THREAD(example_broadcast_process, ev, data)
{
  static struct etimer et; //initializing event timer
 leds_init();	//initialzing LEDS
 static int sink =0;
 static int id = 16;
 //SENSORS_ACTIVATE(light_sensor); //activating light sensorl

 printf("process started!");


  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)

  PROCESS_BEGIN();

  broadcast_open(&broadcast, 129, &broadcast_call);
 
  packetbuf_set_attr(PACKETBUF_ATTR_RADIO_TXPOWER,11); //Transmission radio power set to 11

  while(1) {
	
	if(sink==0)
	{

		etimer_set(&et, CLOCK_SECOND * 5);
		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
			      
	      SENSORS_ACTIVATE(light_sensor); //activating light sensor
	      SENSORS_ACTIVATE(sht11_sensor);

	      light = light_sensor.value(LIGHT_SENSOR_TOTAL_SOLAR);
	      humidity = sht11_sensor.value(SHT11_SENSOR_HUMIDITY);
	 	temperature = sht11_sensor.value(SHT11_SENSOR_TEMP);
		
	      struct moteData *msg;	//same time
	      msg = (struct moteData *)packetbuf_dataptr();
	      packetbuf_set_datalen(sizeof(struct moteData));
	      msg->lVal =light;
	      msg->humVal = humidity;
	      msg->id =16;
	      msg->temp = temperature;
	     
		SENSORS_DEACTIVATE(light_sensor);
	        SENSORS_DEACTIVATE(sht11_sensor);

		//char message[20];
		//sprintf(message,"%d:%d:%d:%d",id,light,humidity,temperature);

	      packetbuf_copyfrom(msg,sizeof(struct moteData));
		//packetbuf_copyfrom(message,20);		
		leds_on(LEDS_RED);
	      printf("message: light = %d humidity = %d temperature=%d\n",light,humidity,temperature);
		
		//puts(message);
	      broadcast_send(&broadcast);
		leds_off(LEDS_RED);
 	
	}//if sink
	
	else
	{
	printf("waiting for data...");
	PROCESS_YIELD();
	}

    }//while

  PROCESS_END();
}//process
/*---------------------------------------------------------------------------*/
