/*
 * Copyright (c) 2009, Swedish Institute of Computer Science.
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
 */

/**
 * \file
 *         Collision-based dynamic duty cycling. 
 * \author
 *         Mat Wymore <mlwymore@iastate.edu>
 */

#include "contiki.h"
#include "sys/clock.h"
#include "net/mac/ddc.h"

#define DEBUG 1

#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINTADDR(addr) PRINTF(" %02x%02x:%02x%02x:%02x%02x:%02x%02x ", ((uint8_t *)addr)[0], ((uint8_t *)addr)[1], ((uint8_t *)addr)[2], ((uint8_t *)addr)[3], ((uint8_t *)addr)[4], ((uint8_t *)addr)[5], ((uint8_t *)addr)[6], ((uint8_t *)addr)[7])
#else
#define PRINTF(...)
#define PRINTADDR(addr)
#endif

#ifdef DDC_CONF_MAX_MULT
#define DDC_MAX_MULT DDC_CONF_MAX_MULT
#else
#define DDC_MAX_MULT 32
#endif

#define DDC_MIN_DIV 1
#define DDC_MAX_DIV 16

int mult;
int div;

/*---------------------------------------------------------------------------*/
static int
init(void)
{
	PRINTF("DDC: Using Collision-DDC.\n");
	mult = 1;
	div = 1;
  return 1;
}
/*---------------------------------------------------------------------------*/
static int
use_ddc(void)
{
	return 1;
}
/*---------------------------------------------------------------------------*/
static int
multiplier(void)
{
	//PRINTF("DDC: mult %d\n", mult);
	return mult;
}
/*---------------------------------------------------------------------------*/
static int
divisor(void)
{
	//PRINTF("DDC: div %d\n", div);
	return div;
}
/*---------------------------------------------------------------------------*/
static void
check_limits(void)
{
	mult = MIN(mult, DDC_MAX_MULT);
	div = MIN(div, DDC_MAX_DIV);
}
/*---------------------------------------------------------------------------*/
static int
increase_interval(void)
{
	if(mult > 1 || (mult == 1 && div == 1)) {
		mult = mult * 2;
	} else {
		div = div / 2;
	}
	check_limits();
	PRINTF("DDC: Increasing interval. Div: %d, Mult: %d\n", div, mult);
	return 1;
}
/*---------------------------------------------------------------------------*/
static int
decrease_interval(void)
{
	if(mult > 1) {
		mult = mult / 2;
	} else {
		div = div * 2;
	}
	check_limits();
	PRINTF("DDC: Decreasing interval. Div: %d, Mult: %d\n", div, mult);
	return 1;
}
/*---------------------------------------------------------------------------*/
static int
input(void)
{
	/*static clock_time_t last_input_clock = 0;
	clock_time_t current_clock = clock_time();
	if(current_clock - last_input_clock < CLOCK_SECOND * mult / div / 2) {
		decrease_interval();
	}
	last_input_clock = current_clock;*/
	return 1;
}
/*---------------------------------------------------------------------------*/
static int
collision(void)
{
	decrease_interval();
	return 1;
}
/*---------------------------------------------------------------------------*/
static int
empty_wakeup(void)
{
	increase_interval();
	return 1;
}
/*---------------------------------------------------------------------------*/
const struct ddc_driver collisionddc_driver = {
  init,
  use_ddc,
  multiplier,
  divisor,
  input,
  collision,
  empty_wakeup,
};
