/*
 * Magnolia OS — IPC Subsystem
 * Purpose:
 *     Magnolia entry point.
 * Description:
 *     Log a greeting and hand off control to the architecture-specific startup
 *     routine.
 *
 * © 2025 Magnolia Project
 */

#include "esp_log.h"
#include "kernel/arch/m_arch.h"

static const char *TAG = "magnolia";

void app_main(void)
{
    ESP_LOGI(TAG, "Hello, Magnolia!");
    m_arch_start();
}
