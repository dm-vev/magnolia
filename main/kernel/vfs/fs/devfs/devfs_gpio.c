#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"

#include "kernel/core/vfs/m_vfs_types.h"
#include "kernel/vfs/fs/devfs/devfs.h"
#include "kernel/vfs/fs/devfs/devfs_gpio.h"
#include "kernel/vfs/fs/devfs/devfs_internal.h"

#ifndef SOC_GPIO_PIN_COUNT
#define SOC_GPIO_PIN_COUNT 40
#endif

#define DEVFS_GPIO_MAX_PINS SOC_GPIO_PIN_COUNT

#ifndef CONFIG_MAGNOLIA_DEVFS_GPIO_EVENT_QUEUE_LEN
#define CONFIG_MAGNOLIA_DEVFS_GPIO_EVENT_QUEUE_LEN 32
#endif

typedef struct {
    QueueHandle_t queue;
    TaskHandle_t notify_task;
    m_vfs_node_t *node;
    portMUX_TYPE lock;
    devfs_event_mask_t ready_mask;
    uint64_t subscribed_mask;
    uint8_t edge_type[DEVFS_GPIO_MAX_PINS];
    uint32_t debounce_us[DEVFS_GPIO_MAX_PINS];
    uint64_t last_ts_us[DEVFS_GPIO_MAX_PINS];
} devfs_gpio_device_t;

static const char *const GPIO_TAG = "devfs_gpio";
static devfs_gpio_device_t g_devfs_gpio;
static bool s_gpio_isr_installed = false;

static inline bool devfs_gpio_pin_valid(uint32_t pin)
{
    return pin < DEVFS_GPIO_MAX_PINS;
}

static gpio_int_type_t devfs_gpio_edge_to_intr(uint8_t edge)
{
    switch (edge) {
    case DEVFS_GPIO_EDGE_RISING:
        return GPIO_INTR_POSEDGE;
    case DEVFS_GPIO_EDGE_FALLING:
        return GPIO_INTR_NEGEDGE;
    case DEVFS_GPIO_EDGE_BOTH:
        return GPIO_INTR_ANYEDGE;
    default:
        return GPIO_INTR_DISABLE;
    }
}

static bool devfs_gpio_mask_empty(uint64_t mask)
{
    return mask == 0;
}

static void devfs_gpio_notify_task(void *arg)
{
    devfs_gpio_device_t *device = (devfs_gpio_device_t *)arg;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (device == NULL) {
            continue;
        }

        devfs_event_mask_t mask = 0;
        m_vfs_node_t *node = NULL;
        portENTER_CRITICAL(&device->lock);
        if (device->queue != NULL &&
                uxQueueMessagesWaiting(device->queue) > 0) {
            mask = DEVFS_EVENT_READABLE;
        }
        node = device->node;
        portEXIT_CRITICAL(&device->lock);

        if (node != NULL) {
            devfs_notify(node, mask);
        }
    }
}

