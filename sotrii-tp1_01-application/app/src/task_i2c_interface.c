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
#include "app_it.h"
#include "task_i2c.h"
#include "task_i2c_attribute.h"
#include "task_i2c_interface.h"

/********************** macros and definitions *******************************/

/********************** internal data declaration ****************************/

/********************** internal data declaration ****************************/
task_i2c_dta_t task_i2c_dta;

/********************** internal functions declaration ***********************/

/********************** internal data definition *****************************/

/********************** external data declaration ****************************/
uint32_t g_write_i2c_wcet_cy;
uint32_t g_read_i2c_wcet_cy;
uint32_t g_ioctl_i2c_wcet_cy;

/********************** external functions definition ************************/
/* Interface functions */
void open_i2c(I2C_HandleTypeDef *h_i2c_device)
{
	BaseType_t ret;
	task_i2c_dta_t *p_task_i2c_dta = &task_i2c_dta;

	p_task_i2c_dta->device_id = h_i2c_device;

    /* Before a queue is used it must be explicitly created.
	 * Check the queue was created successfully.
     * Add queue to registry. */
	p_task_i2c_dta->queue_tx = xQueueCreate(5, sizeof(task_i2c_tx_dta_t));
	configASSERT(NULL != p_task_i2c_dta->queue_tx);
	p_task_i2c_dta->sem_tx_done = xSemaphoreCreateBinary();
	configASSERT(NULL != p_task_i2c_dta->sem_tx_done);
	vQueueAddToRegistry(p_task_i2c_dta->queue_tx, "Task I2C Tx Queue Handle");

	p_task_i2c_dta->queue_rx = xQueueCreate(10, sizeof(uint8_t));
	configASSERT(NULL != p_task_i2c_dta->queue_rx);
	vQueueAddToRegistry(p_task_i2c_dta->queue_rx, "Task I2C Rx Queue Handle");

    /* Before a task is executed it must be explicitly created.
	 * Check the task was created successfully. */
    ret = xTaskCreate(task_i2c_tx, "Task I2C Tx", (configMINIMAL_STACK_SIZE),
					  (void *)p_task_i2c_dta,
					  (tskIDLE_PRIORITY + 1ul), &p_task_i2c_dta->task_tx);
    configASSERT(pdPASS == ret);

    ret = xTaskCreate(task_i2c_rx, "Task I2C Rx", (configMINIMAL_STACK_SIZE),
    				  (void *)p_task_i2c_dta,
					  (tskIDLE_PRIORITY + 1ul), &p_task_i2c_dta->task_rx);
    configASSERT(pdPASS == ret);
}

void release_i2c(I2C_HandleTypeDef *h_i2c_device)
{
	task_i2c_dta_t *p_task_i2c_dta = &task_i2c_dta;

	p_task_i2c_dta->device_id = h_i2c_device;

	// Check which version of the i2c triggered this function
	if (p_task_i2c_dta->device_id == h_i2c_device)
	{
	    vQueueUnregisterQueue(p_task_i2c_dta->queue_tx);
		vQueueDelete(p_task_i2c_dta->queue_tx);
	    vQueueUnregisterQueue(p_task_i2c_dta->queue_rx);
		vQueueDelete(p_task_i2c_dta->queue_rx);

		vTaskDelete(p_task_i2c_dta->task_tx);
		vTaskDelete(p_task_i2c_dta->task_rx);
	}
}

void write_i2c(I2C_HandleTypeDef *h_i2c_device, uint16_t dev_address, uint8_t dev_data)
{
	task_i2c_dta_t *p_task_i2c_dta = &task_i2c_dta;

	p_task_i2c_dta->device_id = h_i2c_device;

	cycle_counter_reset();

	// Check which version of the i2c triggered this function
	if (p_task_i2c_dta->device_id == h_i2c_device)
	{
		task_i2c_tx_dta_t task_i2c_tx_dta;

		task_i2c_tx_dta.address = dev_address;
		task_i2c_tx_dta.data = dev_data;

		xQueueSend(p_task_i2c_dta->queue_tx, &task_i2c_tx_dta, portMAX_DELAY);
	}

	uint32_t cy = cycle_counter_get();
	if (cy > g_write_i2c_wcet_cy)
		g_write_i2c_wcet_cy = cy;
}

bool read_i2c(I2C_HandleTypeDef *h_i2c_device, uint8_t *data)
{
	/* Prevent unused argument(s) compilation warning */
	task_i2c_dta_t *p_task_i2c_dta = &task_i2c_dta;

	p_task_i2c_dta->device_id = h_i2c_device;

	cycle_counter_reset();

	bool ok = false;
	if(p_task_i2c_dta -> device_id == h_i2c_device)
	{
		if (xQueueReceive(p_task_i2c_dta->queue_rx, data, 0) == pdPASS)
			ok = true;
	}

	uint32_t cy = cycle_counter_get();
	if (cy > g_read_i2c_wcet_cy)
		g_read_i2c_wcet_cy = cy;

	return ok;
}

extern uint32_t g_task_xxxx_tx_runtime_us;
extern uint32_t g_task_xxxx_rx_runtime_us;

bool ioctl_i2c(I2C_HandleTypeDef *h_i2c_device, uint8_t cmd, void *arg)
{
      task_i2c_dta_t *p_task_i2c_dta = &task_i2c_dta;

      p_task_i2c_dta->device_id = h_i2c_device;

      cycle_counter_reset();

      if (p_task_i2c_dta->device_id != h_i2c_device)
              return false;

      switch (cmd)
      {
              case I2C_GET_TX_WCET_US:
                      *(uint32_t *)arg = g_task_xxxx_tx_runtime_us;
                      break;

              case I2C_GET_RX_WCET_US:
                      *(uint32_t *)arg = g_task_xxxx_rx_runtime_us;
                      break;

              case I2C_RESET_STATS:
                      g_task_xxxx_tx_runtime_us = 0;
                      g_task_xxxx_rx_runtime_us = 0;
                      break;

              default:
                      return false;
      }

      uint32_t cy = cycle_counter_get();
      if (cy > g_ioctl_i2c_wcet_cy)
              g_ioctl_i2c_wcet_cy = cy;

      return true;
}

/* Callback de fin de transmisión OK (esclavo hizo ACK) */
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
	task_i2c_dta_t *p_task_i2c_dta = &task_i2c_dta;

	if (hi2c == p_task_i2c_dta->device_id)
	{
		BaseType_t x_higher_pw_token = pdFALSE;
		xSemaphoreGiveFromISR(p_task_i2c_dta->sem_tx_done, &x_higher_pw_token);
		portYIELD_FROM_ISR(x_higher_pw_token);
	}
}

/* Callback de error (ej. NACK: no hay esclavo en esa dirección) */
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
	task_i2c_dta_t *p_task_i2c_dta = &task_i2c_dta;

	if (hi2c == p_task_i2c_dta->device_id)
	{
		BaseType_t x_higher_pw_token = pdFALSE;
		xSemaphoreGiveFromISR(p_task_i2c_dta->sem_tx_done, &x_higher_pw_token);
		portYIELD_FROM_ISR(x_higher_pw_token);
	}
}

/********************** end of file ******************************************/
