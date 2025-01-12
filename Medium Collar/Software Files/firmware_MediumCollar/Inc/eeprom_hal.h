/*
Copyright (c) 2020 Devilbinder (Binder Tronics) https://bindertronics.com/

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
 */ 


#ifndef EEPROM_HAL_H_
#define EEPROM_HAL_H_

#include <string.h>
//#include "config.h"
#include <avr/io.h>
#include <avr/interrupt.h>


#define EEPROM_SIZE 1023


enum{
	EEPROM_OK,
	EEPROM_WRITE_FAIL,
	EEPROM_INVALID_ADDR	
};


uint8_t EEPROM_read(uint16_t uiAddress, uint8_t *data);
uint8_t EEPROM_write(uint16_t uiAddress, uint8_t ucData);
uint8_t EEPROM_update(uint16_t uiAddress, uint8_t ucData);
uint8_t EEPROM_update_batch(uint16_t uiAddress, void *data,uint16_t len);
uint8_t EEPROM_read_batch(uint16_t uiAddress, void *data,uint16_t len);
void storeTimeInEEPROM (struct tm * st_t, uint16_t start_addr);
void redTimeFromEEPROM (struct tm * st_t, uint16_t start_addr);


#endif /* EEPROM_HAL_H_ */