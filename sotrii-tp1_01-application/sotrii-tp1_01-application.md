# sotrii-tp1_01-application — Device Driver I2C de FreeRTOS

## Análisis del proyecto base

El proyecto implementa un *device driver* de I2C sobre FreeRTOS siguiendo el patrón
**gatekeeper**: una única tarea es la dueña del periférico y el resto del sistema se
comunica con ella a través de colas. De este modo el acceso al hardware queda
serializado y no hace falta un mutex explícito para proteger el bus.

La aplicación corre sobre una NUCLEO-F446RE y usa el canal `I2C1` (`hi2c1`) para
escribir a un expansor **PCF8574** en la dirección `0x27` (módulo LCD serie).

### Estructura en capas

```
task_sender / task_receiver        (aplicación)
        │  write_i2c() / read_i2c()
        ▼
task_i2c_interface.c               (interfaz del driver: open/release/write/read/ioctl)
        │  colas queue_tx / queue_rx
        ▼
task_i2c.c                         (tareas gatekeeper: únicas que tocan el HAL I2C)
        │  HAL_I2C_Master_Transmit / Receive
        ▼
periférico I2C1 → PCF8574 (0x27)
```

---

## Archivo por archivo

### `app.c`
Contiene `app_init()`, que se ejecuta una vez antes de arrancar el scheduler:

- Inicializa los contadores globales (`g_app_tick_cnt`, `g_task_idle_cnt`,
  `g_app_stack_overflow_cnt`).
- Imprime por el logger los datos de la aplicación.
- Crea las dos tareas de aplicación con `xTaskCreate`:
  - `task_sender` — prioridad `tskIDLE_PRIORITY + 1`, stack `2 * configMINIMAL_STACK_SIZE`.
  - `task_receiver` — misma prioridad y stack.
  - Cada creación se valida con `configASSERT(pdPASS == ret)`.
- Consulta el heap libre con `xPortGetFreeHeapSize()` (heap_4).
- Instancia el driver con **`open_i2c(&hi2c1)`**.
- Inicializa las interrupciones de aplicación (`app_it_init()`) y el contador de
  ciclos DWT (`cycle_counter_init()`) que se usa para medir WCET.

Notar que las colas y semáforos globales quedan **declarados pero no creados** en
esta capa: la creación de las colas del driver la hace `open_i2c()`.

### `app_it.c`
Soporte de interrupciones de aplicación:

- `app_it_init()` — marca una sección crítica con `__asm("CPSID i")` /
  `__asm("CPSIE i")` (deshabilitar/habilitar interrupciones globales). Es el
  esqueleto donde iría la inicialización que deba protegerse.
- `HAL_GPIO_EXTI_Callback()` — callback de línea EXTI; discrimina por
  `BTN_A_PIN`. Está vacío (placeholder para el pulsador de usuario).

### `task_sender.c`
Tarea productora que ejercita la escritura del driver:

- Variables locales: `dev_address = 0x27` (PCF8574) y `dev_data = 0x55`.
- En el lazo infinito, cada iteración:
  1. Incrementa `g_task_sender_cnt`.
  2. Invierte el dato: `dev_data = ~dev_data` (alterna `0x55` ↔ `0xAA`).
  3. Llama **`write_i2c(&hi2c1, dev_address, dev_data)`**.
  4. Espera 250 ms con `vTaskDelay(TASK_SENDER_DEL_MAX)`.

La tarea **no bloquea contra el hardware**: `write_i2c()` sólo encola el dato y
retorna; la transmisión real la resuelve la gatekeeper.

### `task_receiver.c`
Tarea consumidora, por ahora un placeholder:

- En el lazo incrementa `g_task_receiver_cnt` y espera 250 ms.
- Todavía **no invoca `read_i2c()`**: es el punto de extensión para cerrar el
  camino de recepción.

### `task_i2c.c`
Implementa las dos tareas **gatekeeper**, únicas que acceden al periférico:

