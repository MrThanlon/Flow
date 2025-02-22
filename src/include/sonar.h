/****************************************************************************
 *
 *   Copyright (C) 2013 PX4 Development Team. All rights reserved.
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

#ifndef SONAR_H_
#define SONAR_H_

#include <stdint.h>
#include <stdbool.h>
#include "settings.h"

/**
 * @brief  Configures the sonar sensor Peripheral.
 */
void sonar_config(void);

/**
  * @brief  Sonar interrupt handler
  */
//void UART4_IRQHandler(void);
void EXTI15_10_IRQHandler (void);

/**
  * @brief  Triggers the sonar to measure the next value
  */
void sonar_trigger(void);

/**
  * @brief  Read out newest sonar data
  *
  * @return true if valid measurement values were obtained, false else
  */
bool sonar_read(float* sonar_value_filtered, float* sonar_value_raw);

/**
  * @brief Get the timestamp of the new sonar value when available to the main code
  */
uint32_t get_sonar_measure_time(void);

/**
  * @brief Get the timestamp of the new sonar value when the interrupt is triggered
  */
uint32_t get_sonar_measure_time_interrupt(void);

#endif /* SONAR_H_ */
