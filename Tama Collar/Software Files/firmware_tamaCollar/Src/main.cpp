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
#include "command_layer.h"
#include "battery.h"
#include "eeprom_hal.h"
#ifdef MEDIUM_COLLAR
#include "medium_collar.h"
#include "ext_mem_eeprom.h"
#endif

// Global Variable
static CmdUARTInterface pkt_main;
static long timer_count = 0;
SchedulingManage schedule;
static char sen_pkt_buff [50];
bool rtc_time_set_flag = false;
static CmdProcess cmd;
uint8_t dev_id;

// GPS Pasrser setup
static TinyGPSPlus gps;

// Temp Debugging related functions
void displayInfo ();

/**
 * @brief ISR scanner
 * @details It's been called in Main loop continuously. It keep reading ISR BUffer and detects the given strings patterns
 */
void CmdUARTInterface::packetDetect (char source)
{
	while (this->isr_in != this->isr_out)
	{
		/*printf ("\r\nisr_in : %d, isr_out : %d : %c   Timer Count : %lu",
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
				////////printf ("\r\ncmd.packetDetect -> Detect Start of Header");
			}
			break;

		case PKT_WAIT_EOH:
			if ((this->isr_buff[this->isr_out]) == PKT_EOH) // Packet deected
			{
				//printf ("\r\ncmd.packetDetect -> Detect end of header");
				//printf ("\r\nisr_in : %d, isr_out : %d : %c",this->isr_in, this->isr_out, this->isr_buff[this->isr_out]);
				cmd.store (this->isr_buff[this->isr_out]);
				cmd.detect (source);
				this->_packet_state = PKT_WAIT_SOH;
				break;
			}
			if ((cmd.store (this->isr_buff[this->isr_out])) == FAILURE) // If buffer reaches it's limit, assume error
			{
				//printf ("\r\ncmd.packetDetect -> Buffer overflow");
				this->_packet_state = PKT_WAIT_SOH;
				break;
			}
			if (!(this->_timer_count)) // Check timeout
			{
				////printf ("\r\ncmd.packetDetect -> Packet timeout");
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

uint16_t generateLoRaPkt (char * pkt, char gps_ret)
{
	char * ptr_pkt = pkt;
	uint16_t byte_count = 0, pkt_len;
	uint8_t vccx10 = readVccVoltage ();
	
	if (gps.location.isValid ())
	{
		*(ptr_pkt++) = PKT_SOH; // Start of header
		
		// Name of device
		*(ptr_pkt++) = DEVICE_CODE;
		byte_count = sprintf (ptr_pkt, "%d", dev_id);
		ptr_pkt += byte_count;

		*(ptr_pkt++) = ','; // Adding seperator
		ptr_pkt = gps.location.getLatStr (ptr_pkt); // Adding Latitude
		*(ptr_pkt++) = ','; // Adding seperator
		ptr_pkt = gps.location.getLonStr (ptr_pkt); // Adding Longitude
		*(ptr_pkt++) = ','; // Adding seperator
		ptr_pkt = gps.getHDOPStr (ptr_pkt); // Adding HDOP
		*(ptr_pkt++) = ','; // Adding seperator
		
		// Adding Satellites count
		byte_count = sprintf (ptr_pkt, "%2lu", (unsigned long)gps.satellites.value());
		ptr_pkt += byte_count;

		*(ptr_pkt++) = ','; // Adding seperator

		// Adding Time stamp
		ptr_pkt += sprintf (ptr_pkt, "%2d:%2d:%2d", schedule.wakeup_time.hour, 
												schedule.wakeup_time.min,
												schedule.wakeup_time.sec);

		*(ptr_pkt++) = ','; // Adding seperator

		ptr_pkt += sprintf (ptr_pkt, "%2d", vccx10); // Adding the Vcc Voltage
		*(ptr_pkt++) = ','; // Adding seperator

		// GPS State
		if (gps_ret == GPS_RET_SLEEP_FAIL)
			ptr_pkt += sprintf (ptr_pkt, "GFS");
		else
			ptr_pkt += sprintf (ptr_pkt, "GOK");

		*(ptr_pkt++) = PKT_EOH; // End of header
		*(ptr_pkt) = 0; // Adding NULL
	}
	else
	{
		*(ptr_pkt++) = PKT_SOH; // Start of header

		// Name of device
		*(ptr_pkt++) = DEVICE_CODE;
		byte_count = sprintf (ptr_pkt, "%d", dev_id);
		ptr_pkt += byte_count;

		*(ptr_pkt++) = ','; // Adding seperator

		byte_count = sprintf (ptr_pkt, "%s", (uint8_t *)"NO_LOC");
		ptr_pkt += byte_count;

		*(ptr_pkt++) = ','; // Adding seperator

		*(ptr_pkt++) = '-'; // Adding empty dash
		*(ptr_pkt++) = ','; // Adding seperator
		*(ptr_pkt++) = '-'; // Adding empty dash
		*(ptr_pkt++) = ','; // Adding seperator
		*(ptr_pkt++) = '-'; // Adding empty dash
		*(ptr_pkt++) = ','; // Adding seperator

/*		// Adding Time stamp
		ptr_pkt += sprintf (ptr_pkt, "%d:%d:%d", schedule.wakeup_time.hour, 
												schedule.wakeup_time.min,
												schedule.wakeup_time.sec);

		*(ptr_pkt++) = ','; // Adding seperator
*/

		ptr_pkt += sprintf (ptr_pkt, "%d", vccx10); // Adding the Vcc Voltage
		*(ptr_pkt++) = ','; // Adding seperator
		*(ptr_pkt++) = ','; // Adding seperator

		// GPS State
		if (gps_ret == GPS_RET_WAKE_FAIL)
			ptr_pkt += sprintf (ptr_pkt, "GFW");
		else
			ptr_pkt += sprintf (ptr_pkt, "GOK");

		*(ptr_pkt++) = PKT_EOH; // End of header
		*(ptr_pkt) = 0; // Adding NULL
	}

	pkt_len = strlen (pkt);
	printf ("Pkt gen: %s   %d \r\n", pkt, pkt_len);
	return pkt_len;
}

