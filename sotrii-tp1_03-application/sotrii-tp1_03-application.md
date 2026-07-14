# Análisis del código fuente – TP1 Actividad 03 – Device Driver ADC

## app.c

Este archivo realiza la inicialización general de la aplicación.

Su función principal es inicializar los distintos módulos del proyecto antes de que el scheduler de FreeRTOS comience a ejecutar las tareas. Entre estas inicializaciones se encuentran la placa, la aplicación y los módulos auxiliares.

No contiene lógica específica del ADC, sino que prepara el sistema para su funcionamiento.

---

## app_it.c

Este archivo contiene las rutinas relacionadas con las interrupciones de la aplicación.

Se definen variables globales para registrar:

- cantidad de interrupciones recibidas,
- tiempo de ejecución de la ISR,
- banderas de sincronización.

La función `app_it_init()` inicializa dichas variables protegiendo el acceso mediante la deshabilitación temporal de interrupciones.

También implementa dos callbacks de la HAL:

### HAL_GPIO_EXTI_Callback()

Corresponde a la interrupción externa del pulsador. Actualmente solamente verifica qué GPIO produjo la interrupción y deja un espacio para que el usuario agregue el procesamiento correspondiente.

### HAL_ADC_ConvCpltCallback()

Se ejecuta cuando finaliza una conversión del ADC en modo no bloqueante.

Dentro de esta función:

- verifica que el ADC corresponda al ADC1,
- activa una bandera indicando que existe un dato disponible,
- incrementa un contador de conversiones realizadas,
- almacena el tiempo de ejecución utilizando el contador de ciclos (DWT).

---

## task_receiver.c

Implementa una tarea denominada `task_receiver`.

Actualmente su funcionamiento es únicamente demostrativo.

Al iniciarse:

- inicializa un contador,
- informa mediante el logger que la tarea comenzó a ejecutarse.

Posteriormente ejecuta un ciclo infinito donde:

- incrementa un contador de ejecuciones,
- imprime un mensaje por consola,
- permanece bloqueada durante 250 ms utilizando `vTaskDelay()`.

En esta plantilla todavía no recibe datos provenientes del driver ADC.

---

## task_adc.c

Implementa la tarea asociada al Device Driver ADC.

Actualmente solamente existe la tarea de recepción (`task_adc_rx`).

Durante su ejecución:

- inicializa contadores y variables de tiempo,
- informa mediante el logger el inicio de la tarea,
- entra en un ciclo infinito.

En cada iteración:

- incrementa un contador de ejecuciones,
- reinicia el contador de ciclos,
- conmuta el LED de usuario,
- mide el tiempo de ejecución de la tarea,
- espera 250 ms mediante `vTaskDelay()`.

Actualmente la tarea no realiza conversiones ADC ni procesa datos, funcionando únicamente como una base para implementar el driver.

---

## task_adc_interface.c

Implementa las funciones de interfaz del Device Driver ADC.

Se encuentran definidas las operaciones típicas de un driver:

- `open_adc()`
- `release_adc()`
- `write_adc()`
- `read_adc()`
- `ioctl_adc()`

En la plantilla todas estas funciones únicamente reciben el manejador del ADC y utilizan la macro `UNUSED()` para evitar advertencias del compilador.

Estas funciones deberán implementarse durante el desarrollo del trabajo práctico para controlar el periférico ADC.

---

## task_adc_interface.h

Este archivo contiene las declaraciones públicas de la interfaz del driver ADC.

Expone las funciones que podrán ser utilizadas por otras tareas del sistema:

- apertura del dispositivo,
- liberación del dispositivo,
- lectura,
- escritura,
- configuración mediante `ioctl`.

Actúa como la interfaz pública del Device Driver.

---

## freertos.c

Contiene las funciones Hook de FreeRTOS.

### vApplicationIdleHook()

Se ejecuta cuando ninguna tarea lista puede utilizar el procesador.

En esta función solamente se incrementa un contador para medir cuánto tiempo permanece el sistema en la tarea IDLE.

### vApplicationTickHook()

Se ejecuta en cada interrupción del Tick de FreeRTOS.

Incrementa un contador que permite conocer la cantidad de ticks transcurridos desde el inicio del sistema.

### vApplicationStackOverflowHook()

Es llamada automáticamente por FreeRTOS cuando detecta un desbordamiento de pila en alguna tarea.

