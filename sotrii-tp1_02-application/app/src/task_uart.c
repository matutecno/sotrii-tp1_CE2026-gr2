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
#include "task_uart.h"
#include "logger.h"
#include <string.h>
/********************** internal data declaration ****************************/

/********************** internal data declaration ****************************/

/********************** internal functions declaration ***********************/
void task_uart_tx(void *parameters);
void task_uart_rx(void *parameters);

/********************** internal data definition *****************************/
const char *p_task_uart_tx_wait_250mS	= "   ==> Task UART TX - Wait:   250mS";
const char *p_task_uart_rx_wait_250mS	= "   ==> Task UART RX - Wait:   250mS";
void task_uart_tx(void *parameters)
{
    uart_device_t *device = parameters;
    uart_spool_t *spool;

    	/* Print out: Task Initialized */
	LOGGER_INFO(" ");
	LOGGER_INFO("%s is running - Tick [mS] = %3d", pcTaskGetName(NULL), (int)xTaskGetTickCount());

    for (;;) {
        if (xQueueReceive(device->output_queue, &spool, portMAX_DELAY) == pdPASS) {
            if ((HAL_UART_Transmit_IT(device->channel, spool->data,
                                      (uint16_t)spool->length) == HAL_OK) &&
                (xSemaphoreTake(device->tx_done, device->timeout) == pdPASS)) {
                device->stats.tx_frames++;
            } else {
                device->stats.dropped_frames++;
                (void)HAL_UART_AbortTransmit_IT(device->channel);
            }
            vPortFree(spool->data); vPortFree(spool);
        }
    }
}

/* Task UART RX thread */
void task_uart_rx(void *parameters)
{
    uart_device_t *device = parameters;

    /* Print out: Task Initialized */
	LOGGER_INFO(" ");
	LOGGER_INFO("%s is running - Tick [mS] = %3d", pcTaskGetName(NULL), (int)xTaskGetTickCount());

	/* As per most tasks, this task is implemented in an infinite loop. */
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        device->rx_work_buffer[device->rx_work_length++] = device->rx_byte;
        if ((device->rx_byte == '\n') ||
            (device->rx_work_length == UART_DEVICE_RX_FRAME_SIZE)) {
            uart_spool_t *spool = pvPortMalloc(sizeof(*spool));
            if (spool != NULL) spool->data = pvPortMalloc(device->rx_work_length);
            if ((spool != NULL) && (spool->data != NULL)) {
                memcpy(spool->data, device->rx_work_buffer, device->rx_work_length);
                spool->length = device->rx_work_length;
                if (xQueueSend(device->input_queue, &spool, 0U) == pdPASS)
                    device->stats.rx_frames++;
                else { vPortFree(spool->data); vPortFree(spool); device->stats.dropped_frames++; }
            } else {
                if (spool != NULL) vPortFree(spool);
                device->stats.dropped_frames++;
            }
            device->rx_work_length = 0U;
        }
        if (HAL_UART_Receive_IT(device->channel, &device->rx_byte, 1U) != HAL_OK)
            device->stats.uart_errors++;
    }
}

/********************** end of file ******************************************/
