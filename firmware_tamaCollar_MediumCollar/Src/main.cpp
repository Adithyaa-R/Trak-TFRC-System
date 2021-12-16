#include "main.h"
#include "pin_map.h"
#include "common.h"
#include "LoRa.h"
#include "spi.h"
#include "softuart_avr.h"
#include "TinyGPS++.h"
#include "scheduling.h"
#include <string.h>
#include "rtc.h"
#include "twi-lowlevel.h"

// Global Variable
static CmdUARTInterface pkt_main;
static long timer_count = 0;
static SchedulingManage schedule;
static char sen_pkt_buff [100];
static bool rtc_time_set_flag = false;

// GPS Pasrser setup
static TinyGPSPlus gps;

// Temp Debugging related functions
void displayInfo ();

/**
 * @brief ISR scanner
 * @details It's been called in Main loop continuously. It keep reading ISR BUffer and detects the given strings patterns
 */
void CmdUARTInterface::packetDetect ()
{
	while (this->isr_in != this->isr_out)
	{
		/*////printf ("\r\nisr_in : %d, isr_out : %d : %c   Timer Count : %lu",
				this->isr_in, this->isr_out, this->isr_buff[this->isr_out], 
				this->_timer_count);*/
		switch (this->_packet_state)
		{
		case PKT_WAIT_SOH:
			if (this->isr_buff[this->isr_out] == PKT_SOH) // Waiting for start of packet
			{
				this->_packet_state = PKT_WAIT_EOH;
				//cmd.start_storing (this->isr_buff[this->isr_out]);
				this->_timer_count = PKT_TIMEOUT;
				//////printf ("\r\ncmd.packetDetect -> Detect Start of Header");
			}
			break;

		case PKT_WAIT_EOH:
			if ((this->isr_buff[this->isr_out]) == PKT_EOH) // Packe deected
			{
				//printf ("\r\ncmd.packetDetect -> Detect end of header");
				//////printf ("\r\nisr_in : %d, isr_out : %d : %c",this->isr_in, this->isr_out, this->isr_buff[this->isr_out]);
				//cmd.store (this->isr_buff[this->isr_out]);
				//cmd.detect ();
				this->_packet_state = PKT_WAIT_SOH;
				break;
			}
			/*if ((cmd.store (this->isr_buff[this->isr_out])) == FAILURE) // If buffer reaches it's limit, assume error
			{
				//printf ("\r\ncmd.packetDetect -> Buffer overflow");
				this->_packet_state = PKT_WAIT_SOH;
				break;
			}*/
			if (!(this->_timer_count)) // Check timeout
			{
				//printf ("\r\ncmd.packetDetect -> Packet timeout");
				this->_packet_state = PKT_WAIT_SOH;
				break;
			}
			break;

		default:
			this->_packet_state = PKT_WAIT_SOH;
		}
		this->isr_out = (this->isr_out + 1) % UART_ISR_BUFF_SIZE;
	}
	return;
}

void CmdUARTInterface::timerHandler ()
{
	if ((this->_timer_state == SET) && (this->_timer_count)) 
		this->_timer_count --;
	return;
}


void LoRaInit ()
{
	spi_init (0, 1, 0, 0, 1);                // MSB, master, mode0, F/4, x2
    if (!begin (915E6)) 
	{
        printf ("Starting LoRa failed!\r\n");
		printf ("..\r\n");
		printf ("..\r\n");
		printf ("..\r\n");
        while (1);
    }
	printf ("LoRa Init Successfull\r\n");
}

uint16_t generateLoRaPkt (char * pkt)
{
	char * ptr_pkt = pkt;
	uint16_t byte_count = 0, pkt_len;
	if (gps.location.isValid ())
	{
		*(ptr_pkt++) = PKT_SOH; // Start of header
		
		// Name of device
		*(ptr_pkt++) = 'C';
		byte_count = sprintf (ptr_pkt, "%d", COLLAR_NUMBER);
		ptr_pkt += byte_count;

		*(ptr_pkt++) = ','; // Adding seperator
		ptr_pkt = gps.location.getLatStr (ptr_pkt); // Adding Latitude
		*(ptr_pkt++) = ','; // Adding seperator
		ptr_pkt = gps.location.getLonStr (ptr_pkt); // Adding Longitude
		*(ptr_pkt++) = ','; // Adding seperator
		ptr_pkt = gps.getHDOPStr (ptr_pkt); // Adding HDOP
		*(ptr_pkt++) = ','; // Adding seperator
		
		// Adding Satellites count
		byte_count = sprintf (ptr_pkt, "%lu", (unsigned long)gps.satellites.value());
		ptr_pkt += byte_count;

		*(ptr_pkt++) = PKT_EOH; // End of header
		*(ptr_pkt) = 0; // Adding NULL
	}
	else
	{
		*(ptr_pkt++) = PKT_SOH; // Start of header

		// Name of device
		*(ptr_pkt++) = 'C';
		byte_count = sprintf (ptr_pkt, "%d", COLLAR_NUMBER);
		ptr_pkt += byte_count;

		*(ptr_pkt++) = ','; // Adding seperator

		byte_count = sprintf (ptr_pkt, "%s", (uint8_t *)"NO_LOC");
		ptr_pkt += byte_count;

		*(ptr_pkt++) = PKT_EOH; // End of header
		*(ptr_pkt) = 0; // Adding NULL
	}
	printf ("Packet generated: %s \r\n", pkt);
	pkt_len = strlen (pkt);
	printf ("Pkt Length: %d\r\n", pkt_len);
	return pkt_len;
}

