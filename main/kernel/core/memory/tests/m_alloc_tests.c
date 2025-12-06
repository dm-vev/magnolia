#include "sdkconfig.h"

#if CONFIG_MAGNOLIA_ALLOC_ENABLED && CONFIG_MAGNOLIA_ALLOC_SELFTESTS

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "kernel/arch/m_arch.h"
#include "kernel/core/job/m_job.h"
#include "kernel/core/memory/m_alloc.h"

#define REGION_ALLOC_BLOCK_SIZE                                              \
    ((CONFIG_MAGNOLIA_ALLOC_REGION_SIZE / 8) > 0                              \
         ? (CONFIG_MAGNOLIA_ALLOC_REGION_SIZE / 8)                            \
         : 64)
#define REGION_ALLOCATIONS_LIMIT (CONFIG_MAGNOLIA_ALLOC_MAX_REGIONS_PER_JOB * 16)

static const char *TAG = "alloc_tests";

static bool test_report(const char *name, bool success)
{
    if (success) {
        ESP_LOGI(TAG, "[PASS] %s", name);
    } else {
        ESP_LOGE(TAG, "[FAIL] %s", name);
    }
    return success;
}

static m_job_queue_t *alloc_test_queue(size_t worker_count)
{
    m_job_queue_config_t config = M_JOB_QUEUE_CONFIG_DEFAULT;
    config.capacity = 8;
    config.worker_count = worker_count;
    config.stack_depth = 8192;
    config.priority = (tskIDLE_PRIORITY + 1);
    config.debug_log = false;
    return m_job_queue_create(&config);
}

static bool await_job_result(m_job_handle_t *job,
                             m_job_result_status_t expected)
{
    if (job == NULL) {
        return false;
    }
    m_job_result_descriptor_t result = {0};
    bool ok = (m_job_wait_for_job(job, &result) == M_JOB_FUTURE_WAIT_OK);
    ok &= (result.status == expected);
    ok &= (m_job_handle_destroy(job) == M_JOB_OK);
    return ok;
}

static bool run_test_arch_malloc_basic(void)
{
    static const size_t sizes[] = {1, 2, 7, 16, 31, 64, 128};
    for (size_t i = 0; i < (sizeof(sizes) / sizeof(sizes[0])); ++i) {
        size_t chunk = sizes[i];
        void *ptr = m_arch_malloc(chunk);
        if (ptr == NULL) {
            return false;
        }
        if (((uintptr_t)ptr & (sizeof(void *) - 1)) != 0) {
            m_arch_free(ptr);
            return false;
        }
        memset(ptr, 0x5A, chunk);
        uint8_t *pattern = ptr;
        for (size_t j = 0; j < chunk; ++j) {
            if (pattern[j] != 0x5A) {
                m_arch_free(ptr);
                return false;
            }
        }
        m_arch_free(ptr);
    }
    if (m_arch_malloc(0) != NULL) {
        return false;
    }
    return true;
}

