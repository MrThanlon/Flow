/****************************************************************************
 *
 *   Copyright (C) 2013 PX4 Development Team. All rights reserved.
 *   Author: Laurens Mackay <mackayl@student.ethz.ch>
 *   		 Dominik Honegger <dominik.honegger@inf.ethz.ch>
 *   		 Petri Tanskanen <tpetri@inf.ethz.ch>
 *   		 Samuel Zihlmann <samuezih@ee.ethz.ch>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include "stm32f4xx.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_adc.h"
#include "stm32f4xx_usart.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_tim.h"
#include "stm32f4xx_dma.h"
#include "stm32f4xx_exti.h" 
#include "stm32f4xx_syscfg.h"
#include "misc.h"
#include "utils.h"
#include "usart.h"
#include "settings.h"
#include "sonar.h"
#include "sonar_mode_filter.h"

#define SONAR_SCALE	1000.0f
#define SONAR_MIN	0.12f		/** 0.12m sonar minimum distance */
#define SONAR_MAX	3.5f		/** 3.50m sonar maximum distance */

#define atoi(nptr)  strtol((nptr), NULL, 10)
extern uint32_t get_boot_time_us(void);

static char data_buffer[5]; // array for collecting decoded data

static volatile uint32_t last_measure_time = 0;
static volatile uint32_t measure_time = 0;
static volatile float dt = 0.0f;
static volatile int valid_data;
static volatile int data_counter = 0;
static volatile int data_valid = 0;
static volatile int new_value = 0;

static volatile uint32_t sonar_measure_time_interrupt = 0;
static volatile uint32_t sonar_measure_time = 0;

/* kalman filter states */
float x_pred = 0.0f; // m
float v_pred = 0.0f;
float x_post = 0.0f; // m
float v_post = 0.0f; // m/s

float sonar_raw = 0.0f;  // m

float sonar_mode = 0.0f;
bool sonar_valid = false;				/**< the mode of all sonar measurements */

/**
  * @brief  Triggers the sonar to measure the next value
  *
  * see datasheet for more info
  */
void sonar_trigger(){
	GPIO_SetBits(GPIOE, GPIO_Pin_8);
	// delay 10 us
	for(int i = 0; i < 10; i++)
            for(int j = 0; j < 60; j++);
	
	GPIO_ResetBits(GPIOE, GPIO_Pin_8);
}

#define Echo_Read() GPIO_ReadInputDataBit(GPIOD, GPIO_Pin_13)

void EXTI15_10_IRQHandler (void)
{
	if(EXTI_GetITStatus(EXTI_Line13) != RESET) //asure generate EXTI Line
	{
		volatile uint32_t ultra_time = 0;
		EXTI_ClearITPendingBit(EXTI_Line13);
		if(Echo_Read() == 1)
			TIM_Cmd(TIM4, ENABLE);
		else if(Echo_Read() == 0)
		{
			TIM_Cmd(TIM4, DISABLE);
			ultra_time = TIM_GetCounter(TIM4);//us
			TIM_SetCounter(TIM4,0);

			//0.001 m,the speed is 340m/s when temperature is 15 degree.function:speed=331+0.6*t
			int temp =ultra_time* 17/100;

			if ((temp > SONAR_MIN*SONAR_SCALE) && (temp < SONAR_MAX*SONAR_SCALE))
			{
				last_measure_time = measure_time;
				measure_time = get_boot_time_us();
				sonar_measure_time_interrupt = measure_time;
				dt = ((float)(measure_time - last_measure_time)) / 1000000.0f;
				valid_data = temp;
				// the mode filter turned out to be more problematic
				// than using the raw value of the sonar
				//insert_sonar_value_and_get_mode_value(valid_data / SONAR_SCALE);
				sonar_mode = valid_data / SONAR_SCALE;
				new_value = 1;
				sonar_valid = true;
			}
			else sonar_valid = false;
		}
	}
}

/**
  * @brief  Sonar interrupt handler
  */
