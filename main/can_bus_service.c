#include "can_bus_service.h"
#include "slcan_bridge_project_configuration.h"

#include <string.h>

#include "driver/twai.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* ────────────────────────────────────────────────────────────────────────────
   SLCAN / LAWICEL статус-флаги (битовая маска ответа команды F).
   Биты взяты из официального LAWICEL CAN232 manual.
   ──────────────────────────────────────────────────────────────────────────── */
#define SLCAN_STATUS_FLAG_RX_QUEUE_FULL    (1U << 0)
#define SLCAN_STATUS_FLAG_TX_QUEUE_FULL    (1U << 1)
#define SLCAN_STATUS_FLAG_ERROR_WARNING    (1U << 2)
#define SLCAN_STATUS_FLAG_DATA_OVERRUN     (1U << 3)
#define SLCAN_STATUS_FLAG_ERROR_PASSIVE    (1U << 5)
#define SLCAN_STATUS_FLAG_ARBITRATION_LOST (1U << 6)
#define SLCAN_STATUS_FLAG_BUS_ERROR        (1U << 7)

/* Максимальный DLC для classical CAN (ISO 11898-1). */
#define CLASSICAL_CAN_MAX_DLC 8U

static const char *TAG = "can_bus_service";

/* ────────────────────────────────────────────────────────────────────────────
   Внутреннее состояние сервиса
   ──────────────────────────────────────────────────────────────────────────── */
typedef struct {
    bool                     driver_installed;
    bool                     bus_running;
    can_bus_operating_mode_t operating_mode;
    char                     selected_speed_code;
    SemaphoreHandle_t        mutex;

    /* Baselines для read-and-clear семантики команды F.
       Счётчики TWAI кумулятивны. Delta (current - baseline) вычисляется
       при каждом чтении F, после чего baseline обновляется.
       Сбрасываются после twai_start(). */
    uint32_t status_flags_baseline_rx_missed_count;
    uint32_t status_flags_baseline_rx_overrun_count;
    uint32_t status_flags_baseline_arb_lost_count;
    uint32_t status_flags_baseline_bus_error_count;
} can_bus_service_state_t;

static can_bus_service_state_t service_state = {
    .driver_installed    = false,
    .bus_running         = false,
    .operating_mode      = CAN_BUS_OPERATING_MODE_NORMAL,
    .selected_speed_code = SLCAN_BRIDGE_DEFAULT_SPEED_CODE,
    .mutex               = NULL,
    .status_flags_baseline_rx_missed_count  = 0U,
    .status_flags_baseline_rx_overrun_count = 0U,
    .status_flags_baseline_arb_lost_count   = 0U,
    .status_flags_baseline_bus_error_count  = 0U,
};

/* ────────────────────────────────────────────────────────────────────────────
   Конфигурация скорости
   ──────────────────────────────────────────────────────────────────────────── */