static m_job_result_descriptor_t job_dropin_sequence(m_job_id_t job, void *arg)
{
    (void)arg;
    magnolia_alloc_job_stats_t stats = {0};
    m_alloc_get_job_stats(job->ctx, &stats);
    if (stats.used_bytes != 0 || stats.region_count != 0) {
        return m_job_result_error("unexpected pre-alloc stats", 0);
    }

    uint8_t *ptr = malloc(64);
    if (ptr == NULL) {
        return m_job_result_error("malloc failed", 0);
    }
    for (size_t i = 0; i < 64; ++i) {
        ptr[i] = (uint8_t)i;
    }

    uint8_t *expanded = realloc(ptr, 128);
    if (expanded == NULL) {
        free(ptr);
        return m_job_result_error("realloc grow failed", 0);
    }
    for (size_t i = 0; i < 64; ++i) {
        if (expanded[i] != (uint8_t)i) {
            free(expanded);
            return m_job_result_error("realloc grow corrupted", 0);
        }
    }

    uint8_t *shrunk = realloc(expanded, 32);
    if (shrunk == NULL) {
        free(expanded);
        return m_job_result_error("realloc shrink failed", 0);
    }
    for (size_t i = 0; i < 32; ++i) {
        if (shrunk[i] != (uint8_t)i) {
            free(shrunk);
            return m_job_result_error("realloc shrink corrupted", 0);
        }
    }

    uint8_t *zeroed = calloc(8, sizeof(uint32_t));
    if (zeroed == NULL) {
        free(shrunk);
        return m_job_result_error("calloc failed", 0);
    }
    for (size_t i = 0; i < (8 * sizeof(uint32_t)); ++i) {
        if (zeroed[i] != 0) {
            free(zeroed);
            free(shrunk);
            return m_job_result_error("calloc not zeroed", 0);
        }
    }
    free(zeroed);

    void *null_alloc = realloc(NULL, 24);
    if (null_alloc == NULL) {
        free(shrunk);
        return m_job_result_error("realloc(NULL) failed", 0);
    }
    free(null_alloc);

    void *freed = realloc(shrunk, 0);
    if (freed != NULL) {
        free(freed);
        return m_job_result_error("realloc(ptr, 0) must return NULL", 0);
    }

    m_alloc_get_job_stats(job->ctx, &stats);
    if (stats.used_bytes != 0) {
        return m_job_result_error("leaked bytes after frees", 0);
    }
    if (stats.peak_bytes == 0 || stats.region_count == 0) {
        return m_job_result_error("invalid peak/region stats", 0);
    }
    return m_job_result_success(NULL, 0);
}

static bool run_test_dropin_malloc_sequence(void)
{
    m_job_queue_t *queue = alloc_test_queue(1);
    if (queue == NULL) {
        return false;
    }

    m_job_handle_t *job = NULL;
    bool ok = (m_job_queue_submit_with_handle(queue,
                                              job_dropin_sequence,
                                              NULL,
                                              &job)
               == M_JOB_OK);
    if (ok) {
        ok &= await_job_result(job, M_JOB_RESULT_SUCCESS);
    } else if (job != NULL) {
        await_job_result(job, M_JOB_RESULT_SUCCESS);
        ok = false;
    }
    m_job_queue_destroy(queue);
    return ok;
}

static m_job_result_descriptor_t job_region_limits(m_job_id_t job, void *arg)
{
    (void)arg;
    void **buffers = pvPortMalloc(REGION_ALLOCATIONS_LIMIT * sizeof(void *));
    size_t snapshot = 0;
    magnolia_alloc_job_stats_t stats = {0};
    size_t limit_from_config = CONFIG_MAGNOLIA_ALLOC_MAX_REGIONS_PER_JOB;
    if (limit_from_config == 0) {
        limit_from_config = REGION_ALLOCATIONS_LIMIT;
    }
    if (CONFIG_MAGNOLIA_ALLOC_MAX_HEAP_SIZE_PER_JOB > 0) {
        size_t heap_limit =
            CONFIG_MAGNOLIA_ALLOC_MAX_HEAP_SIZE_PER_JOB
            / CONFIG_MAGNOLIA_ALLOC_REGION_SIZE;
        if (heap_limit == 0) {
            heap_limit = 1;
        }
        if (heap_limit < limit_from_config) {
            limit_from_config = heap_limit;
        }
    }
    if (limit_from_config > REGION_ALLOCATIONS_LIMIT) {
        limit_from_config = REGION_ALLOCATIONS_LIMIT;
    }
    size_t target_regions = limit_from_config;

    if (buffers == NULL) {
        return m_job_result_error("buffer allocation failed", 0);
    }

    size_t allocated = 0;
    for (size_t i = 0; i < REGION_ALLOCATIONS_LIMIT; ++i) {
        void *ptr = malloc(REGION_ALLOC_BLOCK_SIZE);
        if (ptr == NULL) {
            break;
        }
        buffers[allocated++] = ptr;
        m_alloc_get_job_stats(job->ctx, &stats);
        if (stats.region_count > snapshot) {
            snapshot = stats.region_count;
            if (target_regions > 0 && snapshot >= target_regions) {
                break;
            }
        }
    }

    for (size_t i = 0; i < allocated; ++i) {
        free(buffers[i]);
    }
    vPortFree(buffers);

    if (target_regions > 0 && snapshot < target_regions) {
        return m_job_result_error("regions did not grow",
                                  0);
    }
    return m_job_result_success(NULL, 0);
}

