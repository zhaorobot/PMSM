/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2014 Pavlo Milo Manovi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file	BasicMotorControl.c
 * @author 	Pavlo Manovi
 * @date	March 21st, 2014
 * @brief 	This library provides implementation of control methods for the DRV8301.
 *
 * This library provides implementation of basic trapezoidal control for a BLDC motor.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "BasicMotorControl.h"
#include "PMSMBoard.h"
#include "DMA_Transfer.h"
#include "PMSM.h"
#include <uart.h>
#include <xc.h>
#include <dsp.h>

tPID speedPID;
fractional speedCoefficient[3] __attribute__((space(xmemory)));
fractional speedControlHistory[3] __attribute__((eds, space(ymemory)));
BasicMotorControlInfo motorInfo;

static struct {
	float Kp;
	float Ki;
	float Kd;
	float err;
	float der;
	float output;
	float integral;
	float currentError;
} PIDs;

static uint32_t timerCurr;
static uint32_t timer;
static uint16_t commandedTorque;
static uint8_t changeState;

void TrapUpdate(uint16_t torque, uint16_t direction);

void DSPInit(void)
{
	speedPID.abcCoefficients = speedCoefficient;
	speedPID.controlHistory = speedControlHistory;
	PIDInit(&speedPID);
}

void DSPTuningsInit(float p, float i, float d)
{
	fractional PID_speed[3];

	PID_speed[0] = Float2Fract(p);
	PID_speed[1] = Float2Fract(i);
	PID_speed[2] = Float2Fract(d);

	PIDCoeffCalc(PID_speed, &speedPID);
}

/**                          Public Functions                                **/


void SpeedControlInit(float p, float i, float d)
{
	motorInfo.currentSpeed = 0;
	motorInfo.hallCount = 0;
	motorInfo.lastHallState = 0;

	PIDs.err = 0;
	PIDs.der = 0;
	PIDs.integral = 0;

	TMR3HLD = 0;
	TMR2 = 0;
	timer = 0;

	PIDs.Kp = p;
	PIDs.Ki = i;
	PIDs.Kd = d;

	DSPInit();
	DSPTuningsInit(p, i, d);
}

void SpeedControlChangeTunings(float p, float i, float d)
{
	DSPTuningsInit(p, i, d);
}

void SpeedControlStep(uint16_t speed, uint8_t direction, uint8_t update)
{
	if (update) {
		if (changeState) {
			motorInfo.currentSpeed = 52500000 / (timer / motorInfo.hallCount);

			PIDs.currentError = (speed - motorInfo.currentSpeed);

			PIDs.der = PIDs.err - PIDs.currentError;
			PIDs.integral += PIDs.err;
			if (PIDs.integral > 2000000) {
				PIDs.integral = 2000000;
			}

			PIDs.output = (PIDs.Kp * PIDs.err) +
				(PIDs.Ki * PIDs.integral * .001); +
				(PIDs.Kd * PIDs.der / .001);

			if (PIDs.output > 30000) {
				PIDs.output = 30000;
			} else if (PIDs.output < 0) {
				PIDs.output = 0;
			}
			PIDs.err = PIDs.currentError;

			commandedTorque = PIDs.output / 30;


			uint8_t out[56];
			sprintf((char *) out, "S: %f, E: %f\r\n", 52500000 / (timer / motorInfo.hallCount), PIDs.output);
			putsUART2(out);

			motorInfo.hallCount = 0;
			changeState = 0;
			timer = 0;
			timerCurr = 0;
		}
	}
}

void ForceDuty(uint16_t GH_A, uint16_t GL_A, uint16_t GH_B, uint16_t GL_B, uint16_t GH_C, uint16_t GL_C)
{
	GH_A_DC = GH_A;
	GL_A_DC = GL_A;
	GH_B_DC = GH_B;
	GL_B_DC = GL_B;
	GH_C_DC = GH_C;
	GL_C_DC = GL_C;
}

/**
 * This function should exclusively be called by the change notify interrupt to
 * ensure that all hall events have been recorded and that the time between events
 * is appropriately read.
 *
 * The only other conceivable time that this would be called is if the motor is currently
 * not spinning.
 *
 * TODO: Look into preempting this interrupt and turning off interrupts when adding
 * data to the circular buffers as they are non-reentrant.  Calling this function
 * will break those libraries.
 * @param torque
 * @param direction
 */
