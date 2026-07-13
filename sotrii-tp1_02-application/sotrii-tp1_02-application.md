# Análisis del código fuente — STM32 y FreeRTOS

## 1. `app.c`

Es el punto principal de inicialización de la aplicación.

### Funcionamiento

La función `app_init()`:

1. Inicializa los contadores globales:
   - `g_app_tick_cnt`;
   - `g_task_idle_cnt`;
   - `g_app_stack_overflow_cnt`.
2. Imprime información de inicio mediante `LOGGER_INFO`.
3. Crea `Task Sender` y `Task Receiver` con:
   - prioridad `tskIDLE_PRIORITY + 1`;
   - pila de `2 * configMINIMAL_STACK_SIZE`;
   - parámetro de entrada `NULL`.
4. Verifica cada creación mediante `configASSERT`.
5. Consulta el heap libre con `xPortGetFreeHeapSize()`.
6. Llama a `open_uart(&huart2)`.
7. Inicializa las variables utilizadas por los callbacks con `app_it_init()`.
8. Inicializa el contador de ciclos DWT mediante `cycle_counter_init()`.

### Observaciones

- Ambas tareas tienen la misma prioridad.
- El resultado de `xPortGetFreeHeapSize()` se guarda en `ret`, de tipo `BaseType_t`, y luego no se utiliza. Sería preferible almacenarlo en una variable `size_t`.
- `open_uart()` actualmente está vacía, por lo que no realiza una inicialización adicional.
- El DWT debería inicializarse antes de cualquier operación que pudiera utilizarlo.
- Los comentarios mencionan colas y semáforos, pero ninguno se crea.

---

## 2. `app_it.c`

Gestiona variables y callbacks relacionados con interrupciones.

### `app_it_init()`

Deshabilita temporalmente las interrupciones, inicializa:

```c
hal_xxxx_callback_flag = false;
hal_xxxx_callback_cnt = 0;
hal_xxxx_callback_runtime_us = 0;
```

y vuelve a habilitarlas.

### `HAL_GPIO_EXTI_Callback()`

Comprueba si la interrupción fue producida por `BTN_A_PIN`, pero no ejecuta ninguna acción. Es un punto preparado para implementar el tratamiento del botón.

### `HAL_UART_TxCpltCallback()`

Cuando finaliza una transmisión de `USART2`:

- activa `hal_xxxx_callback_flag`;
- incrementa `hal_xxxx_callback_cnt`;
- guarda el tiempo leído mediante `cycle_counter_get_time_us()`.

### Observaciones

- No se observa ninguna llamada a `HAL_UART_Transmit_IT()` o `HAL_UART_Transmit_DMA()`, por lo que este callback no sería activado por el código analizado.
- La variable `hal_xxxx_callback_runtime_us` no mide necesariamente la duración del callback, porque el contador no se reinicia al ingresar.
- `volatile` evita ciertas optimizaciones del compilador, pero no reemplaza una cola, un semáforo o una notificación de FreeRTOS.
- `CPSID i` y `CPSIE i` no conservan el estado anterior de las interrupciones.

---

## 3. `task_sender.c`

Define la función ejecutada por `Task Sender`.

### Funcionamiento

Al comenzar:

- inicializa `g_task_sender_cnt`;
- imprime el nombre de la tarea y el tick actual.

Luego ejecuta un bucle infinito:

```c
g_task_sender_cnt++;
LOGGER_INFO(...);
vTaskDelay(pdMS_TO_TICKS(250));
```

La tarea incrementa un contador, imprime un mensaje y queda bloqueada durante 250 ms.

### Observaciones

- A pesar de su nombre, no envía datos.
- No utiliza UART, colas, semáforos ni notificaciones.
- `vTaskDelay()` introduce un retardo relativo; el período real incluye también el tiempo empleado en ejecutar e imprimir el mensaje.
- Para una periodicidad más estable podría utilizarse `vTaskDelayUntil()`.
- `TASK_SENDER_DEL_ZERO` no se utiliza.

---

## 4. `task_receiver.c`

Define la función ejecutada por `Task Receiver`.

### Funcionamiento

Inicializa `g_task_receiver_cnt`, imprime información de inicio y ejecuta continuamente:

```c
g_task_receiver_cnt++;
LOGGER_INFO(...);
vTaskDelay(pdMS_TO_TICKS(250));
```

### Observaciones