void setDefaultTime ()
{
	struct tm temp_time;

	temp_time.hour = schedule.start_time.hour; 
	temp_time.min = schedule.start_time.min;
	temp_time.sec = schedule.start_time.sec;
	temp_time.mday = 14; temp_time.mon = 1; temp_time.year = 22;

	rtc_set_time (&temp_time);
	rtc_time_set_flag = true;
}

void setTempScheduleConfig ()
{
/*	struct tm temp_time;

	temp_time.hour = 19; temp_time.min = 0; temp_time.sec = 0;
	temp_time.mday = 27; temp_time.mon = 12; temp_time.year = 21;
*/
//	rtc_set_time (&temp_time);

//	rtc_set_time_s (19, 1, 0);
//	rtc_time_set_flag = true;

	schedule.start_time.hour = 9;
	schedule.start_time.min = 0;
	schedule.start_time.sec = 0;

	schedule.send_interval.hour = 0;
	schedule.send_interval.min = 10;
	schedule.send_interval.sec = 0;

	schedule.end_time.hour = 18;
	schedule.end_time.min = 0;
	schedule.end_time.sec = 0;	
}

void UARTRcvPkts ()
{
	char sent_flag = 0;
    LoRaInit ();
	timer_count = 10000;
	while (timer_count)
	{
		pkt_main.packetDetect (PKT_SRC_UART);
	}
}

void LoRaRcvPkts ()
{
	char sent_flag = 0;
    LoRaInit ();
	timer_count = 30000;
	while (timer_count)
	{
		if (parsePacket (0))
		{
			while (available ())
			{
				appRS485RcvCallback (read ());
			}
			pkt_main.packetDetect (PKT_SRC_LORA);
			if (rtc_time_set_flag) break;
		}
	}
}

