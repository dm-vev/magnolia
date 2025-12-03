#include "sdkconfig.h"

#ifdef CONFIG_MAGNOLIA_SCHED_SELFTESTS

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "kernel/core/sched/m_sched.h"
#include "kernel/core/sched/tests/m_sched_tests.h"
#include "kernel/core/timer/m_timer.h"

static const char *TAG = "sched_tests";

static bool test_report(const char *name, bool success)
{
    if (success) {
        ESP_LOGI(TAG, "[PASS] %s", name);
    } else {
        ESP_LOGE(TAG, "[FAIL] %s", name);
    }
    return success;
}

static void sched_worker_lifecycle(void *arg)
{
    SemaphoreHandle_t done = arg;
    if (done != NULL) {
        xSemaphoreGive(done);
    }
    m_sched_sleep_ms(5);
}

static bool run_test_create_destroy(void)
{
    static StaticSemaphore_t storage;
    SemaphoreHandle_t done = xSemaphoreCreateBinaryStatic(&storage);
    if (done == NULL) {
        return false;
    }

    m_sched_task_id_t id = M_SCHED_TASK_ID_INVALID;
    m_sched_task_options_t opts = {
        .name = "sched_spawn",
        .entry = sched_worker_lifecycle,
        .argument = done,
        .stack_depth = configMINIMAL_STACK_SIZE,
        .priority = (tskIDLE_PRIORITY + 1),
        .cpu_affinity = M_SCHED_CPU_AFFINITY_ANY,
    };

    if (m_sched_task_create(&opts, &id) != M_SCHED_OK) {
        return false;
    }

    if (xSemaphoreTake(done, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }

    m_sched_sleep_ms(5);
    m_sched_error_t res = m_sched_task_destroy(id);
    return (res == M_SCHED_ERR_NOT_FOUND);
}

static void sched_blocking_worker(void *arg)
{
    SemaphoreHandle_t trigger = arg;
    xSemaphoreTake(trigger, portMAX_DELAY);
}

static bool run_test_destroy_waiting(void)
{
    static StaticSemaphore_t storage;
    SemaphoreHandle_t trigger = xSemaphoreCreateBinaryStatic(&storage);
    if (trigger == NULL) {
        return false;
    }

    m_sched_task_id_t id = M_SCHED_TASK_ID_INVALID;
    m_sched_task_options_t opts = {
        .name = "sched_wait",
        .entry = sched_blocking_worker,
        .argument = trigger,
        .stack_depth = configMINIMAL_STACK_SIZE,
        .priority = (tskIDLE_PRIORITY + 1),
        .cpu_affinity = M_SCHED_CPU_AFFINITY_ANY,
    };

    if (m_sched_task_create(&opts, &id) != M_SCHED_OK) {
        return false;
    }

    m_sched_sleep_ms(5);
    m_sched_error_t destroy_res = m_sched_task_destroy(id);
    bool valid = m_sched_task_id_is_valid(id);
    xSemaphoreGive(trigger);
    return (destroy_res == M_SCHED_OK) && (valid == false);
}

static bool run_test_sleep_timing(void)
{
    m_timer_time_t before = m_timer_get_monotonic();
    m_sched_wait_result_t result = m_sched_sleep_ms(15);
    m_timer_time_t after = m_timer_get_monotonic();
    return (result == M_SCHED_WAIT_RESULT_OK) && ((after - before) >= 10000ULL);
}

static bool run_test_metadata_snapshot(void)
{
    m_sched_task_id_t id = M_SCHED_TASK_ID_INVALID;
    const char *tag = "sched_test_tag";
    int marker = 0xABCD;
    m_sched_task_options_t opts = {
        .name = "sched_meta",
        .entry = sched_worker_lifecycle,
        .argument = NULL,
        .stack_depth = configMINIMAL_STACK_SIZE,
        .priority = (tskIDLE_PRIORITY + 1),
        .tag = tag,
        .user_data = &marker,
        .cpu_affinity = M_SCHED_CPU_AFFINITY_ANY,
    };

    if (m_sched_task_create(&opts, &id) != M_SCHED_OK) {
        return false;
    }

    m_sched_task_metadata_t snapshot = {0};
    bool ok = m_sched_task_metadata_get(id, &snapshot);
    m_sched_sleep_ms(5);
    m_sched_task_destroy(id);
    ok &= (snapshot.user_data == &marker);
    ok &= (strncmp(snapshot.tag, tag, M_SCHED_TASK_TAG_MAX_LEN) == 0);
    return ok;
}

void m_sched_selftests_run(void)
{
    bool overall = true;
    overall &= test_report("task create/destroy", run_test_create_destroy());
    overall &= test_report("destroy while waiting", run_test_destroy_waiting());
    overall &= test_report("sleep timing", run_test_sleep_timing());
    overall &= test_report("metadata snapshot", run_test_metadata_snapshot());
    ESP_LOGI(TAG, "scheduler self-tests %s",
             overall ? "PASSED" : "FAILED");
}

#else

#include "kernel/core/sched/tests/m_sched_tests.h"


#endif /* CONFIG_MAGNOLIA_SCHED_SELFTESTS */