- No recibe datos ni espera eventos.
- No existe una conexión funcional con `Task Sender`.
- Su comportamiento es prácticamente idéntico al de `Task Sender`.
- Como ambas tareas tienen la misma prioridad y el mismo retardo, normalmente ejecutarán sus ciclos en momentos cercanos, aunque el orden exacto no debe considerarse garantizado.
- `TASK_RECEIVER_DEL_ZERO` no se utiliza.

---

## 5. `task_uart.c`

Define dos funciones de tarea:

- `task_uart_tx()`;
- `task_uart_rx()`.

### Funcionamiento actual

Cada tarea:

1. inicializa sus contadores;
2. reinicia el contador DWT;
3. conmuta el estado de `LED_A`;
4. mide el tiempo empleado;
5. imprime un mensaje;
6. espera 250 ms.

### Observaciones

- Ninguna de estas tareas se crea en `app_init()`, por lo que actualmente no se ejecutan.
- `task_uart_tx()` no transmite por UART.
- `task_uart_rx()` no recibe por UART.
- Ambas tareas modifican el mismo LED; si se ejecutaran casi simultáneamente, podrían anular visualmente sus cambios.
- Ambas reinician un contador DWT global, lo que puede interferir con otras mediciones.
- Los nombres `xxxx` indican que el archivo todavía conserva elementos genéricos de una plantilla.

---

## 6. `task_uart_interface.c`

Implementa una interfaz abstracta para el dispositivo UART:

```c
open_uart()
release_uart()
write_uart()
read_uart()
ioctl_uart()
```

### Funcionamiento actual

Todas las funciones únicamente aplican `UNUSED()` al parámetro recibido. En consecuencia, no realizan ninguna operación.

### Observaciones

- `open_uart()` no inicializa ni habilita USART2.
- `write_uart()` no recibe buffer ni longitud.
- `read_uart()` no recibe buffer ni longitud.
- `ioctl_uart()` no recibe un comando de configuración.
- Las firmas actuales son insuficientes para implementar una interfaz UART general.

Una interfaz funcional debería recibir, como mínimo, el dispositivo, un buffer, una cantidad de bytes y devolver un estado de la operación.

---

## 7. `freertos.c`

Implementa hooks llamados automáticamente por FreeRTOS cuando están habilitados en `FreeRTOSConfig.h`.

### `vApplicationIdleHook()`

Incrementa `g_task_idle_cnt` cuando no hay tareas de mayor prioridad listas para ejecutarse.

El contador representa la cantidad de ejecuciones del hook, no tiempo ni porcentaje de uso del procesador.

### `vApplicationTickHook()`

Incrementa `g_app_tick_cnt` en cada tick del sistema.

El valor solo equivale directamente a milisegundos si `configTICK_RATE_HZ` es igual a 1000 Hz.

### `vApplicationStackOverflowHook()`

Se ejecuta cuando FreeRTOS detecta un desbordamiento de pila. Entra en una sección crítica y fuerza:

```c
configASSERT(0);
```

para detener el sistema durante la depuración.

### Observaciones

- `g_app_stack_overflow_cnt++` se encuentra después de `configASSERT(0)` y probablemente nunca se ejecutará.
- Los parámetros `xTask` y `pcTaskName` no se utilizan, aunque permitirían identificar la tarea que desbordó su pila.
- Los hooks dependen de que sus opciones correspondientes estén activadas en `FreeRTOSConfig.h`.

---

## 8. `task_uart_interface.h`

Este archivo de cabecera declara la interfaz pública utilizada para controlar el dispositivo UART. Contiene los prototipos de:
```c
- open_uart()
- release_uart()
- write_uart()
- read_uart()
- ioctl_uart()
```

Las guardas:

```c
#ifndef TASK_UART_INTERFACE_H_
#define TASK_UART_INTERFACE_H_
```

evitan que el contenido del encabezado se procese más de una vez dentro de una misma unidad de compilación.

El bloque:
```c
#ifdef __cplusplus
extern "C" {
#endif
```
permite incluir el encabezado desde código C++ conservando el enlace de las funciones como código C.

### Observaciones
-  El encabezado utiliza UART_HandleTypeDef, pero no incluye el archivo donde se define este tipo. Por ello, depende de que otro encabezado, como main.h, haya sido incluido previamente.
-  Para hacerlo autosuficiente debería incluir el encabezado HAL correspondiente a UART o declarar apropiadamente el tipo requerido.
-  El uso de extern en los prototipos de funciones es válido, aunque redundante, porque las funciones declaradas a nivel global ya poseen enlace externo por defecto en C.
- Las funciones write_uart() y read_uart() no reciben un buffer ni una longitud, por lo que sus firmas actuales no permiten indicar qué datos deben transferirse.
- ioctl_uart() tampoco recibe un comando o argumento que permita seleccionar una operación de configuración.
- El encabezado define únicamente la interfaz; la implementación se encuentra en task_uart_interface.c, donde actualmente todas las funciones están vacías.