static bool run_test_region_limits(void)
{
    m_job_queue_t *queue = alloc_test_queue(1);
    if (queue == NULL) {
        return false;
    }

    m_job_handle_t *job = NULL;
    bool ok = (m_job_queue_submit_with_handle(queue,
                                              job_region_limits,
                                              NULL,
                                              &job)
               == M_JOB_OK);
    if (ok) {
        ok &= await_job_result(job, M_JOB_RESULT_SUCCESS);
    } else if (job != NULL) {
        await_job_result(job, M_JOB_RESULT_SUCCESS);
        ok = false;
    }
    m_job_queue_destroy(queue);
    return ok;
}

static m_job_result_descriptor_t job_double_free(m_job_id_t job, void *arg)
{
    (void)arg;
    void *ptr = malloc(32);
    if (ptr == NULL) {
        return m_job_result_error("alloc failed", 0);
    }
    free(ptr);
    free(ptr);
    return m_job_result_success(NULL, 0);
}

static bool run_test_double_free_cancel(void)
{
    m_job_queue_t *queue = alloc_test_queue(1);
    if (queue == NULL) {
        return false;
    }

    m_job_handle_t *job = NULL;
    bool ok = (m_job_queue_submit_with_handle(queue,
                                              job_double_free,
                                              NULL,
                                              &job)
               == M_JOB_OK);
    if (ok) {
        ok &= await_job_result(job, M_JOB_RESULT_CANCELLED);
    } else if (job != NULL) {
        await_job_result(job, M_JOB_RESULT_CANCELLED);
        ok = false;
    }
    m_job_queue_destroy(queue);
    return ok;
}

static m_job_result_descriptor_t job_realloc_after_free(m_job_id_t job,
                                                       void *arg)
{
    (void)arg;
    void *ptr = malloc(16);
    if (ptr == NULL) {
        return m_job_result_error("alloc failed", 0);
    }
    free(ptr);
    realloc(ptr, 32);
    return m_job_result_success(NULL, 0);
}

static bool run_test_realloc_after_free_cancel(void)
{
    m_job_queue_t *queue = alloc_test_queue(1);
    if (queue == NULL) {
        return false;
    }

    m_job_handle_t *job = NULL;
    bool ok = (m_job_queue_submit_with_handle(queue,
                                              job_realloc_after_free,
                                              NULL,
                                              &job)
               == M_JOB_OK);
    if (ok) {
        ok &= await_job_result(job, M_JOB_RESULT_CANCELLED);
    } else if (job != NULL) {
        await_job_result(job, M_JOB_RESULT_CANCELLED);
        ok = false;
    }
    m_job_queue_destroy(queue);
    return ok;
}

static m_job_result_descriptor_t job_invalid_free(m_job_id_t job, void *arg)
{
    (void)job;
    (void)arg;
    free((void *)0x12345678);
    return m_job_result_success(NULL, 0);
}

static bool run_test_invalid_free_cancel(void)
{
    m_job_queue_t *queue = alloc_test_queue(1);
    if (queue == NULL) {
        return false;
    }

    m_job_handle_t *job = NULL;
    bool ok = (m_job_queue_submit_with_handle(queue,
                                              job_invalid_free,
                                              NULL,
                                              &job)
               == M_JOB_OK);
    if (ok) {
        ok &= await_job_result(job, M_JOB_RESULT_CANCELLED);
    } else if (job != NULL) {
        await_job_result(job, M_JOB_RESULT_CANCELLED);
        ok = false;
    }
    m_job_queue_destroy(queue);
    return ok;
}

typedef struct {
    SemaphoreHandle_t ready;
    SemaphoreHandle_t start;
    SemaphoreHandle_t done;
    size_t observed_used_bytes;
    size_t observed_region_count;
} parallel_job_ctx_t;

