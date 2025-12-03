/*
 * Magnolia OS — Architecture Layer
 * Target: ESP32-C3 (RISC-V)
 *
 * File: start.c
 * Description:
 *     Architecture-specific startup routine for the ESP32-C3 platform.
 *     Called from the Magnolia kernel bootstrap sequence to initialize
 *     low-level subsystems before the scheduler is started.
 *
 * Copyright (c) 2025
 * Magnolia Project Developers
 */

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "kernel/arch/m_hw_init.h"

static const char *TAG = "arch_esp32c3";

void m_kernel_task_entry(void *arg)
{
    ESP_LOGI(TAG, "Magnolia kernel task started.");

    for (;;) {
        /* Keep the root task alive; returning would trigger an abort. */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void m_arch_start(void)
{
    ESP_LOGI(TAG, "Magnolia ESP32-C3 booting...");

    magnolia_hw_init();

    xTaskCreatePinnedToCore(
        m_kernel_task_entry,
        "magnolia_root",
        4096,
        NULL,
        10,
        NULL,
        tskNO_AFFINITY
    );
}