En consecuencia, el archivo organiza correctamente las declaraciones públicas de la capa UART, pero la interfaz todavía debe ampliarse para soportar operaciones reales de apertura, lectura, escritura, configuración y reporte de errores.

# Trabajo práctico: Device Driver UART con FreeRTOS

## 1. Objetivo

Se diseñó e implementó un Device Driver UART asíncrono para STM32F446RE,
utilizando FreeRTOS, interrupciones y la API STM32F4 HAL.

## 2. Arquitectura

El driver está representado por `uart_device_t`, que contiene el identificador
del dispositivo, referencia al canal UART, colas de entrada y salida, semáforo
de finalización TX, tareas gatekeeper, buffers y estadísticas.

La interfaz pública está compuesta por:

- `open_uart()`
- `release_uart()`
- `write_uart()`
- `read_uart()`
- `ioctl_uart()`

## 3. Funcionamiento asíncrono

`write_uart()` copia los datos a un spool asignado dinámicamente y coloca su
puntero en la cola de salida. El gatekeeper TX consume el spool e inicia la
transmisión mediante `HAL_UART_Transmit_IT()`.

La recepción utiliza `HAL_UART_Receive_IT()`. El gatekeeper RX forma una trama
y crea dinámicamente un spool, que se deposita en la cola de entrada para ser
consumido mediante `read_uart()`.

## 4. Observaciones experimentales

El payload transmitido se observó por USART2 mediante el puerto serie virtual
del ST-LINK a 115200 bit/s, 8 bits, sin paridad y un bit de parada.

El logger del template utiliza semihosting, por lo que sus mensajes se
observaron en STM32CubeIDE durante una sesión Debug.

El terminal `tio` enviaba CR al presionar Enter, mientras que el driver
consideraba LF como terminador. El envío de LF mediante Ctrl+J completó
correctamente las tramas.

La tarea `task_sender()` generó periódicamente un mensaje, que `write_uart()` almacenó en un spool dinámico y colocó en la cola de salida. El gatekeeper TX extrajo el spool e inició la transmisión mediante `HAL_UART_Transmit_IT()`.

En recepción, el gatekeeper RX acumuló los bytes recibidos por interrupción hasta detectar LF (`'\n'`). Luego creó un spool dinámico y lo depositó en la cola de entrada. `task_receiver()` permaneció bloqueada en `read_uart()` hasta recibir una trama completa, que posteriormente retransmitió como eco.

Las pruebas comprobaron el funcionamiento asíncrono del driver, el uso de interrupciones, las colas de entrada y salida, la asignación dinámica de spools y la centralización del acceso a USART2 mediante los gatekeepers.

## 5. Medición temporal

Las funciones se midieron mediante el contador de ciclos DWT del Cortex-M4.
Los valores reportados corresponden al máximo tiempo observado
experimentalmente y no a una cota formal demostrada de WCET.

### Condiciones

- MCU: STM32F446RE
- Frecuencia: 84 MHz
- Compilación: Debug, `-O0`
- UART: 115200 bit/s
- Cantidad de repeticiones: 100

### Resultados

| Función | Escenario considerado | WCET estimado |
|---|---|---:|
| `open_uart()` | Apertura exitosa | 500 µs |
| `release_uart()` | Colas vacías | 250 µs |
| `write_uart()` | Encolado exitoso, timeout 0 | 60 µs |
| `write_uart()` | Cola llena, timeout 0 | 70 µs |
| `read_uart()` | Trama disponible de hasta 64 bytes | 40 µs |
| `read_uart()` | Cola vacía, timeout 0 | 10 µs |
| `ioctl_uart()` | Obtener estadísticas | 10 µs |
| `ioctl_uart()` | Vaciar cola RX con 8 spools | 150 µs |

## 6. Conclusiones

La implementación cumple con el patrón asíncrono solicitado, utiliza
interrupciones STM32 HAL, dos tareas gatekeeper y colas con spools asignados
dinámicamente. Las pruebas confirmaron la transmisión periódica y la recepción
con eco mediante USART2.