static void IRAM_ATTR devfs_gpio_isr_handler(void *arg)
{
    uint32_t pin = (uint32_t)(uintptr_t)arg;
    if (!devfs_gpio_pin_valid(pin)) {
        return;
    }

    devfs_gpio_device_t *device = &g_devfs_gpio;
    if (device->queue == NULL) {
        return;
    }

    uint64_t now = (uint64_t)esp_timer_get_time();
    uint32_t debounce = device->debounce_us[pin];
    uint64_t last = device->last_ts_us[pin];
    if (debounce > 0 && (now - last) < debounce) {
        return;
    }
    device->last_ts_us[pin] = now;

    uint8_t edge = device->edge_type[pin];
    if (edge == DEVFS_GPIO_EDGE_BOTH) {
        int level = gpio_get_level((gpio_num_t)pin);
        edge = (level != 0) ? DEVFS_GPIO_EDGE_RISING : DEVFS_GPIO_EDGE_FALLING;
    }

    devfs_gpio_event_t event = {
        .gpio_num = pin,
        .edge = edge,
        .timestamp_us = now,
    };

    BaseType_t should_yield = pdFALSE;
    if (xQueueSendFromISR(device->queue, &event, &should_yield) == pdPASS) {
        if (device->notify_task != NULL) {
            vTaskNotifyGiveFromISR(device->notify_task, &should_yield);
        }
    }
    if (should_yield == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static m_vfs_error_t devfs_gpio_read(void *private_data,
                                     void *buffer,
                                     size_t size,
                                     size_t *read)
{
    devfs_gpio_device_t *device = (devfs_gpio_device_t *)private_data;
    if (device == NULL || buffer == NULL || read == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    if (size < sizeof(devfs_gpio_event_t)) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    size_t max_events = size / sizeof(devfs_gpio_event_t);
    devfs_gpio_event_t *out = (devfs_gpio_event_t *)buffer;
    size_t count = 0;
    while (count < max_events) {
        if (xQueueReceive(device->queue, &out[count], 0) != pdTRUE) {
            break;
        }
        ++count;
    }

    if (count == 0) {
        *read = 0;
        return M_VFS_ERR_WOULD_BLOCK;
    }

    *read = count * sizeof(devfs_gpio_event_t);

    devfs_event_mask_t mask = 0;
    if (uxQueueMessagesWaiting(device->queue) > 0) {
        mask = DEVFS_EVENT_READABLE;
    }
    if (device->node != NULL) {
        devfs_notify(device->node, mask);
    }

    return M_VFS_ERR_OK;
}

static m_vfs_error_t devfs_gpio_ioctl(void *private_data,
                                      unsigned long request,
                                      void *arg)
{
    devfs_gpio_device_t *device = (devfs_gpio_device_t *)private_data;
    if (device == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    switch (request) {
    case DEVFS_IOCTL_GPIO_CONFIG: {
        if (arg == NULL) {
            return M_VFS_ERR_INVALID_PARAM;
        }
        const devfs_gpio_config_t *cfg = (const devfs_gpio_config_t *)arg;
        if (devfs_gpio_mask_empty(cfg->pin_mask)) {
            return M_VFS_ERR_INVALID_PARAM;
        }

        gpio_config_t io_conf = {
            .pin_bit_mask = cfg->pin_mask,
            .mode = GPIO_MODE_DISABLE,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };

        switch (cfg->direction) {
        case DEVFS_GPIO_DIR_IN:
            io_conf.mode = GPIO_MODE_INPUT;
            break;
        case DEVFS_GPIO_DIR_OUT:
            io_conf.mode = (cfg->drive == DEVFS_GPIO_DRIVE_OPEN_DRAIN)
                ? GPIO_MODE_OUTPUT_OD
                : GPIO_MODE_OUTPUT;
            break;
        case DEVFS_GPIO_DIR_IN_OUT:
            io_conf.mode = (cfg->drive == DEVFS_GPIO_DRIVE_OPEN_DRAIN)
                ? GPIO_MODE_INPUT_OUTPUT_OD
                : GPIO_MODE_INPUT_OUTPUT;
            break;
        default:
            return M_VFS_ERR_INVALID_PARAM;
        }

        switch (cfg->pull) {
        case DEVFS_GPIO_PULL_UP:
            io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            break;
        case DEVFS_GPIO_PULL_DOWN:
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
            break;
        default:
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            break;
        }

        if (gpio_config(&io_conf) != ESP_OK) {
            return M_VFS_ERR_IO;
        }

        if (cfg->direction != DEVFS_GPIO_DIR_IN) {
            for (uint32_t pin = 0; pin < DEVFS_GPIO_MAX_PINS; ++pin) {
                if ((cfg->pin_mask & (1ULL << pin)) == 0) {
                    continue;
                }
                int level = (cfg->values & (1ULL << pin)) ? 1 : 0;
                gpio_set_level((gpio_num_t)pin, level);
            }
        }

        return M_VFS_ERR_OK;
    }
    case DEVFS_IOCTL_GPIO_READ: {
        if (arg == NULL) {
            return M_VFS_ERR_INVALID_PARAM;
        }
        devfs_gpio_values_t *req = (devfs_gpio_values_t *)arg;
        if (devfs_gpio_mask_empty(req->pin_mask)) {
            return M_VFS_ERR_INVALID_PARAM;
        }
        uint64_t values = 0;
        for (uint32_t pin = 0; pin < DEVFS_GPIO_MAX_PINS; ++pin) {
            if ((req->pin_mask & (1ULL << pin)) == 0) {
                continue;
            }
            int level = gpio_get_level((gpio_num_t)pin);
            if (level) {
                values |= (1ULL << pin);
            }
        }
        req->values = values;
        return M_VFS_ERR_OK;
    }
    case DEVFS_IOCTL_GPIO_WRITE: {
        if (arg == NULL) {
            return M_VFS_ERR_INVALID_PARAM;
        }
        devfs_gpio_values_t *req = (devfs_gpio_values_t *)arg;
        if (devfs_gpio_mask_empty(req->pin_mask)) {
            return M_VFS_ERR_INVALID_PARAM;
        }
        for (uint32_t pin = 0; pin < DEVFS_GPIO_MAX_PINS; ++pin) {
            if ((req->pin_mask & (1ULL << pin)) == 0) {
                continue;
            }
            int level = (req->values & (1ULL << pin)) ? 1 : 0;
            gpio_set_level((gpio_num_t)pin, level);
        }
        return M_VFS_ERR_OK;
    }
    case DEVFS_IOCTL_GPIO_EDGE_SUBSCRIBE: {
        if (arg == NULL) {
            return M_VFS_ERR_INVALID_PARAM;
        }
        devfs_gpio_edge_config_t *req = (devfs_gpio_edge_config_t *)arg;
        if (devfs_gpio_mask_empty(req->pin_mask)) {
            return M_VFS_ERR_INVALID_PARAM;
        }

        if (!s_gpio_isr_installed) {
            esp_err_t err = gpio_install_isr_service(0);
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                return M_VFS_ERR_IO;
            }
            s_gpio_isr_installed = true;
        }

        for (uint32_t pin = 0; pin < DEVFS_GPIO_MAX_PINS; ++pin) {
            if ((req->pin_mask & (1ULL << pin)) == 0) {
                continue;
            }
            gpio_isr_handler_remove((gpio_num_t)pin);
            gpio_set_intr_type((gpio_num_t)pin, devfs_gpio_edge_to_intr(req->edge));
            if (req->edge != DEVFS_GPIO_EDGE_NONE) {
                gpio_isr_handler_add((gpio_num_t)pin,
                                     devfs_gpio_isr_handler,
                                     (void *)(uintptr_t)pin);
                gpio_intr_enable((gpio_num_t)pin);
                device->edge_type[pin] = req->edge;
                device->debounce_us[pin] = req->debounce_us;
                device->subscribed_mask |= (1ULL << pin);
            } else {
                gpio_intr_disable((gpio_num_t)pin);
                device->edge_type[pin] = DEVFS_GPIO_EDGE_NONE;
                device->debounce_us[pin] = 0;
                device->subscribed_mask &= ~(1ULL << pin);
            }
        }
        return M_VFS_ERR_OK;
    }
    case DEVFS_IOCTL_GPIO_EDGE_UNSUBSCRIBE: {
        if (arg == NULL) {
            return M_VFS_ERR_INVALID_PARAM;
        }
        devfs_gpio_edge_config_t *req = (devfs_gpio_edge_config_t *)arg;
        if (devfs_gpio_mask_empty(req->pin_mask)) {
            return M_VFS_ERR_INVALID_PARAM;
        }
        for (uint32_t pin = 0; pin < DEVFS_GPIO_MAX_PINS; ++pin) {
            if ((req->pin_mask & (1ULL << pin)) == 0) {
                continue;
            }
            gpio_intr_disable((gpio_num_t)pin);
            gpio_isr_handler_remove((gpio_num_t)pin);
            device->edge_type[pin] = DEVFS_GPIO_EDGE_NONE;
            device->debounce_us[pin] = 0;
            device->subscribed_mask &= ~(1ULL << pin);
        }
        return M_VFS_ERR_OK;
    }
    default:
        return M_VFS_ERR_NOT_SUPPORTED;
    }
}

static uint32_t devfs_gpio_poll(void *private_data)
{
    devfs_gpio_device_t *device = (devfs_gpio_device_t *)private_data;
    if (device == NULL) {
        return 0;
    }

    if (uxQueueMessagesWaiting(device->queue) > 0) {
        return DEVFS_EVENT_READABLE;
    }
    return 0;
}

static void devfs_gpio_attach_node(const devfs_entry_t *entry,
                                   devfs_device_node_t *record)
{
    if (entry == NULL || record == NULL) {
        return;
    }
    devfs_gpio_device_t *device = (devfs_gpio_device_t *)entry->private_data;
    if (device == NULL) {
        return;
    }
    portENTER_CRITICAL(&device->lock);
    device->node = record->node;
    portEXIT_CRITICAL(&device->lock);
}

static void devfs_gpio_detach_node(const devfs_entry_t *entry,
                                   devfs_device_node_t *record)
{
    (void)record;
    if (entry == NULL) {
        return;
    }
    devfs_gpio_device_t *device = (devfs_gpio_device_t *)entry->private_data;
    if (device == NULL) {
        return;
    }
    portENTER_CRITICAL(&device->lock);
    device->node = NULL;
    portEXIT_CRITICAL(&device->lock);
}

static const devfs_ops_t s_devfs_gpio_ops = {
    .read = devfs_gpio_read,
    .ioctl = devfs_gpio_ioctl,
    .poll = devfs_gpio_poll,
};

bool devfs_gpio_register(void)
{
    devfs_gpio_device_t *device = &g_devfs_gpio;
    memset(device, 0, sizeof(*device));
    device->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;

    device->queue = xQueueCreate(CONFIG_MAGNOLIA_DEVFS_GPIO_EVENT_QUEUE_LEN,
                                 sizeof(devfs_gpio_event_t));
    if (device->queue == NULL) {
        ESP_LOGE(GPIO_TAG, "event queue create failed");
        return false;
    }

    BaseType_t task_ok = xTaskCreate(
        devfs_gpio_notify_task,
        "devfs_gpio",
        4096,
        device,
        6,
        &device->notify_task);
    if (task_ok != pdPASS) {
        ESP_LOGE(GPIO_TAG, "notify task create failed");
        vQueueDelete(device->queue);
        device->queue = NULL;
        return false;
    }

    m_vfs_error_t err = devfs_register_ext("/dev/gpio",
                                           &s_devfs_gpio_ops,
                                           device,
                                           devfs_gpio_attach_node,
                                           devfs_gpio_detach_node);
    if (err != M_VFS_ERR_OK) {
        ESP_LOGE(GPIO_TAG, "register /dev/gpio failed err=%d", err);
        vQueueDelete(device->queue);
        device->queue = NULL;
        return false;
    }

    ESP_LOGI(GPIO_TAG, "registered /dev/gpio");
    return true;
}