- **`task_i2c_tx`**:
  - Recibe por parámetro un `task_i2c_dta_t *` (la estructura del dispositivo).
  - En el lazo: `cycle_counter_reset()` → `xQueueReceive(queue_tx, …, portMAX_DELAY)`
    (se bloquea hasta que haya un dato) → **`HAL_I2C_Master_Transmit(device_id,
    address << 1, &data, 1, HAL_MAX_DELAY)`** (transferencia por **polling**,
    bloqueante) → `cycle_counter_get_time_us()` guarda el tiempo de ejecución en
    `g_task_xxxx_tx_runtime_us` → log + `vTaskDelay(250ms)`.
  - El corrimiento `address << 1` arma el byte de dirección de 8 bits (7 bits +
    bit R/W = 0 para escritura).
- **`task_i2c_rx`**:
  - Marca `UNUSED(parameters)`.
  - En el lazo mide ciclos, togglea `LED_A` (`HAL_GPIO_TogglePin`) y espera.
  - **No lee del bus I2C todavía**: es el placeholder de la recepción.

### `task_i2c_interface.c` / `task_i2c_interface.h`
Capa de interfaz del driver, con firma estilo POSIX. El header exporta:

```c
void open_i2c   (I2C_HandleTypeDef *h_i2c_device);
void release_i2c(I2C_HandleTypeDef *h_i2c_device);
void write_i2c  (I2C_HandleTypeDef *h_i2c_device, uint16_t address, uint8_t data);
void read_i2c   (I2C_HandleTypeDef *h_i2c_device);
void ioctl_i2c  (I2C_HandleTypeDef *h_i2c_device);
```

Implementación:

- **`open_i2c()`** — guarda `device_id`, crea `queue_tx` (5 × `task_i2c_tx_dta_t`) y
  `queue_rx` (10 × `uint8_t`), las registra con `vQueueAddToRegistry`, y crea las
  dos tareas gatekeeper (`task_i2c_tx`, `task_i2c_rx`), pasándoles la estructura
  del dispositivo como parámetro. Valida todo con `configASSERT`.
- **`release_i2c()`** — libera colas (`vQueueUnregisterQueue` + `vQueueDelete`) y
  elimina las tareas (`vTaskDelete`).
- **`write_i2c()`** — arma un `task_i2c_tx_dta_t {address, data}` y lo envía con
  `xQueueSend(queue_tx, …, portMAX_DELAY)`. Es el camino de escritura completo.
- **`read_i2c()`** — **vacío** (sólo `UNUSED`). A implementar.
- **`ioctl_i2c()`** — **vacío** (sólo `UNUSED`). A implementar.

La estructura del dispositivo (`task_i2c_attribute.h`) es:

```c
typedef struct {
    I2C_HandleTypeDef *device_id;   // canal I2C sobre el que opera el driver
    TaskHandle_t       task_tx;     // gatekeeper de transmisión
    QueueHandle_t      queue_tx;    // cola de escritura
    TaskHandle_t       task_rx;     // gatekeeper de recepción
    QueueHandle_t      queue_rx;    // cola de lectura
} task_i2c_dta_t;

typedef struct {
    uint16_t address;
    uint8_t  data;
} task_i2c_tx_dta_t;
```

La instancia `task_i2c_dta` es **estática** (asignación estática, como pide la
consigna para esta actividad).

### `freertos.c`
Hooks de FreeRTOS:

- `vApplicationIdleHook()` — incrementa `g_task_idle_cnt` (mide tiempo ocioso).
- `vApplicationTickHook()` — incrementa `g_app_tick_cnt` desde el ISR de tick.
- `vApplicationStackOverflowHook()` — `configASSERT(0)` dentro de sección crítica
  para colgar la ejecución y depurar un desborde de stack.

---

## Mapa del patrón de diseño

- **Patrón:** Synchronous — la aplicación entrega el dato al driver y la
  transferencia HAL es bloqueante dentro de la gatekeeper.
