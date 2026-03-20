#pragma once

/*
 * slcan_bridge_project_configuration.h
 *
 * Единый файл пользовательских настроек проекта ESP32_S3_SLCAN_BRIDGE.
 *
 * Всё, что может понадобиться изменить под конкретное железо и сценарий
 * использования, находится здесь. Menuconfig для этих параметров не нужен.
 *
 * НЕ ОТНОСИТСЯ к этому файлу:
 *   - Настройки ESP-IDF / SoC / toolchain (остаются в sdkconfig.defaults)
 *   - Errata-фиксы (CONFIG_TWAI_ERRATA_FIX_LISTEN_ONLY_DOM в sdkconfig.defaults)
 *   - Аппаратная настройка режима трансивера (STB / S / RS / EN) —
 *     это hardware wiring, см. README.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ════════════════════════════════════════════════════════════════════════════
   РАЗДЕЛ 1: GPIO — физическое подключение к TWAI-контроллеру ESP32-S3

   Прошивка использует только эти два сигнала. Управление режимом трансивера
   (STB / S / RS) — задача аппаратной схемы, не прошивки. См. README.
   ════════════════════════════════════════════════════════════════════════════ */

/* GPIO ESP32-S3, подключённый к выводу RXD трансивера.
   Сигнал входит в ESP32-S3 от трансивера. */
#define SLCAN_BRIDGE_CAN_RECEIVE_GPIO   6

/* GPIO ESP32-S3, подключённый к выводу TXD трансивера.
   Сигнал выходит из ESP32-S3 к трансиверу. */
#define SLCAN_BRIDGE_CAN_TRANSMIT_GPIO  5

/* ════════════════════════════════════════════════════════════════════════════
   РАЗДЕЛ 2: CAN — скорость шины по умолчанию
   ════════════════════════════════════════════════════════════════════════════ */

/* SLCAN-код скорости CAN, используемый при старте до получения команды Sx.
   '2' = 50 kbit/s,  '3' = 100 kbit/s, '4' = 125 kbit/s,
   '5' = 250 kbit/s, '6' = 500 kbit/s, '7' = 800 kbit/s, '8' = 1 Mbit/s. */
#define SLCAN_BRIDGE_DEFAULT_SPEED_CODE  '6'

/* ════════════════════════════════════════════════════════════════════════════
   РАЗДЕЛ 3: USB Serial/JTAG — буферы
   ════════════════════════════════════════════════════════════════════════════ */

/* Размер TX-буфера USB Serial/JTAG в байтах. */
#define SLCAN_BRIDGE_USB_TRANSMIT_BUFFER_SIZE_BYTES  1024U

/* Размер RX-буфера USB Serial/JTAG в байтах. */
#define SLCAN_BRIDGE_USB_RECEIVE_BUFFER_SIZE_BYTES   512U

/* ════════════════════════════════════════════════════════════════════════════
   РАЗДЕЛ 4: TWAI — очереди драйвера
   ════════════════════════════════════════════════════════════════════════════ */

/* Глубина RX-очереди TWAI (количество кадров). */
#define SLCAN_BRIDGE_TWAI_RECEIVE_QUEUE_DEPTH    128U

/* Глубина TX-очереди TWAI (количество кадров). 0 отключает TX-очередь. */
#define SLCAN_BRIDGE_TWAI_TRANSMIT_QUEUE_DEPTH   32U

/* ════════════════════════════════════════════════════════════════════════════
   РАЗДЕЛ 5: Тайм-ауты операций TWAI и drain wait перед uninstall

   Официальная документация ESP-IDF (driver/twai.h):
   "The application must ensure that no tasks are blocked on TX/RX queues
   or alerts when twai_driver_uninstall() is called."

   SLCAN_BRIDGE_TWAI_UNINSTALL_DRAIN_WAIT_MILLISECONDS вычисляется
   автоматически как max(RX_timeout, TX_timeout) + safety_margin.
   При изменении любого timeout'а — drain wait пересчитывается сам.
   ════════════════════════════════════════════════════════════════════════════ */

/* Максимальное время ожидания одного вызова twai_receive() (мс). */
#define SLCAN_BRIDGE_CAN_RECEIVE_TIMEOUT_MILLISECONDS    20U

/* Максимальное время ожидания одного вызова twai_transmit() (мс). */
#define SLCAN_BRIDGE_CAN_TRANSMIT_TIMEOUT_MILLISECONDS   100U

/* Дополнительный запас к drain wait (мс). */
#define SLCAN_BRIDGE_TWAI_UNINSTALL_DRAIN_SAFETY_MARGIN_MILLISECONDS  10U

/* Helper: максимум двух compile-time значений. */
#define SLCAN_BRIDGE_UNSIGNED_MAX(a, b)  ((a) > (b) ? (a) : (b))

/* Drain wait перед twai_driver_uninstall() = max(RX, TX) + margin. */
#define SLCAN_BRIDGE_TWAI_UNINSTALL_DRAIN_WAIT_MILLISECONDS        \
    (SLCAN_BRIDGE_UNSIGNED_MAX(                                     \
         SLCAN_BRIDGE_CAN_RECEIVE_TIMEOUT_MILLISECONDS,             \
         SLCAN_BRIDGE_CAN_TRANSMIT_TIMEOUT_MILLISECONDS)            \
     + SLCAN_BRIDGE_TWAI_UNINSTALL_DRAIN_SAFETY_MARGIN_MILLISECONDS)

/* ════════════════════════════════════════════════════════════════════════════
   РАЗДЕЛ 6: FreeRTOS задачи
   ════════════════════════════════════════════════════════════════════════════ */

/* Размер стека задачи обработки SLCAN-команд (байты). */
#define SLCAN_BRIDGE_COMMAND_TASK_STACK_SIZE_BYTES      4096U

/* Размер стека задачи приёма CAN-кадров (байты). */
#define SLCAN_BRIDGE_CAN_RECEIVE_TASK_STACK_SIZE_BYTES  4096U

/* Приоритет обеих задач моста (1–24, выше число — выше приоритет). */
#define SLCAN_BRIDGE_TASKS_PRIORITY  10U

/* ════════════════════════════════════════════════════════════════════════════
   РАЗДЕЛ 7: SLCAN протокол
   ════════════════════════════════════════════════════════════════════════════ */

/* Максимальная длина SLCAN-команды в байтах (включая CR, без нулевого байта).
   Самая длинная команда — расширенный кадр с 8 байтами данных: T+8+1+16=26.
   64 байта — достаточный запас. */
#define SLCAN_BRIDGE_SLCAN_COMMAND_BUFFER_MAX_LENGTH  64U

/* Строка версии прошивки, возвращаемая по команде V (формат LAWICEL: 4 символа). */
#define SLCAN_BRIDGE_LAWICEL_FIRMWARE_VERSION  "0100"

/* Серийный номер устройства, возвращаемый по команде N (формат LAWICEL: 4 символа). */
#define SLCAN_BRIDGE_LAWICEL_SERIAL_NUMBER     "0001"

#ifdef __cplusplus
}
#endif
