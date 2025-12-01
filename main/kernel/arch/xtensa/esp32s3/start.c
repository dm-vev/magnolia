/*
 * Magnolia OS — Architecture Layer
 * Target: ESP32S3 (Xtensa)
 *
 * File: start.c
 * Description:
 *     Architecture-specific startup routine for the ESP32S3 platform.
 *     Called from the Magnolia kernel bootstrap sequence to initialize
 *     low-level subsystems before the scheduler is started.
 *
 * Copyright (c) 2025
 * Magnolia Project Developers
 */

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "arch_esp32s3";

void m_kernel_task_entry(void *arg) {
    ESP_LOGI(TAG, "Magnolia kernel task started.");

    for (;;) {
        /* Keep the root task alive; returning would trigger an abort. */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief Perform early architecture-specific initialization.
 *
 * This function is the first architecture-level entry point executed by
 * Magnolia OS during boot. It prepares low-level hardware state required
 * before the kernel scheduler and subsystems are started.
 */
void m_arch_start(void)
{
    ESP_LOGI(TAG, "Magnolia ESP32-S3 booting...");


    xTaskCreatePinnedToCore(
        m_kernel_task_entry,  // entrypoint Magnolia
        "magnolia_root",
        4096,
        NULL,
        10, // приоритет
        NULL,
        tskNO_AFFINITY
    );
}
