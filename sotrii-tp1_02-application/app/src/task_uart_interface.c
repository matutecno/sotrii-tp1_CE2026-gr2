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

 /* Application & Tasks includes */
#include "task_uart_interface.h"
#include "task_uart.h"
#include <string.h>

static uart_device_t *active_device;

static void free_queue(QueueHandle_t queue)
{
    uart_spool_t *spool;
    while ((queue != NULL) && (xQueueReceive(queue, &spool, 0U) == pdPASS)) {
        if (spool != NULL) {
            vPortFree(spool->data);
            vPortFree(spool);
        }
    }
}

uart_device_status_t open_uart(uart_device_t *device, uint32_t device_id,
                               UART_HandleTypeDef *channel)
{
    BaseType_t result;
    if ((device == NULL) || (channel == NULL) || (active_device != NULL)) {
        return UART_DEVICE_INVALID;
    }
    memset(device, 0, sizeof(*device));
    device->device_id = device_id;
    device->channel = channel;
    device->timeout = UART_DEVICE_TIMEOUT_DEFAULT;
    device->input_queue = xQueueCreate(UART_DEVICE_QUEUE_LENGTH, sizeof(uart_spool_t *));
    device->output_queue = xQueueCreate(UART_DEVICE_QUEUE_LENGTH, sizeof(uart_spool_t *));
    device->tx_done = xSemaphoreCreateBinary();
    device->rx_work_buffer = pvPortMalloc(UART_DEVICE_RX_FRAME_SIZE);
    if ((device->input_queue == NULL) || (device->output_queue == NULL) ||
        (device->tx_done == NULL) || (device->rx_work_buffer == NULL)) {
        (void)release_uart(device);
        return UART_DEVICE_NO_MEMORY;
    }
    vQueueAddToRegistry(device->input_queue, "UART RX spool");
    vQueueAddToRegistry(device->output_queue, "UART TX spool");
    device->opened = 1U;
    active_device = device;
    result = xTaskCreate(task_uart_tx, "UART TX gate", 2U * configMINIMAL_STACK_SIZE,
                         device, tskIDLE_PRIORITY + 2U, &device->tx_gatekeeper);
    if (result == pdPASS) {
        result = xTaskCreate(task_uart_rx, "UART RX gate", 2U * configMINIMAL_STACK_SIZE,
                             device, tskIDLE_PRIORITY + 2U, &device->rx_gatekeeper);
    }
    if ((result != pdPASS) ||
        (HAL_UART_Receive_IT(channel, &device->rx_byte, 1U) != HAL_OK)) {
        (void)release_uart(device);
        return UART_DEVICE_ERROR;
    }
    return UART_DEVICE_OK;
}

uart_device_status_t release_uart(uart_device_t *device)
{
    if (device == NULL) return UART_DEVICE_INVALID;
    device->opened = 0U;
    (void)HAL_UART_Abort_IT(device->channel);
    if (device->tx_gatekeeper != NULL) vTaskDelete(device->tx_gatekeeper);
    if (device->rx_gatekeeper != NULL) vTaskDelete(device->rx_gatekeeper);
    free_queue(device->input_queue);
    free_queue(device->output_queue);
    if (device->input_queue != NULL) vQueueDelete(device->input_queue);
    if (device->output_queue != NULL) vQueueDelete(device->output_queue);
    if (device->tx_done != NULL) vSemaphoreDelete(device->tx_done);
    vPortFree(device->rx_work_buffer);
    if (active_device == device) active_device = NULL;
    memset(device, 0, sizeof(*device));
    return UART_DEVICE_OK;
}

uart_device_status_t write_uart(uart_device_t *device, const void *data,
                                size_t length, TickType_t timeout)
{
    uart_spool_t *spool;
    if ((device == NULL) || !device->opened || (data == NULL) ||
        (length == 0U) || (length > UINT16_MAX)) return UART_DEVICE_INVALID;
    spool = pvPortMalloc(sizeof(*spool));
    if (spool == NULL) return UART_DEVICE_NO_MEMORY;
    spool->data = pvPortMalloc(length);
    if (spool->data == NULL) { vPortFree(spool); return UART_DEVICE_NO_MEMORY; }
    memcpy(spool->data, data, length);
    spool->length = length;
    if (xQueueSend(device->output_queue, &spool, timeout) != pdPASS) {
        vPortFree(spool->data); vPortFree(spool); return UART_DEVICE_TIMEOUT;
    }
    return UART_DEVICE_OK;
}

uart_device_status_t read_uart(uart_device_t *device, void *data, size_t capacity,
                               size_t *length, TickType_t timeout)
{
    uart_spool_t *spool;
    if ((device == NULL) || !device->opened || (data == NULL) ||
        (length == NULL) || (capacity == 0U)) return UART_DEVICE_INVALID;
    if (xQueueReceive(device->input_queue, &spool, timeout) != pdPASS)
        return UART_DEVICE_TIMEOUT;
    *length = (spool->length < capacity) ? spool->length : capacity;
    memcpy(data, spool->data, *length);
    vPortFree(spool->data); vPortFree(spool);
    return UART_DEVICE_OK;
}

uart_device_status_t ioctl_uart(uart_device_t *device, uart_ioctl_cmd_t command,
                                void *argument)
{
    if ((device == NULL) || !device->opened) return UART_DEVICE_INVALID;
    switch (command) {
    case UART_IOCTL_SET_TIMEOUT:
        if (argument == NULL) return UART_DEVICE_INVALID;
        device->timeout = *(TickType_t *)argument; break;
    case UART_IOCTL_FLUSH_RX: free_queue(device->input_queue); break;
    case UART_IOCTL_FLUSH_TX: free_queue(device->output_queue); break;
    case UART_IOCTL_GET_STATS:
        if (argument == NULL) return UART_DEVICE_INVALID;
        *(uart_device_stats_t *)argument = device->stats; break;
    default: return UART_DEVICE_INVALID;
    }
    return UART_DEVICE_OK;
}

void uart_device_tx_complete_isr(UART_HandleTypeDef *channel)
{
    BaseType_t wake = pdFALSE;
    if ((active_device != NULL) && (active_device->channel == channel)) {
        xSemaphoreGiveFromISR(active_device->tx_done, &wake);
        portYIELD_FROM_ISR(wake);
    }
}

void uart_device_rx_complete_isr(UART_HandleTypeDef *channel)
{
    BaseType_t wake = pdFALSE;
    if ((active_device != NULL) && (active_device->channel == channel)) {
        vTaskNotifyGiveFromISR(active_device->rx_gatekeeper, &wake);
        portYIELD_FROM_ISR(wake);
    }
}

void uart_device_error_isr(UART_HandleTypeDef *channel)
{
    if ((active_device != NULL) && (active_device->channel == channel)) {
        active_device->stats.uart_errors++;
        (void)HAL_UART_Receive_IT(channel, &active_device->rx_byte, 1U);
    }
}