Su objetivo es facilitar la detección de errores durante el desarrollo.

---

# Funcionamiento general

El proyecto corresponde a una plantilla para implementar un Device Driver ADC utilizando FreeRTOS.

La arquitectura separa claramente:

- la aplicación,
- las rutinas de interrupción,
- las tareas del sistema,
- la interfaz pública del driver.

Actualmente las tareas únicamente realizan funciones de demostración (contador, temporización y cambio de estado del LED), mientras que la interfaz del ADC se encuentra preparada para que posteriormente se implementen las operaciones propias del Device Driver, como la apertura del dispositivo, lectura de conversiones, configuración y liberación del periférico.

# Paso 06 - Diseño, implementación y prueba del Device Driver ADC

### Implementación

Se implementó un Device Driver para el periférico ADC utilizando la API HAL de STM32F4 y el patrón de diseño **Latest Input Only**.

El driver está compuesto por:

- Una estructura `adc_device` que representa el dispositivo e incluye:
  - Identificador del dispositivo.
  - Referencia al periférico ADC (`ADC_HandleTypeDef`).
  - Cola de entrada.
  - Cola de salida.
  - Último valor convertido.
  - Handle de la tarea Gatekeeper.

- Funciones de interfaz:
  - `open_adc()`
  - `release_adc()`
  - `read_adc()`
  - `write_adc()`
  - `ioctl_adc()`
  - `init_DMA_ADC()`

El periférico ADC utiliza el controlador DMA para transferir automáticamente las conversiones al buffer de memoria, reduciendo la intervención de la CPU.

La interrupción `HAL_ADC_ConvCpltCallback()` obtiene la última conversión realizada por el DMA y la envía mediante `xQueueOverwriteFromISR()` a una cola de longitud uno, implementando el patrón **Latest Input Only**.

Una tarea tipo **Gatekeeper** (`task_adc_rx`) recibe las muestras provenientes de la ISR, actualiza el último valor disponible del dispositivo y lo publica en la cola de salida mediante `xQueueOverwrite()`.

La tarea `task_receiver` utiliza la función `read_adc()` para obtener el último valor convertido y mostrarlo por consola.

---

### Pruebas realizadas

Para validar el funcionamiento del driver se conectó un potenciómetro al canal ADC configurado.

Durante la ejecución se observó que:

- La tarea Gatekeeper recibió correctamente las muestras provenientes del DMA.
- La tarea Receiver leyó correctamente el último valor disponible utilizando la función `read_adc()`.
- El valor mostrado por consola variaba de forma continua al modificar la posición del potenciómetro, verificando el correcto funcionamiento del ADC, DMA, interrupciones y mecanismo de comunicación mediante colas.

---

### Problema encontrado

Inicialmente el ADC fue configurado con:

- Conversión continua (`Continuous Conversion Mode`).
- Solicitudes DMA continuas (`DMA Continuous Requests`).
- Tiempo de muestreo de **3 ciclos**.

Esta configuración generaba una frecuencia de conversión muy elevada, provocando una gran cantidad de interrupciones que impedían la ejecución normal de las tareas de FreeRTOS, observándose que la tarea Gatekeeper consumía prácticamente todo el tiempo de CPU.

Como solución se incrementó el tiempo de muestreo del ADC y posteriormente se modificó la arquitectura para utilizar un temporizador **TIM2** como disparador externo del ADC a una frecuencia aproximada de **100 Hz**.

Con esta configuración:

- El ADC únicamente realiza conversiones cuando el temporizador genera un evento TRGO.
- Se reduce significativamente la cantidad de interrupciones.
- La utilización del procesador disminuye considerablemente.
- La tarea IDLE vuelve a ser la de mayor porcentaje de ejecución, obteniendo un comportamiento mucho más eficiente y adecuado para un sistema de tiempo real.

---

### Resultado

El Device Driver ADC cumple con los requisitos propuestos:

- ✔ API de interfaz del dispositivo.
- ✔ Uso de STM32 HAL.
- ✔ Gestión del ADC mediante DMA.
- ✔ Patrón Latest Input Only.
- ✔ Tarea Gatekeeper.
- ✔ Comunicación mediante colas dinámicas.
- ✔ Lectura correcta del último valor convertido utilizando un potenciómetro como entrada analógica.