void TrapUpdate(uint16_t torque, uint16_t direction)
{
	motorInfo.hallCount++;
	// Reading from 32-bit timer
	timerCurr = TMR2;
	timerCurr |= ((uint32_t) TMR3HLD << 16) & 0xFFFF0000;
	timer += timerCurr;
	// Writing to 32-bit timer
	TMR3HLD = 0; //Write msw to the Type C timer holding register
	TMR2 = 0; //Write lsw to the Type B timer register
	changeState = 1;

	if (direction == CW) {
		if ((HALL1 && HALL2 && !HALL3)) {
			GH_A_DC = 0;
			GL_A_DC = 0;
			GH_B_DC = torque;
			GL_B_DC = 0;
			GH_C_DC = 0;
			GL_C_DC = torque;
			LED1 = 1;
			LED2 = 1;
			LED3 = 0;
		} else if ((!HALL1 && HALL2 && !HALL3)) {
			GH_A_DC = 0;
			GL_A_DC = torque;
			GH_B_DC = torque;
			GL_B_DC = 0;
			GH_C_DC = 0;
			GL_C_DC = 0;
			LED1 = 0;
			LED2 = 1;
			LED3 = 0;
		} else if ((!HALL1 && HALL2 && HALL3)) {
			GH_A_DC = 0;
			GL_A_DC = torque;
			GH_B_DC = 0;
			GL_B_DC = 0;
			GH_C_DC = torque;
			GL_C_DC = 0;
			LED1 = 0;
			LED2 = 1;
			LED3 = 1;
		} else if ((!HALL1 && !HALL2 && HALL3)) {
			GH_A_DC = 0;
			GL_A_DC = 0;
			GH_B_DC = 0;
			GL_B_DC = torque;
			GH_C_DC = torque;
			GL_C_DC = 0;
			LED1 = 0;
			LED2 = 0;
			LED3 = 1;
		} else if ((HALL1 && !HALL2 && HALL3)) {
			GH_A_DC = torque;
			GL_A_DC = 0;
			GH_B_DC = 0;
			GL_B_DC = torque;
			GH_C_DC = 0;
			GL_C_DC = 0;
			LED1 = 1;
			LED2 = 0;
			LED3 = 1;
		} else if ((HALL1 && !HALL2 && !HALL3)) {
			GH_A_DC = torque;
			GL_A_DC = 0;
			GH_B_DC = 0;
			GL_B_DC = 0;
			GH_C_DC = 0;
			GL_C_DC = torque;
			LED1 = 1;
			LED2 = 0;
			LED3 = 0;
		}
	} else if (direction == CCW) {
		if ((HALL1 && HALL2 && !HALL3)) {
			GH_A_DC = 0;
			GL_A_DC = 0;
			GH_B_DC = 0;
			GL_B_DC = torque;
			GH_C_DC = torque;
			GL_C_DC = 0;
			LED1 = 1;
			LED2 = 1;
			LED3 = 0;
		} else if ((!HALL1 && HALL2 && !HALL3)) {
			GH_A_DC = torque;
			GL_A_DC = 0;
			GH_B_DC = 0;
			GL_B_DC = torque;
			GH_C_DC = 0;
			GL_C_DC = 0;
			LED1 = 0;
			LED2 = 1;
			LED3 = 0;
		} else if ((!HALL1 && HALL2 && HALL3)) {
			GH_A_DC = torque;
			GL_A_DC = 0;
			GH_B_DC = 0;
			GL_B_DC = 0;
			GH_C_DC = 0;
			GL_C_DC = torque;
			LED1 = 0;
			LED2 = 1;
			LED3 = 1;
		} else if ((!HALL1 && !HALL2 && HALL3)) {
			GH_A_DC = 0;
			GL_A_DC = 0;
			GH_B_DC = torque;
			GL_B_DC = 0;
			GH_C_DC = 0;
			GL_C_DC = torque;
			LED1 = 0;
			LED2 = 0;
			LED3 = 1;
		} else if ((HALL1 && !HALL2 && HALL3)) {
			GH_A_DC = 0;
			GL_A_DC = torque;
			GH_B_DC = torque;
			GL_B_DC = 0;
			GH_C_DC = 0;
			GL_C_DC = 0;
			LED1 = 1;
			LED2 = 0;
			LED3 = 1;
		} else if ((HALL1 && !HALL2 && !HALL3)) {
			GH_A_DC = 0;
			GL_A_DC = torque;
			GH_B_DC = 0;
			GL_B_DC = 0;
			GH_C_DC = torque;
			GL_C_DC = 0;
			LED1 = 1;
			LED2 = 0;
			LED3 = 0;
		}
	}
}

void __attribute__((__interrupt__)) _CNInterrupt(void)
{
	TrapUpdate(commandedTorque, CW);
	IFS1bits.CNIF = 0; // Clear CN interrupt
}