static esp_err_t map_speed_code_to_timing_configuration(char speed_code,
                                                         twai_timing_config_t *timing_configuration)
{
    if (timing_configuration == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (speed_code) {
#ifdef TWAI_TIMING_CONFIG_20KBITS
        case '1':
            *timing_configuration = (twai_timing_config_t)TWAI_TIMING_CONFIG_20KBITS();
            return ESP_OK;
#endif
        case '2':
            *timing_configuration = (twai_timing_config_t)TWAI_TIMING_CONFIG_50KBITS();
            return ESP_OK;
        case '3':
            *timing_configuration = (twai_timing_config_t)TWAI_TIMING_CONFIG_100KBITS();
            return ESP_OK;
        case '4':
            *timing_configuration = (twai_timing_config_t)TWAI_TIMING_CONFIG_125KBITS();
            return ESP_OK;
        case '5':
            *timing_configuration = (twai_timing_config_t)TWAI_TIMING_CONFIG_250KBITS();
            return ESP_OK;
        case '6':
            *timing_configuration = (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS();
            return ESP_OK;
        case '7':
            *timing_configuration = (twai_timing_config_t)TWAI_TIMING_CONFIG_800KBITS();
            return ESP_OK;
        case '8':
            *timing_configuration = (twai_timing_config_t)TWAI_TIMING_CONFIG_1MBITS();
            return ESP_OK;
        default:
            return ESP_ERR_NOT_SUPPORTED;
    }
}

/* ────────────────────────────────────────────────────────────────────────────
   Статус-флаги — read-and-clear (вызывать только под service mutex)
   ──────────────────────────────────────────────────────────────────────────── */

static void reset_status_flag_baselines_locked(void)
{
    twai_status_info_t info;
    memset(&info, 0, sizeof(info));

    if (twai_get_status_info(&info) == ESP_OK) {
        service_state.status_flags_baseline_rx_missed_count  = info.rx_missed_count;
        service_state.status_flags_baseline_rx_overrun_count = info.rx_overrun_count;
        service_state.status_flags_baseline_arb_lost_count   = info.arb_lost_count;
        service_state.status_flags_baseline_bus_error_count  = info.bus_error_count;
    } else {
        service_state.status_flags_baseline_rx_missed_count  = 0U;
        service_state.status_flags_baseline_rx_overrun_count = 0U;
        service_state.status_flags_baseline_arb_lost_count   = 0U;
        service_state.status_flags_baseline_bus_error_count  = 0U;
    }
}

static uint8_t build_slcan_status_flags_and_update_baselines_locked(void)
{
    if (!service_state.driver_installed) {
        return 0U;
    }

    twai_status_info_t info;
    memset(&info, 0, sizeof(info));
    if (twai_get_status_info(&info) != ESP_OK) {
        return 0U;
    }

    uint8_t flags = 0U;

    /* Мгновенные флаги — текущее состояние, без накопления. */
    if (info.msgs_to_tx >= (uint32_t)SLCAN_BRIDGE_TWAI_TRANSMIT_QUEUE_DEPTH &&
        SLCAN_BRIDGE_TWAI_TRANSMIT_QUEUE_DEPTH > 0U) {
        flags |= SLCAN_STATUS_FLAG_TX_QUEUE_FULL;
    }
    if (info.tx_error_counter >= 96U || info.rx_error_counter >= 96U) {
        flags |= SLCAN_STATUS_FLAG_ERROR_WARNING;
    }
    if (info.tx_error_counter >= 128U || info.rx_error_counter >= 128U) {
        flags |= SLCAN_STATUS_FLAG_ERROR_PASSIVE;
    }

    /* Событийные флаги — delta от baseline (read-and-clear). */
    if (info.rx_missed_count > service_state.status_flags_baseline_rx_missed_count) {
        flags |= SLCAN_STATUS_FLAG_RX_QUEUE_FULL;
    }
    if (info.rx_overrun_count > service_state.status_flags_baseline_rx_overrun_count) {
        flags |= SLCAN_STATUS_FLAG_DATA_OVERRUN;
    }
    if (info.arb_lost_count > service_state.status_flags_baseline_arb_lost_count) {
        flags |= SLCAN_STATUS_FLAG_ARBITRATION_LOST;
    }
    if (info.bus_error_count > service_state.status_flags_baseline_bus_error_count ||
        info.state == TWAI_STATE_BUS_OFF) {
        flags |= SLCAN_STATUS_FLAG_BUS_ERROR;
    }

    /* Clear: обновить baselines. */
    service_state.status_flags_baseline_rx_missed_count  = info.rx_missed_count;
    service_state.status_flags_baseline_rx_overrun_count = info.rx_overrun_count;
    service_state.status_flags_baseline_arb_lost_count   = info.arb_lost_count;
    service_state.status_flags_baseline_bus_error_count  = info.bus_error_count;

    return flags;
}

/* ────────────────────────────────────────────────────────────────────────────
   Driver lifecycle helpers (вызывать только под service mutex)
   ──────────────────────────────────────────────────────────────────────────── */

static esp_err_t install_driver_locked(can_bus_operating_mode_t operating_mode)
{
    twai_timing_config_t timing_configuration;
    const esp_err_t map_result = map_speed_code_to_timing_configuration(
        service_state.selected_speed_code, &timing_configuration);
    if (map_result != ESP_OK) {
        ESP_LOGE(TAG, "install_driver_locked: unsupported speed code '%c'",
                 service_state.selected_speed_code);
        return map_result;
    }

    twai_general_config_t general_configuration = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)SLCAN_BRIDGE_CAN_TRANSMIT_GPIO,
        (gpio_num_t)SLCAN_BRIDGE_CAN_RECEIVE_GPIO,
        operating_mode == CAN_BUS_OPERATING_MODE_LISTEN_ONLY
            ? TWAI_MODE_LISTEN_ONLY
            : TWAI_MODE_NORMAL);

    general_configuration.tx_queue_len   = SLCAN_BRIDGE_TWAI_TRANSMIT_QUEUE_DEPTH;
    general_configuration.rx_queue_len   = SLCAN_BRIDGE_TWAI_RECEIVE_QUEUE_DEPTH;
    general_configuration.alerts_enabled = TWAI_ALERT_NONE;

    twai_filter_config_t filter_configuration = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    const esp_err_t install_result = twai_driver_install(
        &general_configuration, &timing_configuration, &filter_configuration);
    if (install_result != ESP_OK) {
        ESP_LOGE(TAG, "twai_driver_install() failed: %s", esp_err_to_name(install_result));
        return install_result;
    }

    service_state.driver_installed = true;
    service_state.operating_mode   = operating_mode;
    return ESP_OK;
}