void setTempScheduleConfig ()
{
	rtc_set_time_s (19, 1, 0);

	schedule.start_time.hour = 19;
	schedule.start_time.min = 0;
	schedule.start_time.sec = 0;

	schedule.send_interval.hour = 0;
	schedule.send_interval.min = 1;
	schedule.send_interval.sec = 0;

	schedule.end_time.hour = 20;
	schedule.end_time.min = 0;
	schedule.end_time.sec = 0;

	rtc_time_set_flag = true;
}

int main ()
{
	uint16_t temp_pkt_len = 0;

	// Hardware UART Setup
	USART_Init ();
    printf ("Entering main loop\r\n");

	// RTC Init
	twi_init_master();
	printf ("RTC->Init: TWI Init Done\r\n");
	rtc_init ();
    printf ("RTC Init\r\n");

	// GPS Software UART setup and Pulse pin mode
	gps.init ();
    printf ("GPS Init\r\n");

	// Just testing functions REMOVE THIS
	setTempScheduleConfig ();

	while (1)
	{
	/*
	0. Post sleep, Record wakeup time in INT0 Handler
	1. Start GPS and wait for accquire or till time out
		a. If GPS acquired before time out, get time, location and needed params from it and generate the pkt
		b. If GPS not acquired before time out, make a failure pkt
	2. Send the pkt
	3. Get the next alarm time and see if it still is in data sending time region
		a. Yes, set the next alarm time as alarm
		b. No, set the _start_time as alarm
	4. Pre sleep
	5. Sleep
	*/

		twi_init_master();
		printf ("RTC->Init: TWI Init Done\r\n");
		rtc_setup_ext_init ();
  		printf ("Main: RTC Ext Init\r\n");
		rtc_get_time_s ((uint8_t *)&schedule.wakeup_time.hour,
						(uint8_t *)&schedule.wakeup_time.min,
						(uint8_t *)&schedule.wakeup_time.sec);
		printf ("Main: Wakeup Time: %d : %d : %d\r\n", schedule.wakeup_time.hour, 
													   schedule.wakeup_time.min,
													   schedule.wakeup_time.sec);

		if (schedule.wakeup_time.hour == 0 && schedule.wakeup_time.min == 0
			&& schedule.wakeup_time.sec == 0)
		{
			printf ("Main: Time not set\r\n");
			rtc_time_set_flag = false;
		}

		if (!rtc_time_set_flag)
		{
			printf ("Main: Waiting for time from LoRa Pkt\r\n");
		}

		// Initialize milliseconds timer used in time 
		// keeping at many places in the functionality
		startmSTimer ();

		// GPS routine
		gps.handler ();
		gps.printData ();
		if (!rtc_time_set_flag)
		{
			printf ("Main: Setting time from LoRa failed so checking if it can be done from GPS\r\n");
			if (gps.time.isValid ())
			{
				printf ("Main: Valid time available in GPS, setting.\r\n");
				//rtc_set_time_s (gps.time.hour (), gps.time.minute (), gps.time.second ());
			}
		}

		// Generate Packet
		temp_pkt_len = generateLoRaPkt (sen_pkt_buff);

		// Send LoRa Packet
/*		LoRaInit ();
		beginPacket (0);
		write (sen_pkt_buff, temp_pkt_len);
		endPacket ();
		LoRaend ();
*/
		// Set the next alarm
		schedule.alarmHandler ();
		rtc_get_time_s ((uint8_t *)&schedule.wakeup_time.hour,
						(uint8_t *)&schedule.wakeup_time.min,
						(uint8_t *)&schedule.wakeup_time.sec);
		printf ("Main: Sleep Time: %d : %d : %d\r\n\r\n\r\n\r\n", schedule.wakeup_time.hour, 
													   schedule.wakeup_time.min,
													   schedule.wakeup_time.sec);

		// Sleep mode
		stopmSTimer ();
	    printf ("Main: Going to sleep now \r\n");
		sleepMode ();
	}
}

void appMilliSecCallback ()
{
	pkt_main.timerHandler ();
	if (timer_count) timer_count--;
	gps.mSTimerCallback ();
	return;
}


/**
 * @brief Interrupt callback for RS485 UART Rx
 * @details Gets called from UART interrupt and then store character in ISR buffer plus handles the counters
 * @param rcv_char Character recieved
 */
void appRS485RcvCallback (char rcv_char)
{
	pkt_main.rcv_char = rcv_char;
	pkt_main.isr_buff[pkt_main.isr_in] = pkt_main.rcv_char;
	pkt_main.isr_in++;

	if (pkt_main.isr_in >= UART_ISR_BUFF_SIZE)
		pkt_main.isr_in = 0;
}