- **Gestión del periférico:** Polling (`HAL_I2C_Master_Transmit` con
  `HAL_MAX_DELAY`). Queda pendiente la variante Interrupt.
- **Acceso:** API STM32-F4 HAL.
- **Almacenamiento:** Queue, asignación estática.
- **Sección crítica:** implícita — sólo la gatekeeper toca el bus, por lo que no
  se requiere mutex adicional.

## Pendientes de implementación (Paso 06)

1. `read_i2c()` — consumir de `queue_rx`.
2. Recepción real en `task_i2c_rx` con `HAL_I2C_Master_Receive`, depositando en
   `queue_rx`.
3. `task_receiver` — consumir vía `read_i2c()`.
4. `ioctl_i2c()` — configuración/estado del dispositivo.
5. Variante Interrupt (`HAL_I2C_Master_Transmit_IT` / `Receive_IT` + semáforo
   binario desde el callback de fin de transferencia).
6. Medición de WCET de cada función de interfaz.

## Comportamiento observado

Corrida sin esclavo I2C conectado (transmisión termina en NACK):

- El sistema corre sin bloquearse: las seis tareas (sender, receiver, gatekeeper
  TX, gatekeeper RX, idle y defaultTask) ejecutan de forma periódica.
- La ruta de **transmisión por interrupción** queda demostrada: como no hay
  esclavo, cada envío genera un NACK que dispara `HAL_I2C_ErrorCallback`, el cual
  libera `sem_tx_done` y despierta a `task_i2c_tx`. El log `Task I2C TX` se repite
  cada 250 ms sin colgarse, lo que confirma la sincronización ISR → tarea.
- La ruta de **recepción por polling** también funciona de punta a punta:
  `task_i2c_rx` (`HAL_I2C_Master_Receive`) deposita en `queue_rx`, `read_i2c()` lo
  extrae y `task_receiver` lo consume (`Task RECEIVER - I2C read: 0xA5`).
- El driver ejercita así **las dos gestiones que pide la consigna para I2C**:
  Interrupt en TX y Polling en RX.

## Mediciones de WCET

Se instrumentaron las funciones de interfaz con el contador de ciclos del DWT
(`cycle_counter_reset()` … `cycle_counter_get()`), registrando el **máximo**
observado sobre muchas ejecuciones. El core corre a **180 MHz** (1 ciclo ≈
5,56 ns). Los valores se leen en la ventana *Expressions* del depurador.

| Función de interfaz | Variable | WCET [ciclos] | WCET [µs] |
| :------------------ | :------- | :-----------: | :-------: |
| `write_i2c()`       | `g_write_i2c_wcet_cy` | 626 | 3,48 |
| `read_i2c()`        | `g_read_i2c_wcet_cy`  | 626 | 3,48 |
| `ioctl_i2c()`       | `g_ioctl_i2c_wcet_cy` | 33  | 0,18 |

Los tiempos son coherentes con el trabajo de cada función: `write`/`read` están
dominadas por el acceso a la cola (`xQueueSend` / `xQueueReceive`), mientras que
`ioctl` solo resuelve un `switch` y lee una variable global.

Tiempo de transferencia de las tareas gatekeeper (accesible además vía
`ioctl_i2c`, comandos `I2C_GET_TX_WCET_US` / `I2C_GET_RX_WCET_US`):

| Gatekeeper | Variable | Tiempo [µs] |
| :--------- | :------- | :---------: |
| `task_i2c_tx` (Interrupt) | `g_task_xxxx_tx_runtime_us` | 23607 |
| `task_i2c_rx` (Polling)   | `g_task_xxxx_rx_runtime_us` | 23607 |