/* Lifecycle uninstall — все состояния TWAI, честный software state.

   BUS_OFF → прямой uninstall (без recovery).
   Официальный driver/twai.h: uninstall принимает STOPPED и BUS_OFF.
   twai_initiate_recovery() перевёл бы в RECOVERING, который uninstall
   не принимает и который может не завершиться, если шина мертва.

   Защита от blocked tasks перед uninstall (официальный driver/twai.h):
   "The application must ensure that no tasks are blocked on TX/RX queues
   or alerts when this function is called."
   Механизм: bus_running=false → drain wait (max TX/RX timeout + margin) → uninstall.
   Drain wait вычисляется из SLCAN_BRIDGE_TWAI_UNINSTALL_DRAIN_WAIT_MILLISECONDS.

   Честный software state:
   driver_installed = false выставляется ТОЛЬКО после успешного uninstall.
   При ошибке uninstall driver_installed остаётся true — hardware неизвестного
   состояния; не фальсифицируем state.
   Трансивер деактивируется независимо от результата uninstall — безопасно.

   Инварианты при успехе:
     driver_installed=false, bus_running=false.
   Инварианты при ошибке uninstall:
     driver_installed=true (честно), bus_running=false. */
static esp_err_t uninstall_driver_locked(void)
{
    if (!service_state.driver_installed) {
        return ESP_OK;
    }

    if (service_state.bus_running) {
        twai_status_info_t info;
        memset(&info, 0, sizeof(info));
        twai_state_t current_twai_state = TWAI_STATE_STOPPED;

        if (twai_get_status_info(&info) == ESP_OK) {
            current_twai_state = info.state;
        }

        if (current_twai_state == TWAI_STATE_RUNNING) {
            const esp_err_t stop_result = twai_stop();
            if (stop_result != ESP_OK) {
                ESP_LOGE(TAG, "twai_stop() failed: %s; proceeding to uninstall",
                         esp_err_to_name(stop_result));
            }
        } else if (current_twai_state == TWAI_STATE_BUS_OFF) {
            /* BUS_OFF: twai_driver_uninstall() принимает это состояние напрямую.
               Не вызываем twai_initiate_recovery() — см. комментарий выше. */
            ESP_LOGW(TAG, "TWAI in BUS_OFF; uninstalling directly");
        }

        /* Выставляем bus_running=false ДО drain wait.
           Новые вызовы send_frame/receive_frame увидят is_running=false и вернут ошибку. */
        service_state.bus_running = false;

        /* Ждём завершения in-flight twai_receive()/twai_transmit() вызовов.
           Drain wait = max(RX_timeout, TX_timeout) + margin. */
        vTaskDelay(pdMS_TO_TICKS(SLCAN_BRIDGE_TWAI_UNINSTALL_DRAIN_WAIT_MILLISECONDS));
    }

    const esp_err_t uninstall_result = twai_driver_uninstall();

    if (uninstall_result != ESP_OK) {
        ESP_LOGE(TAG, "twai_driver_uninstall() failed: %s", esp_err_to_name(uninstall_result));
        /* driver_installed остаётся true: hardware неизвестного состояния.
           Caller видит реальность, а не притворство успеха. */
        return uninstall_result;
    }

    service_state.driver_installed = false;
    return ESP_OK;
}

