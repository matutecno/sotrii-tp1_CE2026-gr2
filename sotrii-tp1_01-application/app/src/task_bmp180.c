/*
 * Copyright (c) 2026 Juan Manuel Cruz <jcruz@fi.uba.ar> <jcruz@frba.utn.edu.ar>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * @author : Juan Manuel Cruz <jcruz@fi.uba.ar> <jcruz@frba.utn.edu.ar>
 */

/********************** inclusions *******************************************/
/* Project includes */
#include "main.h"
#include "cmsis_os.h"

/* Demo includes */
#include "logger.h"
#include "dwt.h"

/* Application & Tasks includes */
#include "board.h"
#include "app.h"
#include "task_i2c_interface.h"
#include "task_bmp180.h"

/********************** macros and definitions *******************************/
#define BMP180_ADDR			0x77

#define BMP180_REG_CAL_AC5	0xB2
#define BMP180_REG_CAL_AC6	0xB4
#define BMP180_REG_CAL_MC	0xBC
#define BMP180_REG_CAL_MD	0xBE

#define BMP180_REG_CTRL		0xF4
#define BMP180_REG_DATA		0xF6
#define BMP180_CMD_TEMP		0x2E

#define BMP180_TEMP_CONV_DEL	(pdMS_TO_TICKS(5ul))
#define TASK_BMP180_DEL_MAX		(pdMS_TO_TICKS(1000ul))

/********************** internal data definition *****************************/
static uint16_t ac5, ac6;
static int16_t  mc, md;

/********************** internal functions definition ************************/
static uint16_t bmp180_read16(uint8_t reg)
{
	uint8_t buf[2] = { 0, 0 };
	read_i2c(&hi2c1, BMP180_ADDR, reg, buf, sizeof(buf));
	return ((uint16_t)buf[0] << 8) | buf[1];
}

static void bmp180_read_calibration(void)
{
	ac5 = bmp180_read16(BMP180_REG_CAL_AC5);
	ac6 = bmp180_read16(BMP180_REG_CAL_AC6);
	mc  = (int16_t)bmp180_read16(BMP180_REG_CAL_MC);
	md  = (int16_t)bmp180_read16(BMP180_REG_CAL_MD);
}

static int32_t bmp180_read_temperature(void)
{
	write_i2c(&hi2c1, BMP180_ADDR, BMP180_REG_CTRL, BMP180_CMD_TEMP);
	vTaskDelay(BMP180_TEMP_CONV_DEL);

	int32_t ut = bmp180_read16(BMP180_REG_DATA);

	int32_t x1 = ((ut - ac6) * ac5) >> 15;
	int32_t x2 = ((int32_t)mc << 11) / (x1 + md);
	int32_t b5 = x1 + x2;

	return (b5 + 8) >> 4;	/* temperatura en decimas de grado */
}

/********************** external functions definition ************************/
/* Task thread */
void task_bmp180(void *parameters)
{
	bmp180_read_calibration();

	/* Print out: Task Initialized */
	LOGGER_INFO(" ");
	LOGGER_INFO("  %s is running - Tick [mS] = %lu", pcTaskGetName(NULL), xTaskGetTickCount());

	/* As per most tasks, this task is implemented in an infinite loop. */
	for (;;)
	{
		int32_t t = bmp180_read_temperature();

		/* Ejercita ioctl_i2c del driver (permite medir su WCET y evita que
		 * --gc-sections elimine la funcion al no tener otro caller). */
		uint32_t rx_wcet_us;
		ioctl_i2c(&hi2c1, I2C_GET_RX_WCET_US, &rx_wcet_us);

		LOGGER_INFO("   ==> Task BMP180 - Temp: %d.%d C", (int)(t / 10), (int)(t % 10));

		vTaskDelay(TASK_BMP180_DEL_MAX);
	}
}

/********************** end of file ******************************************/