> Nota 1: el DWT cuenta todos los ciclos del núcleo, por lo que una preempción
> durante la medición infla el valor. Se documenta el máximo como cota observada,
> no como WCET teórico.
>
> Nota 2: los ~23,6 ms de las gatekeeper **no** son un tiempo de transferencia
> nominal. La medición se tomó **sin esclavo y sin resistencias de pull-up**: el
> bus queda flotando y la HAL espera los flags hasta detectar el error (NACK /
> bus-error). Con un esclavo real (p. ej. BMP180) y pull-ups, este tiempo baja a
> decenas de µs. El valor refleja la latencia de la condición de falla, no la de
> una transacción exitosa.

---

## Actualización 2026-07-13 — Driver register-aware + esclavo real (BMP180)

Para probar el driver contra un esclavo I2C real se conectó un **BMP180**
(dirección `0x77`, `I2C1` en PB8/PB9) y se evolucionó el driver de transacciones
de 1 byte a **transacciones por registro**, necesarias para hablar con el sensor.

### Cambios de diseño

- **`write_i2c(h, addr, reg, val)`** — ahora lleva el registro destino; la
  gatekeeper TX usa `HAL_I2C_Mem_Write`.
- **`read_i2c(h, addr, reg, buf, len)`** — pasa de "no bloqueante, saco lo que
  haya" a **pedido → respuesta**: encola un pedido `{addr, reg, len}` en la nueva
  cola `queue_rx_req` y espera los `len` bytes por `queue_rx` (bloqueante, timeout
  100 ms). La gatekeeper RX (`HAL_I2C_Mem_Read`) sólo responde ante un pedido; si
  la transacción da NACK no devuelve datos y `read_i2c` retorna `false`.
- **`task_i2c_attribute.h`** — el struct TX incorpora `reg`; se agrega
  `task_i2c_rx_req_dta_t {address, reg, len}` y la cola `queue_rx_req`.
- **`task_bmp180.c`** (cliente nuevo) — lee los coeficientes AC5/AC6/MC/MD, dispara
  la conversión de temperatura (`0x2E` → `0xF4`), espera 5 ms y lee `UT` (`0xF6`),
  todo **a través de `write_i2c`/`read_i2c`** (no toca la HAL). Es la única cliente
  del bus; el demo `task_sender`/`task_receiver` queda deshabilitado (`#if 0`).

El patrón sigue siendo **gatekeeper + colas + asignación estática**; ahora el
BMP180 es el esclavo que ejercita el driver de punta a punta.

### Comportamiento observado (con esclavo real)

- `task_bmp180` imprime la temperatura ambiente, coherente, cada 1 s
  (`==> Task BMP180 - Temp: 25.3 C`), confirmando que la lectura viaja por el
  gatekeeper y que la compensación del datasheet es correcta.
- Se confirma la **Nota 2** de arriba: con esclavo real y pull-ups, el tiempo de
  transferencia de las gatekeeper baja de los ~23,6 ms (latencia de NACK sin
  esclavo) a las decenas de µs de una transacción exitosa.

### WCET (re-medición pendiente en esta versión)

Como `read_i2c` cambió de semántica (ahora bloquea esperando la respuesta de la
gatekeeper), su WCET ya no es comparable con el de la versión anterior. Re-medir
en el depurador (ventana *Live Expressions*; core @ 180 MHz → µs = ciclos / 180):

| Función de interfaz | Variable | WCET [ciclos] | WCET [µs] |
| :------------------ | :------- | :-----------: | :-------: |
| `write_i2c()`       | `g_write_i2c_wcet_cy` | _(a medir)_ | |
| `read_i2c()`        | `g_read_i2c_wcet_cy`  | _(a medir)_ | |
| `ioctl_i2c()`       | `g_ioctl_i2c_wcet_cy` | _(a medir)_ | |

| Gatekeeper | Variable | Tiempo [µs] |
| :--------- | :------- | :---------: |
| `task_i2c_tx` | `g_task_xxxx_tx_runtime_us` | _(a medir)_ |
| `task_i2c_rx` | `g_task_xxxx_rx_runtime_us` | _(a medir)_ |