/* ────────────────────────────────────────────────────────────────────────────
   Публичный API
   ──────────────────────────────────────────────────────────────────────────── */

void can_bus_service_initialize(void)
{
    if (service_state.mutex == NULL) {
        service_state.mutex = xSemaphoreCreateMutex();
        configASSERT(service_state.mutex != NULL);
    }
}

esp_err_t can_bus_service_set_speed_from_slcan_code(char speed_code)
{
    twai_timing_config_t dummy;
    const esp_err_t validate_result =
        map_speed_code_to_timing_configuration(speed_code, &dummy);
    if (validate_result != ESP_OK) {
        return validate_result;
    }

    xSemaphoreTake(service_state.mutex, portMAX_DELAY);

    if (service_state.bus_running) {
        xSemaphoreGive(service_state.mutex);
        return ESP_ERR_INVALID_STATE;
    }

    service_state.selected_speed_code = speed_code;
    xSemaphoreGive(service_state.mutex);
    return ESP_OK;
}

char can_bus_service_get_selected_speed_code(void)
{
    xSemaphoreTake(service_state.mutex, portMAX_DELAY);
    const char code = service_state.selected_speed_code;
    xSemaphoreGive(service_state.mutex);
    return code;
}

esp_err_t can_bus_service_start(can_bus_operating_mode_t operating_mode)
{
    xSemaphoreTake(service_state.mutex, portMAX_DELAY);

    if (service_state.bus_running) {
        if (service_state.operating_mode == operating_mode) {
            xSemaphoreGive(service_state.mutex);
            return ESP_OK;
        }
        const esp_err_t stop_result = uninstall_driver_locked();
        if (stop_result != ESP_OK) {
            xSemaphoreGive(service_state.mutex);
            return stop_result;
        }
    } else if (service_state.driver_installed) {
        const esp_err_t uninstall_result = uninstall_driver_locked();
        if (uninstall_result != ESP_OK) {
            xSemaphoreGive(service_state.mutex);
            return uninstall_result;
        }
    }

    const esp_err_t install_result = install_driver_locked(operating_mode);
    if (install_result != ESP_OK) {
        xSemaphoreGive(service_state.mutex);
        return install_result;
    }

    const esp_err_t start_result = twai_start();
    if (start_result != ESP_OK) {
        ESP_LOGE(TAG, "twai_start() failed: %s", esp_err_to_name(start_result));
        (void)uninstall_driver_locked();
        xSemaphoreGive(service_state.mutex);
        return start_result;
    }

    service_state.bus_running    = true;
    service_state.operating_mode = operating_mode;
    reset_status_flag_baselines_locked();

    xSemaphoreGive(service_state.mutex);
    return ESP_OK;
}

esp_err_t can_bus_service_stop(void)
{
    xSemaphoreTake(service_state.mutex, portMAX_DELAY);
    const esp_err_t result = uninstall_driver_locked();
    xSemaphoreGive(service_state.mutex);
    return result;
}

bool can_bus_service_is_running(void)
{
    xSemaphoreTake(service_state.mutex, portMAX_DELAY);
    const bool is_running = service_state.bus_running;
    xSemaphoreGive(service_state.mutex);
    return is_running;
}

can_bus_operating_mode_t can_bus_service_get_operating_mode(void)
{
    xSemaphoreTake(service_state.mutex, portMAX_DELAY);
    const can_bus_operating_mode_t mode = service_state.operating_mode;
    xSemaphoreGive(service_state.mutex);
    return mode;
}