static m_job_result_descriptor_t job_parallel_worker(m_job_id_t job, void *arg)
{
    parallel_job_ctx_t *ctx = arg;
    void *ptr = malloc(256);
    if (ptr == NULL) {
        return m_job_result_error("alloc failed", 0);
    }

    magnolia_alloc_job_stats_t stats = {0};
    m_alloc_get_job_stats(job->ctx, &stats);
    ctx->observed_used_bytes = stats.used_bytes;
    ctx->observed_region_count = stats.region_count;

    xSemaphoreGive(ctx->ready);
    xSemaphoreTake(ctx->start, portMAX_DELAY);
    free(ptr);
    xSemaphoreGive(ctx->done);
    return m_job_result_success(NULL, 0);
}

static bool run_test_parallel_job_isolation(void)
{
    m_job_queue_t *queue = alloc_test_queue(2);
    if (queue == NULL) {
        return false;
    }

    parallel_job_ctx_t contexts[2] = {0};
    StaticSemaphore_t ready_storage[2];
    StaticSemaphore_t start_storage[2];
    StaticSemaphore_t done_storage[2];

    for (size_t i = 0; i < 2; ++i) {
        contexts[i].ready = xSemaphoreCreateBinaryStatic(&ready_storage[i]);
        contexts[i].start = xSemaphoreCreateBinaryStatic(&start_storage[i]);
        contexts[i].done = xSemaphoreCreateBinaryStatic(&done_storage[i]);
        if (contexts[i].ready == NULL || contexts[i].start == NULL
            || contexts[i].done == NULL) {
            for (size_t j = 0; j <= i; ++j) {
                if (contexts[j].ready != NULL) {
                    vSemaphoreDelete(contexts[j].ready);
                }
                if (contexts[j].start != NULL) {
                    vSemaphoreDelete(contexts[j].start);
                }
                if (contexts[j].done != NULL) {
                    vSemaphoreDelete(contexts[j].done);
                }
            }
            m_job_queue_destroy(queue);
            return false;
        }
    }

    m_job_handle_t *handles[2] = {0};
    for (size_t i = 0; i < 2; ++i) {
        if (m_job_queue_submit_with_handle(queue,
                                           job_parallel_worker,
                                           &contexts[i],
                                           &handles[i]) != M_JOB_OK) {
            m_job_queue_destroy(queue);
            for (size_t j = 0; j < 2; ++j) {
                if (contexts[j].ready != NULL) {
                    vSemaphoreDelete(contexts[j].ready);
                }
                if (contexts[j].start != NULL) {
                    vSemaphoreDelete(contexts[j].start);
                }
                if (contexts[j].done != NULL) {
                    vSemaphoreDelete(contexts[j].done);
                }
            }
            return false;
        }
    }

    bool ok = true;
    for (size_t i = 0; i < 2; ++i) {
        ok &= (xSemaphoreTake(contexts[i].ready, portMAX_DELAY) == pdTRUE);
        ok &= (contexts[i].observed_used_bytes > 0);
        ok &= (contexts[i].observed_region_count > 0);
    }

    for (size_t i = 0; i < 2; ++i) {
        xSemaphoreGive(contexts[i].start);
    }
    for (size_t i = 0; i < 2; ++i) {
        ok &= (xSemaphoreTake(contexts[i].done, portMAX_DELAY) == pdTRUE);
    }

    for (size_t i = 0; i < 2; ++i) {
        ok &= await_job_result(handles[i], M_JOB_RESULT_SUCCESS);
    }

    for (size_t i = 0; i < 2; ++i) {
        vSemaphoreDelete(contexts[i].ready);
        vSemaphoreDelete(contexts[i].start);
        vSemaphoreDelete(contexts[i].done);
    }

    m_job_queue_destroy(queue);
    return ok;
}

typedef struct {
    SemaphoreHandle_t ready;
    SemaphoreHandle_t release;
    SemaphoreHandle_t done;
    void *ptr;
} shared_alloc_ctx_t;