void UART4_IRQHandler(void)
{
	if (USART_GetITStatus(UART4, USART_IT_RXNE) != RESET)
	{
		/* Read one byte from the receive data register */
		uint8_t data = (USART_ReceiveData(UART4));

		if (data == 'R')
		{
			/* this is the first char (start of transmission) */
			data_counter = 0;
			data_valid = 1;

			/* set sonar pin 4 to low -> we want triggered mode */
			GPIO_ResetBits(GPIOE, GPIO_Pin_8);
		}
		else if (0x30 <= data && data <= 0x39)
		{
			if (data_valid)
			{
				data_buffer[data_counter] = data;
				data_counter++;
			}
		}
		else if (data == 0x0D)
		{
			if (data_valid && data_counter == 4)
			{
				data_buffer[4] = 0;
				int temp = atoi(data_buffer);

				/* use real-world maximum ranges to cut off pure noise */
				if ((temp > SONAR_MIN*SONAR_SCALE) && (temp < SONAR_MAX*SONAR_SCALE))
				{
					/* it is in normal sensor range, take it */
					last_measure_time = measure_time;
					measure_time = get_boot_time_us();
					sonar_measure_time_interrupt = measure_time;
					dt = ((float)(measure_time - last_measure_time)) / 1000000.0f;

					valid_data = temp;
					// the mode filter turned out to be more problematic
					// than using the raw value of the sonar
					//insert_sonar_value_and_get_mode_value(valid_data / SONAR_SCALE);
					sonar_mode = valid_data / SONAR_SCALE;
					new_value = 1;
					sonar_valid = true;
				} else {
					sonar_valid = false;
				}
			}

			data_valid = 0;
		}
		else
		{
			data_valid = 0;
		}
	}
}

/**
  * @brief  Basic Kalman filter
  */
static void sonar_filter(void)
{
	/* no data for long time */
	if (dt > 0.25f) // more than 2 values lost
	{
		v_pred = 0;
	}

	x_pred = x_post + dt * v_pred;
	v_pred = v_post;

	float x_new = sonar_mode;
	sonar_raw = x_new;
	x_post = x_pred + global_data.param[PARAM_SONAR_KALMAN_L1] * (x_new - x_pred);
	v_post = v_pred + global_data.param[PARAM_SONAR_KALMAN_L2] * (x_new - x_pred);

}


/**
  * @brief  Read out newest sonar data
  *
  * @param  sonar_value_filtered Filtered return value
  * @param  sonar_value_raw Raw return value
  */
bool sonar_read(float* sonar_value_filtered, float* sonar_value_raw)
{
	/* getting new data with only around 10Hz */
	if (new_value) {
		sonar_filter();
		new_value = 0;
		sonar_measure_time = get_boot_time_us();
	}

	/* catch post-filter out of band values */
	if (x_post < SONAR_MIN || x_post > SONAR_MAX) {
		sonar_valid = false;
	}

	*sonar_value_filtered = x_post;
	*sonar_value_raw = sonar_raw;

	return sonar_valid;
}

/**
 * @brief  Configures the sonar sensor Peripheral.
 */
void sonar_config(void)
{
	valid_data = 0;
	GPIO_InitTypeDef GPIO_InitStructure;
	// Enable GPIO clocks
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);
	//trig
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOE, &GPIO_InitStructure);
	//echo
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;

	GPIO_Init(GPIOD, &GPIO_InitStructure);

	EXTI_InitTypeDef EXTI_InitStructure;
	SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOD, GPIO_PinSource13);
	EXTI_InitStructure.EXTI_Line = EXTI_Line13;
	EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising_Falling;
	EXTI_InitStructure.EXTI_LineCmd = ENABLE;
	EXTI_Init(&EXTI_InitStructure);

	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = EXTI15_10_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);
	TIM_TimeBaseStructure.TIM_Period = 41000-1; //not used
	TIM_TimeBaseStructure.TIM_Prescaler =83; //1us pass,time count+1
	TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInit(TIM4, &TIM_TimeBaseStructure);
	TIM_SetCounter(TIM4,0);
	TIM_ITConfig(TIM4,TIM_IT_Update,DISABLE);
	TIM_Cmd(TIM4, DISABLE);
}

uint32_t get_sonar_measure_time()
{
    return sonar_measure_time;
}

uint32_t get_sonar_measure_time_interrupt()
{
    return sonar_measure_time_interrupt;
}

