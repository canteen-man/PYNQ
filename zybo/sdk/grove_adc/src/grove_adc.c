/******************************************************************************
 *  Copyright (c) 2016, Xilinx, Inc.
 *  All rights reserved.
 * 
 *  Redistribution and use in source and binary forms, with or without 
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1.  Redistributions of source code must retain the above copyright notice, 
 *     this list of conditions and the following disclaimer.
 *
 *  2.  Redistributions in binary form must reproduce the above copyright 
 *      notice, this list of conditions and the following disclaimer in the 
 *      documentation and/or other materials provided with the distribution.
 *
 *  3.  Neither the name of the copyright holder nor the names of its 
 *      contributors may be used to endorse or promote products derived from 
 *      this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
 *  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR 
 *  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR 
 *  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 *  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
 *  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 *  OR BUSINESS INTERRUPTION). HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 *  WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR 
 *  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF 
 *  ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
/******************************************************************************
 *
 *
 * @file grove_adc.c
 *
 * IOP code (MicroBlaze) for grove ADC121C021.
 * Grove ADC is read only, and has IIC interface
 * Operations implemented:
 *  1. Simple, single read from sensor, and write to data area.
 *  2. Continuous read from sensor and log to data area.
 * Hardware version 1.2.
 * http://www.ti.com/lit/ds/symlink/adc121c021-q1.pdf
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- --- ------- -----------------------------------------------
 * 1.00a cmc 04/06/16 release
 *
 * </pre>
 *
 *****************************************************************************/

#include "pmod.h"

#define IIC_ADDRESS 0x50

// VRef = Va measured on the board
#define V_REF 3.10

// Mailbox commands
// bit 1 always needs to be set

#define CONFIG_IOP_SWITCH      0x1
#define READ_RAW_DATA          0x2 
#define READ_VOLTAGE           0x3 
#define READ_AND_LOG_RAW_DATA  0x4 
#define READ_AND_LOG_VOLTAGE   0x5 
#define SET_LOW_LEVEL          0x6 
#define SET_HIGH_LEVEL         0x7
#define SET_HYSTERESIS_LEVEL   0x8 
#define READ_LOWEST_LEVEL      0x9
#define READ_HIGHEST_LEVEL     0xA
#define READ_STATUS            0xB
#define RESET_ADC              0xC

// Log constants
#define LOG_BASE_ADDRESS (MAILBOX_DATA_PTR(4))
#define LOG_ITEM_SIZE sizeof(float)
#define LOG_CAPACITY  (4000/LOG_ITEM_SIZE)

// ADC Registers
#define REG_ADDR_RESULT        0x00
#define REG_ADDR_ALERT         0x01
#define REG_ADDR_CONFIG        0x02
#define REG_ADDR_LIMITL        0x03
#define REG_ADDR_LIMITH        0x04
#define REG_ADDR_HYST          0x05
#define REG_ADDR_CONVL         0x06
#define REG_ADDR_CONVH         0x07

// Read from a Register
u32 read_adc(u8 reg){
   u8 data_buffer[2];
   u32 sample;
   
   data_buffer[0] = reg; // Set the address pointer register
   iic_write(IIC_ADDRESS, data_buffer, 1);
  
   iic_read(IIC_ADDRESS,data_buffer,2);
   sample = ((data_buffer[0]&0x0f) << 8) | data_buffer[1];
   return sample; 
}


// Write a number of bytes to a Register
// Maximum of 2 data bytes can be written in one transaction
void write_adc(u8 reg, u32 data, u8 bytes){
   u8 data_buffer[3];
   data_buffer[0] = reg;
   if(bytes ==2){
      data_buffer[1] = data & 0x0f; // Bits 11:8
      data_buffer[2] = data & 0xff; // Bits 7:0
   }else if(bytes == 1){
      data_buffer[1] = data & 0xff; // Bits 11:8
   }
     
   iic_write(IIC_ADDRESS, data_buffer, bytes+1);

}

