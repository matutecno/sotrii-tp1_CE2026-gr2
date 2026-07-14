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

#ifndef TASK_UART_ATTRIBUTE_H_
#define TASK_UART_ATTRIBUTE_H_

/********************** CPP guard ********************************************/
#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "semphr.h"
#include <stddef.h>
#include <stdint.h>

#define UART_DEVICE_QUEUE_LENGTH       8U
#define UART_DEVICE_RX_FRAME_SIZE      64U
#define UART_DEVICE_TIMEOUT_DEFAULT    pdMS_TO_TICKS(1000U)

typedef enum {
    UART_DEVICE_OK = 0,
    UART_DEVICE_ERROR = -1,
    UART_DEVICE_BUSY = -2,
    UART_DEVICE_TIMEOUT = -3,
    UART_DEVICE_NO_MEMORY = -4,
    UART_DEVICE_INVALID = -5
} uart_device_status_t;

typedef enum {
    UART_IOCTL_SET_TIMEOUT = 0,
    UART_IOCTL_FLUSH_RX,
    UART_IOCTL_FLUSH_TX,
    UART_IOCTL_GET_STATS
} uart_ioctl_cmd_t;

typedef struct {
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t dropped_frames;
    uint32_t uart_errors;
} uart_device_stats_t;

typedef struct {
    uint8_t *data;
    size_t length;
} uart_spool_t;

typedef struct {
    uint32_t device_id;
    UART_HandleTypeDef *channel;
    QueueHandle_t input_queue;
    QueueHandle_t output_queue;
    SemaphoreHandle_t tx_done;
    TaskHandle_t tx_gatekeeper;
    TaskHandle_t rx_gatekeeper;
    TickType_t timeout;
    uint8_t rx_byte;
    uint8_t *rx_work_buffer;
    size_t rx_work_length;
    volatile uint8_t opened;
    uart_device_stats_t stats;
} uart_device_t;

#ifdef __cplusplus
}
#endif

#endif /* TASK_UART_ATTRIBUTE_H_ */

/********************** end of file ******************************************/