void loadPrintWakeTime ()
{
	rtc_get_time_s ((uint8_t *)&schedule.wakeup_time.hour,
				(uint8_t *)&schedule.wakeup_time.min,
				(uint8_t *)&schedule.wakeup_time.sec);
	printf (("Main: RTC Time: "));
	printf ("%d : %d : %d\r\n", schedule.wakeup_time.hour, schedule.wakeup_time.min, schedule.wakeup_time.sec);
	return;
}

int main ()
{
	uint16_t temp_pkt_len = 0;
	char * temp_buff_point;
	uint8_t temp_read = 0, temp_count = 0, gps_ret;

	// Hardware UART Setup
	USART_Init ();
    //printf ("Entering main loop\r\n");
	#ifndef MEDIUM_COLLAR
    printf (PSTR("------- Tama Collar -------\r\n"));
	#endif

	#ifdef MEDIUM_COLLAR
    printf (PSTR("------- Medium Collar -------\r\n"));
	#endif

	// I2C Init
	twi_init_master();
	//printf ("Main: TWI Init Done\r\n");
	rtc_init ();
    //printf ("Main: RTC Init\r\n");

	// GPS Software UART setup and Pulse pin mode
	#ifndef MEDIUM_COLLAR
	gps.init (GPS_PULSE_POWER_MODE);
    //printf ("Main: GPS Init Tama Collar\r\n");
	#endif

	#ifdef MEDIUM_COLLAR
	gps.init (GPS_MOSFET_POWER_MODE);
    //printf ("Main: GPS Init for Medium Collar\r\n");
	#endif

//	printf ("RTC and GPS Init Done \r\n");
	// Just testing functions REMOVE THIS
//	setTempScheduleConfig ();

	// Section to send first tim wakeup packet
	uint8_t temp_bat_volt = readVccVoltage ();

	#ifdef MEDIUM_COLLAR
	//firstTimeMOtorRoutine ();
	#endif

	// Initialize milliseconds timer used in time 
	// keeping at many places in the functionality
	startmSTimer ();
	
	twi_init_master();
	printf (PSTR("RTC->Init: TWI Init Done\r\n"));
	rtc_setup_ext_init ();
	//printf ("Main: RTC Ext Init\r\n");
	loadPrintWakeTime ();
	
	/*if (schedule.wakeup_time.hour == 0 && schedule.wakeup_time.min == 0
		&& schedule.wakeup_time.sec == 0)
	{
		rtc_time_set_flag = false;
	}*/
	
//	EEPROM_read (EEPROM_ADDR_ID_FLAG, &temp_read);
	//if ((temp_read != ID_SET_FLAG) || (!rtc_time_set_flag))
	//{
		printf (PSTR("Main: Wait for Pkt\r\n\r\n\r\n"));
		UARTRcvPkts ();
		LoRaRcvPkts ();
		loadPrintWakeTime ();
	//}
	// Reading device ID and schedule from EEPROM
	EEPROM_read (EEPROM_ADDR_ID, &dev_id);
	redTimeFromEEPROM (&(schedule.start_time), EEPROM_ADDR_START_TIME_HR);
	redTimeFromEEPROM (&(schedule.end_time), EEPROM_ADDR_END_TIME_HR);
	redTimeFromEEPROM (&(schedule.send_interval), EEPROM_ADDR_INTRVL_TIME_HR);
	printf (("Main Start, end, int Time: \r\n"));
	printf ("%d : %d : %d\r\n", schedule.start_time.hour, schedule.start_time.min, schedule.start_time.sec);
	printf ("%d : %d : %d\r\n", schedule.end_time.hour, schedule.end_time.min, schedule.end_time.sec);											   
	printf ("%d : %d : %d\r\n", schedule.send_interval.hour, schedule.send_interval.min, schedule.send_interval.sec);
	if ((schedule.start_time.hour > 23) || (schedule.start_time.min > 59) || (schedule.start_time.sec > 59))
	{
		printf ("Changing to :\r\n");
		setTempScheduleConfig ();
		printf ("%d : %d : %d\r\n", schedule.start_time.hour, schedule.start_time.min, schedule.start_time.sec);
		printf ("%d : %d : %d\r\n", schedule.end_time.hour, schedule.end_time.min, schedule.end_time.sec);											   
		printf ("%d : %d : %d\r\n", schedule.send_interval.hour, schedule.send_interval.min, schedule.send_interval.sec);
	}
	printf (("\r\nDev ID: %d\r\n"), dev_id);

	temp_buff_point = sen_pkt_buff;
	*(temp_buff_point++) = PKT_SOH;
	*(temp_buff_point++) = DEVICE_CODE;
	temp_buff_point += sprintf (temp_buff_point, "%d,ON,", dev_id);
	// Adding Time stamp
	temp_buff_point += sprintf (temp_buff_point, "%d:%d:%d,", schedule.start_time.hour, schedule.start_time.min, schedule.start_time.sec);
	temp_buff_point += sprintf (temp_buff_point, "%d:%d:%d,", schedule.send_interval.hour, schedule.send_interval.min, schedule.send_interval.sec);
	temp_buff_point += sprintf (temp_buff_point, "%d", temp_bat_volt);
	*(temp_buff_point++) = PKT_EOH;
	*temp_buff_point = 0;
	temp_pkt_len = strlen (sen_pkt_buff);
	printf ("Sending ON Packet: %s \r\n", sen_pkt_buff);
	LoRaSendSleep (sen_pkt_buff, temp_pkt_len);

	while (1)
	{
	/*
	0. Post sleep, Record wakeup time in INT0 Handler
	1. Start GPS and wait for accquire or till time out
		a. If GPS acquired before time out, get time, location and needed params from it and generate the pkt
		b. If GPS not acquired before time out, make a failure pkt
	2. Send the pkt
	-> If it is a Medium Collar
		- Function that checks if Power is too low or if time has come for collar to drop
		- If time to drop than send packet with location of dropping collar and run the motor
		- Stay if Infinite loop of sleep
	3. Get the next alarm time and see if it still is in data sending time region
		a. Yes, set the next alarm time as alarm
		b. No, set the _start_time as alarm
	4. Pre sleep
	5. Sleep
	Medium Collar notes:
		- Low power is considered around 3.1 V because below 3 V GPS does not work
	*/
		// Initialize milliseconds timer used in time 
		// keeping at many places in the functionality
		startmSTimer ();
		
		twi_init_master();
		//printf ("RTC->Init: TWI Init Done\r\n");
		rtc_setup_ext_init ();
  		//printf ("Main: RTC Ext Init\r\n");
		loadPrintWakeTime ();

		temp_bat_volt = readVccVoltage ();

		// GPS routine and Generate Packet
		gps_ret = gps.handler ();
		temp_pkt_len = generateLoRaPkt (sen_pkt_buff, gps_ret);
		gps.printData ();

		// Check if the GPS location is valid
		if (gps.location.isValid ())
		{
			//printf ("Main: Valid GPS, set RTC\r\n");
			struct tm temp_tm;
			temp_tm.hour = gps.time.hour ();
			temp_tm.min = gps.time.minute ();
			temp_tm.sec = gps.time.second ();
			temp_tm.mday = gps.date.day ();
			temp_tm.mon = gps.date.month ();
			temp_tm.year = gps.date.year ();
			rtc_set_time (&temp_tm);
			loadPrintWakeTime ();
			rtc_time_set_flag = true;

			// Storing the data point in the EEPROM
			#ifdef MEDIUM_COLLAR
			storeDataPointInEEPROM (gps, gps_ret, &(schedule.wakeup_time));
			#endif
		}
		else if (!rtc_time_set_flag)
		{
			setDefaultTime ();
			loadPrintWakeTime ();
		}

		// Send LoRa Packet
		LoRaSendSleep (sen_pkt_buff, temp_pkt_len);

		#ifdef MEDIUM_COLLAR
		//releaseHandler (temp_bat_volt);
		#endif

		// Set the next alarm
		schedule.alarmHandler ();
		loadPrintWakeTime ();
		
		// Sleep mode
		stopmSTimer ();
	    //printf ("Main: Sleep now \r\n\r\n");
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