int main(void)
{
   u32 cmd;
   int i;
   u32 delay;

   u32 adc_raw_value;
   float adc_voltage;
   u8 iop_pins[8];
   u32 temp;
   u32 iop_switch_config;
   
   // Initialize PMOD and timers
   pmod_init();
   // Initialize switch
   configureSwitch(GPIO_0, GPIO_1, SDA, GPIO_3, GPIO_4, GPIO_5, SCL, GPIO_7);
   // Reset, set Tconvert x 32 (fconvert 27 ksps)
   write_adc(REG_ADDR_CONFIG, 0x20, 1); 
   // Run application
   while(1){

      // wait and store valid command
      while((MAILBOX_CMD_ADDR)==0);
      
      cmd = MAILBOX_CMD_ADDR;
      
      switch(cmd){

         case CONFIG_IOP_SWITCH:
            // Only one pin can be changed with each CONFIG_IOP_SWITCH command
            // Pin number should be in the first 4 bits. 
            // Pin configuration should be in the next 4 bits. 
            
            // Read existing IOP switch configuration
            iop_switch_config = (*(volatile u32 *)(SWITCH_BASEADDR)); 
            
            // read new pin configuration
            temp = MAILBOX_DATA(0);
            // Extract individual pin configurations from existing
            for(i=0; i<8; i++){
               iop_pins[i] = (iop_switch_config>>(i*4))&0xf;
            }
            // set new pin configuration
            // Pin configuration bits [7:0], Pin number bits [3:0]
            iop_pins[temp & 0xf] = (temp >> 4) & 0xf; 
            configureSwitch(iop_pins[0], iop_pins[1], iop_pins[2], 
                            iop_pins[3], iop_pins[4], iop_pins[5], 
                            iop_pins[6], iop_pins[7]);            
            MAILBOX_CMD_ADDR = 0x0; 
            break;
         case READ_RAW_DATA:
            // write out adc_value, reset mailbox
            MAILBOX_DATA(0) = 2*read_adc(REG_ADDR_RESULT);
            MAILBOX_CMD_ADDR = 0x0; 

            break;

         case READ_VOLTAGE:
            // write out adc_value, reset mailbox
            adc_voltage = (float)((read_adc(REG_ADDR_RESULT))*V_REF*2/4096);
            MAILBOX_DATA_FLOAT(0) = adc_voltage;
            MAILBOX_CMD_ADDR = 0x0; 

            break;
         case READ_AND_LOG_RAW_DATA:   
            // initialize logging variables, reset cmd
            cb_init(&pmod_log, LOG_BASE_ADDRESS, LOG_CAPACITY, LOG_ITEM_SIZE);
            delay = MAILBOX_DATA(1);   
            MAILBOX_CMD_ADDR = 0x0; 

            do{   
               // push sample to log and delay
               adc_raw_value = 2*read_adc(REG_ADDR_RESULT);
               cb_push_back(&pmod_log, &adc_raw_value);
               delay_ms(delay);

            } while((MAILBOX_CMD_ADDR & 0x1)== 0); // do while no new command

            break;
         case READ_AND_LOG_VOLTAGE:   
            // initialize logging variables, reset cmd
            cb_init(&pmod_log, LOG_BASE_ADDRESS, LOG_CAPACITY, LOG_ITEM_SIZE);
            delay = MAILBOX_DATA(1);   
            MAILBOX_CMD_ADDR = 0x0; 

            do{   
               // push sample to log and delay
               adc_voltage = (float)((read_adc(REG_ADDR_RESULT))*V_REF*2/4096);
               cb_push_back_float(&pmod_log, &adc_voltage);
               delay_ms(delay);

            } while((MAILBOX_CMD_ADDR & 0x1)== 0); // do while no new command

            break;
         case SET_LOW_LEVEL:
            write_adc(REG_ADDR_LIMITL, MAILBOX_DATA(0), 2);
            MAILBOX_CMD_ADDR = 0x0; 
            break;
         case SET_HIGH_LEVEL:   
            write_adc(REG_ADDR_LIMITH, MAILBOX_DATA(0), 2);
            MAILBOX_CMD_ADDR = 0x0; 
            break;
         case SET_HYSTERESIS_LEVEL:
            write_adc(REG_ADDR_HYST, MAILBOX_DATA(0), 2);
            MAILBOX_CMD_ADDR = 0x0; 
            break;
         case READ_LOWEST_LEVEL:
            MAILBOX_DATA_FLOAT(0) = read_adc(REG_ADDR_CONVL);
            MAILBOX_CMD_ADDR = 0x0; 
            break;
         case READ_HIGHEST_LEVEL:
            MAILBOX_DATA_FLOAT(0) = read_adc(REG_ADDR_CONVH);
            MAILBOX_CMD_ADDR = 0x0; 
            break;           
         case READ_STATUS:
            MAILBOX_DATA_FLOAT(0) = read_adc(REG_ADDR_ALERT);
            MAILBOX_CMD_ADDR = 0x0; 
            break;
         case RESET_ADC:
            write_adc(REG_ADDR_CONFIG, 0x20, 1);
            MAILBOX_CMD_ADDR = 0x0; 
            break;
         default:
            MAILBOX_CMD_ADDR = 0x0; // reset command
            break;
      }
   }
   return 0;
}