/* Mutex удерживается только для state-check. twai_transmit() вызывается БЕЗ mutex.
   Race window: stop() выставит bus_running=false, подождёт drain wait (>= TX timeout),
   затем вызовет twai_driver_uninstall(). К этому моменту twai_transmit() завершился. */
esp_err_t can_bus_service_send_frame(const can_bus_frame_t *frame)
{
    if (frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (frame->data_length_code > CLASSICAL_CAN_MAX_DLC) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!frame->is_extended_identifier && frame->identifier > 0x7FFU) {
        return ESP_ERR_INVALID_ARG;
    }
    if (frame->is_extended_identifier && frame->identifier > 0x1FFFFFFFU) {
        return ESP_ERR_INVALID_ARG;
    }

    twai_message_t message;
    memset(&message, 0, sizeof(message));
    message.identifier       = frame->identifier;
    message.extd             = frame->is_extended_identifier ? 1U : 0U;
    message.rtr              = frame->is_remote_frame ? 1U : 0U;
    message.data_length_code = frame->data_length_code;

    if (!frame->is_remote_frame && frame->data_length_code > 0U) {
        memcpy(message.data, frame->data, frame->data_length_code);
    }

    xSemaphoreTake(service_state.mutex, portMAX_DELAY);
    const bool is_running     = service_state.bus_running;
    const bool is_listen_only = (service_state.operating_mode ==
                                  CAN_BUS_OPERATING_MODE_LISTEN_ONLY);
    xSemaphoreGive(service_state.mutex);

    if (!is_running) {
        return ESP_ERR_INVALID_STATE;
    }
    if (is_listen_only) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    return twai_transmit(&message,
                         pdMS_TO_TICKS(SLCAN_BRIDGE_CAN_TRANSMIT_TIMEOUT_MILLISECONDS));
}

/* Mutex только для state-check; twai_receive() без mutex.
   DLC clamp — defensive защита от out-of-bounds на RX path. */
esp_err_t can_bus_service_receive_frame(can_bus_frame_t *frame, TickType_t timeout_ticks)
{
    if (frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(service_state.mutex, portMAX_DELAY);
    const bool is_running = service_state.bus_running;
    xSemaphoreGive(service_state.mutex);

    if (!is_running) {
        return ESP_ERR_INVALID_STATE;
    }

    twai_message_t message;
    memset(&message, 0, sizeof(message));
    const esp_err_t receive_result = twai_receive(&message, timeout_ticks);
    if (receive_result != ESP_OK) {
        return receive_result;
    }

    /* Defensive clamp DLC — явная защита независимо от TWAI драйвера. */
    const uint8_t safe_dlc = (message.data_length_code > CLASSICAL_CAN_MAX_DLC)
                                  ? CLASSICAL_CAN_MAX_DLC
                                  : message.data_length_code;

    memset(frame, 0, sizeof(*frame));
    frame->identifier             = message.identifier;
    frame->is_extended_identifier = (bool)message.extd;
    frame->is_remote_frame        = (bool)message.rtr;
    frame->data_length_code       = safe_dlc;
    frame->timestamp_milliseconds =
        (uint16_t)((esp_timer_get_time() / 1000ULL) & 0xFFFFU);

    if (!frame->is_remote_frame && safe_dlc > 0U) {
        memcpy(frame->data, message.data, safe_dlc);
    }

    return ESP_OK;
}

can_bus_status_snapshot_t can_bus_service_get_status_snapshot(void)
{
    xSemaphoreTake(service_state.mutex, portMAX_DELAY);

    can_bus_status_snapshot_t snapshot = {
        .driver_installed    = service_state.driver_installed,
        .bus_running         = service_state.bus_running,
        .operating_mode      = service_state.operating_mode,
        .selected_speed_code = service_state.selected_speed_code,
        .slcan_status_flags  = build_slcan_status_flags_and_update_baselines_locked(),
    };

    xSemaphoreGive(service_state.mutex);
    return snapshot;
}