static m_job_result_descriptor_t job_shared_alloc(m_job_id_t job, void *arg)
{
    shared_alloc_ctx_t *ctx = arg;
    ctx->ptr = malloc(64);
    if (ctx->ptr == NULL) {
        return m_job_result_error("alloc failed", 0);
    }
    xSemaphoreGive(ctx->ready);
    xSemaphoreTake(ctx->release, portMAX_DELAY);
    free(ctx->ptr);
    ctx->ptr = NULL;
    xSemaphoreGive(ctx->done);
    return m_job_result_success(NULL, 0);
}

static m_job_result_descriptor_t job_misused_free(m_job_id_t job, void *arg)
{
    void *target = arg;
    free(target);
    (void)job;
    return m_job_result_success(NULL, 0);
}

static bool run_test_cross_job_free_cancel(void)
{
    m_job_queue_t *queue = alloc_test_queue(2);
    if (queue == NULL) {
        return false;
    }

    shared_alloc_ctx_t ctx = {0};
    StaticSemaphore_t ready_storage;
    StaticSemaphore_t release_storage;
    StaticSemaphore_t done_storage;

    ctx.ready = xSemaphoreCreateBinaryStatic(&ready_storage);
    ctx.release = xSemaphoreCreateBinaryStatic(&release_storage);
    ctx.done = xSemaphoreCreateBinaryStatic(&done_storage);
    if (ctx.ready == NULL || ctx.release == NULL || ctx.done == NULL) {
        m_job_queue_destroy(queue);
        return false;
    }

    m_job_handle_t *claimer = NULL;
    if (m_job_queue_submit_with_handle(queue,
                                       job_shared_alloc,
                                       &ctx,
                                       &claimer)
        != M_JOB_OK) {
        vSemaphoreDelete(ctx.ready);
        vSemaphoreDelete(ctx.release);
        vSemaphoreDelete(ctx.done);
        m_job_queue_destroy(queue);
        return false;
    }

    if (xSemaphoreTake(ctx.ready, portMAX_DELAY) != pdTRUE) {
        await_job_result(claimer, M_JOB_RESULT_SUCCESS);
        vSemaphoreDelete(ctx.ready);
        vSemaphoreDelete(ctx.release);
        vSemaphoreDelete(ctx.done);
        m_job_queue_destroy(queue);
        return false;
    }

    m_job_handle_t *misuser = NULL;
    bool ok = (m_job_queue_submit_with_handle(queue,
                                              job_misused_free,
                                              ctx.ptr,
                                              &misuser)
               == M_JOB_OK);
    if (ok) {
        ok &= await_job_result(misuser, M_JOB_RESULT_CANCELLED);
    } else if (misuser != NULL) {
        await_job_result(misuser, M_JOB_RESULT_CANCELLED);
        ok = false;
    }

    xSemaphoreGive(ctx.release);
    if (xSemaphoreTake(ctx.done, portMAX_DELAY) != pdTRUE) {
        ok = false;
    }
    ok &= await_job_result(claimer, M_JOB_RESULT_SUCCESS);

    vSemaphoreDelete(ctx.ready);
    vSemaphoreDelete(ctx.release);
    vSemaphoreDelete(ctx.done);
    m_job_queue_destroy(queue);
    return ok;
}

void m_alloc_selftests_run(void)
{
    bool overall = true;
    overall &= test_report("m_arch malloc basics", run_test_arch_malloc_basic());
    overall &= test_report("drop-in malloc/calloc/realloc", run_test_dropin_malloc_sequence());
    overall &= test_report("region limit enforcement", run_test_region_limits());
    overall &= test_report("double free detection", run_test_double_free_cancel());
    overall &= test_report("realloc after free detection",
                           run_test_realloc_after_free_cancel());
    overall &= test_report("invalid free detection", run_test_invalid_free_cancel());
    overall &= test_report("parallel job heap isolation",
                           run_test_parallel_job_isolation());
    overall &= test_report("cross job free rejection",
                           run_test_cross_job_free_cancel());
    ESP_LOGI(TAG, "allocator self-tests %s",
             overall ? "PASSED" : "FAILED");
}

#else

#include "kernel/core/memory/tests/m_alloc_tests.h"

#endif /* CONFIG_MAGNOLIA_ALLOC_ENABLED && CONFIG_MAGNOLIA_ALLOC_SELFTESTS */
