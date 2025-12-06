#include "kernel/core/memory/m_alloc.h"

#include "sdkconfig.h"

#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/reent.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include "kernel/core/job/m_job_core.h"
#include "kernel/arch/m_arch.h"

#if !CONFIG_MAGNOLIA_ALLOC_ENABLED
#error "Magnolia allocator must be enabled"
#endif

#define TAG "m_alloc"

#define MAGNOLIA_ALLOC_ALIGNMENT (sizeof(void *))
#define MAGNOLIA_ALLOC_ROUND_UP(value, align)                                   (((value) + ((align) - 1)) & ~((align) - 1))

#define MAGNOLIA_ALLOC_REGION_BYTES CONFIG_MAGNOLIA_ALLOC_REGION_SIZE
#define MAGNOLIA_ALLOC_MAX_REGIONS CONFIG_MAGNOLIA_ALLOC_MAX_REGIONS_PER_JOB
#define MAGNOLIA_ALLOC_MAX_JOB_HEAP CONFIG_MAGNOLIA_ALLOC_MAX_HEAP_SIZE_PER_JOB
#define MAGNOLIA_ALLOC_MAGIC 0x4D41474D

#if CONFIG_MAGNOLIA_ALLOC_DEBUG
#define MAGNOLIA_ALLOC_DEBUG_LOG(...) ESP_LOGD(TAG, __VA_ARGS__)
#else
#define MAGNOLIA_ALLOC_DEBUG_LOG(...) ((void)0)
#endif

typedef struct m_region m_region_t;
typedef struct m_region_block m_region_block_t;
typedef struct m_region_heap m_region_heap_t;

struct m_region {
    void *raw;
    void *base;
    size_t size;
    m_region_t *next;
};

struct m_region_block {
    size_t size;
    m_region_block_t *next;
    m_region_block_t *prev;
    m_region_block_t *free_next;
    m_region_block_t *free_prev;
    m_region_heap_t *owner;
    m_region_t *region;
    uint32_t magic;
    bool allocated;
};

struct m_region_heap {
    m_region_t *regions;
    m_region_block_t *block_head;
    m_region_block_t *block_tail;
    m_region_block_t *free_list;
    size_t region_count;
    size_t total_capacity;
    size_t used_bytes;
    size_t peak_bytes;
    portMUX_TYPE lock;
};

#define MAGNOLIA_ALLOC_BLOCK_HEADER_SIZE                                       MAGNOLIA_ALLOC_ROUND_UP(sizeof(m_region_block_t), MAGNOLIA_ALLOC_ALIGNMENT)

#define MAGNOLIA_ALLOC_MIN_SPLIT                                                 (MAGNOLIA_ALLOC_BLOCK_HEADER_SIZE + MAGNOLIA_ALLOC_ALIGNMENT)

#define MAGNOLIA_ALLOC_MAX_PAYLOAD                                                (MAGNOLIA_ALLOC_REGION_BYTES - MAGNOLIA_ALLOC_BLOCK_HEADER_SIZE)

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(MAGNOLIA_ALLOC_REGION_BYTES > MAGNOLIA_ALLOC_BLOCK_HEADER_SIZE,
               "Region size must exceed block header metadata (increase MAGNOLIA_ALLOC_REGION_SIZE)");
#else
typedef char mag_alloc_region_guard[
    (MAGNOLIA_ALLOC_REGION_BYTES > MAGNOLIA_ALLOC_BLOCK_HEADER_SIZE) ? 1 : -1];
#endif

typedef struct {
    size_t total_regions;
    size_t total_psram_bytes;
    size_t total_allocations;
    size_t total_frees;
} m_alloc_global_stats_internal_t;

static portMUX_TYPE g_alloc_stats_lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
static m_alloc_global_stats_internal_t g_alloc_globals = {0};
static job_ctx_t *g_system_job_ctx;

static inline size_t align_up(size_t size)
{
    return MAGNOLIA_ALLOC_ROUND_UP(size, MAGNOLIA_ALLOC_ALIGNMENT);
}

static inline void *block_data(m_region_block_t *block)
{
    return (uint8_t *)block + MAGNOLIA_ALLOC_BLOCK_HEADER_SIZE;
}

static inline m_region_block_t *data_to_block(void *data)
{
    return (m_region_block_t *)((uint8_t *)data - MAGNOLIA_ALLOC_BLOCK_HEADER_SIZE);
}

static inline size_t block_total_bytes(m_region_block_t *block)
{
    return MAGNOLIA_ALLOC_BLOCK_HEADER_SIZE + block->size;
}

static void global_stats_add_region(size_t bytes)
{
    portENTER_CRITICAL(&g_alloc_stats_lock);
    g_alloc_globals.total_regions += 1;
    g_alloc_globals.total_psram_bytes += bytes;
    portEXIT_CRITICAL(&g_alloc_stats_lock);
}

static void global_stats_report_alloc(void)
{
    portENTER_CRITICAL(&g_alloc_stats_lock);
    g_alloc_globals.total_allocations += 1;
    portEXIT_CRITICAL(&g_alloc_stats_lock);
}

static void global_stats_report_free(void)
{
    portENTER_CRITICAL(&g_alloc_stats_lock);
    g_alloc_globals.total_frees += 1;
    portEXIT_CRITICAL(&g_alloc_stats_lock);
}

static void m_alloc_report_error(job_ctx_t *ctx,
                                 const char *message,
                                 void *related)
{
    job_ctx_t *target = ctx ? ctx : g_system_job_ctx;
    if (target != NULL && target->job_id != NULL) {
        ESP_LOGE(TAG,
                 "job=%p trace_id=%" PRIu64 " alloc error: %s ptr=%p",
                 target->job_id,
                 target->trace_id,
                 message,
                 related);
        m_job_cancel(target->job_id);
    } else {
        ESP_LOGE(TAG, "system alloc error: %s ptr=%p", message, related);
        m_arch_panic(message);
    }
}

static void m_alloc_fallback_free(void *ptr)
{
    MAGNOLIA_ALLOC_DEBUG_LOG("fallback free ptr=%p", ptr);
    m_arch_free(ptr);
}

static void insert_free_block(m_region_heap_t *heap, m_region_block_t *block)
{
    block->free_next = heap->free_list;
    block->free_prev = NULL;
    if (heap->free_list) {
        heap->free_list->free_prev = block;
    }
    heap->free_list = block;
}

static void detach_free_block(m_region_heap_t *heap, m_region_block_t *block)
{
    if (block->free_prev) {
        block->free_prev->free_next = block->free_next;
    }
    if (block->free_next) {
        block->free_next->free_prev = block->free_prev;
    }
    if (heap->free_list == block) {
        heap->free_list = block->free_next;
    }
    block->free_next = NULL;
    block->free_prev = NULL;
}

static m_region_block_t *find_fit_block(m_region_heap_t *heap, size_t required)
{
    m_region_block_t *cursor = heap->free_list;
    while (cursor != NULL) {
        if (cursor->size >= required) {
            return cursor;
        }
        cursor = cursor->free_next;
    }
    return NULL;
}

static void add_region_to_heap(m_region_heap_t *heap, m_region_t *region)
{
    region->next = heap->regions;
    heap->regions = region;
    heap->region_count += 1;
    heap->total_capacity += region->size;
    global_stats_add_region(region->size);
}

static m_region_t *m_region_alloc(void)
{
    void *raw = m_arch_malloc(MAGNOLIA_ALLOC_REGION_BYTES);
    if (raw == NULL) {
        return NULL;
    }

    uintptr_t raw_addr = (uintptr_t)raw;
    uintptr_t aligned = MAGNOLIA_ALLOC_ROUND_UP(raw_addr, MAGNOLIA_ALLOC_ALIGNMENT);
    size_t offset = aligned - raw_addr;
    if (offset >= MAGNOLIA_ALLOC_REGION_BYTES) {
        m_arch_free(raw);
        return NULL;
    }

    size_t usable = MAGNOLIA_ALLOC_REGION_BYTES - offset;
    if (usable <= MAGNOLIA_ALLOC_BLOCK_HEADER_SIZE) {
        m_arch_free(raw);
        return NULL;
    }

    m_region_t *region = pvPortMalloc(sizeof(*region));
    if (region == NULL) {
        m_arch_free(raw);
        return NULL;
    }

    region->raw = raw;
    region->base = (void *)aligned;
    region->size = usable;
    region->next = NULL;
    return region;
}

static m_region_block_t *merge_blocks(m_region_block_t *left,
                                      m_region_block_t *right,
                                      m_region_heap_t *heap)
{
    uint8_t *left_data = block_data(left);
    uint8_t *right_header = (uint8_t *)right;
    uint8_t *left_end = left_data + left->size;
    size_t added = (right_header - left_end) + block_total_bytes(right);
    left->size += added;
    left->next = right->next;
    if (right->next) {
        right->next->prev = left;
    } else {
        heap->block_tail = left;
    }
    return left;
}

static void coalesce_free_block(m_region_heap_t *heap, m_region_block_t *block)
{
    if (block->prev && !block->prev->allocated) {
        detach_free_block(heap, block->prev);
        block = merge_blocks(block->prev, block, heap);
    }
    if (block->next && !block->next->allocated) {
        detach_free_block(heap, block->next);
        merge_blocks(block, block->next, heap);
    }
    insert_free_block(heap, block);
}

static void split_block(m_region_heap_t *heap,
                        m_region_block_t *block,
                        size_t required)
{
    size_t available = block->size;
    if (available < required + MAGNOLIA_ALLOC_MIN_SPLIT) {
        return;
    }

    uint8_t *data = block_data(block);
    uint8_t *split_header = (uint8_t *)align_up((uintptr_t)(data + required));
    uint8_t *block_end = data + available;
    if (split_header + MAGNOLIA_ALLOC_BLOCK_HEADER_SIZE >= block_end) {
        return;
    }

    size_t second_payload = block_end - (split_header + MAGNOLIA_ALLOC_BLOCK_HEADER_SIZE);
    if (second_payload < MAGNOLIA_ALLOC_ALIGNMENT) {
        return;
    }

    block->size = required;
    m_region_block_t *second = (m_region_block_t *)split_header;
    memset(second, 0, MAGNOLIA_ALLOC_BLOCK_HEADER_SIZE);
    second->size = second_payload;
    second->owner = heap;
    second->region = block->region;
    second->magic = MAGNOLIA_ALLOC_MAGIC;
    second->allocated = false;
    second->prev = block;
    second->next = block->next;
    if (block->next) {
        block->next->prev = second;
    } else {
        heap->block_tail = second;
    }
    block->next = second;
    insert_free_block(heap, second);
}

static bool m_region_heap_grow(m_region_heap_t *heap)
{
    if (MAGNOLIA_ALLOC_MAX_REGIONS > 0 && heap->region_count >= MAGNOLIA_ALLOC_MAX_REGIONS) {
        return false;
    }
    if (MAGNOLIA_ALLOC_MAX_JOB_HEAP > 0 &&
        heap->total_capacity + MAGNOLIA_ALLOC_REGION_BYTES > MAGNOLIA_ALLOC_MAX_JOB_HEAP) {
        return false;
    }

    m_region_t *region = m_region_alloc();
    if (region == NULL) {
        return false;
    }

    add_region_to_heap(heap, region);

    m_region_block_t *block = (m_region_block_t *)region->base;
    memset(block, 0, MAGNOLIA_ALLOC_BLOCK_HEADER_SIZE);
    block->size = region->size - MAGNOLIA_ALLOC_BLOCK_HEADER_SIZE;
    block->owner = heap;
    block->region = region;
    block->magic = MAGNOLIA_ALLOC_MAGIC;
    block->allocated = false;
    block->prev = heap->block_tail;
    if (heap->block_tail) {
        heap->block_tail->next = block;
    } else {
        heap->block_head = block;
    }
    heap->block_tail = block;
    insert_free_block(heap, block);
    return true;
}

static void *m_region_heap_alloc(m_region_heap_t *heap, size_t size)
{
    if (heap == NULL || size == 0) {
        return NULL;
    }

    if (size > MAGNOLIA_ALLOC_MAX_PAYLOAD) {
        return NULL;
    }

    size_t required = align_up(size);
    if (required > MAGNOLIA_ALLOC_MAX_PAYLOAD) {
        return NULL;
    }

    portENTER_CRITICAL(&heap->lock);
    m_region_block_t *block = find_fit_block(heap, required);
    if (block == NULL) {
        if (!m_region_heap_grow(heap)) {
            portEXIT_CRITICAL(&heap->lock);
            return NULL;
        }
        block = find_fit_block(heap, required);
        if (block == NULL) {
            portEXIT_CRITICAL(&heap->lock);
            return NULL;
        }
    }

    detach_free_block(heap, block);
    split_block(heap, block, required);
    block->allocated = true;
    heap->used_bytes += block->size;
    if (heap->used_bytes > heap->peak_bytes) {
        heap->peak_bytes = heap->used_bytes;
    }
    global_stats_report_alloc();
    void *result = block_data(block);
    portEXIT_CRITICAL(&heap->lock);
    return result;
}

static void m_region_heap_free_block(m_region_heap_t *heap, m_region_block_t *block)
{
    block->allocated = false;
    heap->used_bytes -= block->size;
    global_stats_report_free();
    coalesce_free_block(heap, block);
}

static void m_region_heap_destroy(m_region_heap_t *heap)
{
    if (heap == NULL) {
        return;
    }

    m_region_t *region = heap->regions;
    while (region != NULL) {
        m_region_t *next = region->next;
        if (region->raw != NULL) {
            m_arch_free(region->raw);
        }
        vPortFree(region);
        region = next;
    }

    vPortFree(heap);
}

static m_region_block_t *m_region_block_from_ptr(void *ptr)
{
    if (ptr == NULL) {
        return NULL;
    }
    m_region_block_t *block = data_to_block(ptr);
    if (block->magic != MAGNOLIA_ALLOC_MAGIC) {
        return NULL;
    }
    return block;
}

static job_ctx_t *m_alloc_effective_ctx(job_ctx_t *ctx)
{
    if (ctx != NULL) {
        return ctx;
    }

    if (g_system_job_ctx == NULL) {
        m_alloc_init();
    }

    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return g_system_job_ctx;
    }

    job_ctx_t *current = jctx_current();
    return current != NULL ? current : g_system_job_ctx;
}

static m_region_heap_t *m_alloc_ensure_heap(job_ctx_t *ctx)
{
    if (ctx == NULL) {
        return NULL;
    }
    portENTER_CRITICAL(&ctx->lock);
    m_region_heap_t *heap = ctx->region_heap;
    if (heap == NULL) {
        heap = pvPortMalloc(sizeof(*heap));
        if (heap != NULL) {
            memset(heap, 0, sizeof(*heap));
            heap->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
            ctx->region_heap = heap;
        }
    }
    portEXIT_CRITICAL(&ctx->lock);
    return heap;
}

void m_alloc_init(void)
{
    if (g_system_job_ctx != NULL) {
        return;
    }
    job_ctx_t *ctx = jctx_create(NULL, NULL);
    if (ctx == NULL) {
        ESP_LOGE(TAG, "failed to create system job context");
        return;
    }
    g_system_job_ctx = ctx;
}

void *malloc(size_t size)
{
    return m_job_alloc(NULL, size);
}

void *calloc(size_t nmemb, size_t size)
{
    return m_job_calloc(NULL, nmemb, size);
}

void *realloc(void *ptr, size_t new_size)
{
    return m_job_realloc(NULL, ptr, new_size);
}

void free(void *ptr)
{
    m_job_free(NULL, ptr);
}

#if CONFIG_LIBC_NEWLIB
void *_malloc_r(struct _reent *r, size_t size)
{
    (void)r;
    return malloc(size);
}

void *_realloc_r(struct _reent *r, void *ptr, size_t size)
{
    (void)r;
    return realloc(ptr, size);
}

void _free_r(struct _reent *r, void *ptr)
{
    (void)r;
    free(ptr);
}

void *_calloc_r(struct _reent *r, size_t nmemb, size_t size)
{
    (void)r;
    return calloc(nmemb, size);
}
#endif

void *m_job_alloc(job_ctx_t *ctx, size_t size)
{
    if (size == 0) {
        return NULL;
    }

    job_ctx_t *target = m_alloc_effective_ctx(ctx);
    if (target == NULL) {
        return NULL;
    }

    m_region_heap_t *heap = m_alloc_ensure_heap(target);
    if (heap == NULL) {
        m_alloc_report_error(target, "failed to build job heap", NULL);
        return NULL;
    }

    void *result = m_region_heap_alloc(heap, size);
    if (result == NULL) {
        m_alloc_report_error(target, "out of memory", NULL);
    }
    else {
        MAGNOLIA_ALLOC_DEBUG_LOG("job=%p alloc size=%zu ptr=%p",
                                 target->job_id,
                                 size,
                                 result);
    }
    return result;
}

void *m_job_calloc(job_ctx_t *ctx, size_t nmemb, size_t size)
{
    if (nmemb == 0 || size == 0) {
        return NULL;
    }
    if (nmemb > SIZE_MAX / size) {
        return NULL;
    }

    job_ctx_t *logger_ctx = m_alloc_effective_ctx(ctx);

    size_t total = nmemb * size;
    void *ptr = m_job_alloc(ctx, total);
    if (ptr != NULL) {
        memset(ptr, 0, total);
        MAGNOLIA_ALLOC_DEBUG_LOG("job=%p calloc size=%zu ptr=%p",
                                 logger_ctx ? logger_ctx->job_id : NULL,
                                 total,
                                 ptr);
    }
    return ptr;
}

void *m_job_realloc(job_ctx_t *ctx, void *ptr, size_t new_size)
{
    if (ptr == NULL) {
        return m_job_alloc(ctx, new_size);
    }
    if (new_size == 0) {
        m_job_free(ctx, ptr);
        return NULL;
    }

    job_ctx_t *target = m_alloc_effective_ctx(ctx);
    if (target == NULL) {
        return NULL;
    }

    m_region_heap_t *heap = target->region_heap;
    if (heap == NULL) {
        m_alloc_report_error(target, "realloc without heap", ptr);
        return NULL;
    }

    m_region_block_t *block = m_region_block_from_ptr(ptr);
    if (block == NULL || block->owner != heap) {
        m_alloc_report_error(target, "realloc pointer mismatch", ptr);
        return NULL;
    }
    if (!block->allocated) {
        m_alloc_report_error(target, "realloc after free", ptr);
        return NULL;
    }

    if (new_size <= block->size) {
        return ptr;
    }

    void *new_ptr = m_region_heap_alloc(heap, new_size);
    if (new_ptr == NULL) {
        return NULL;
    }

    memcpy(new_ptr, ptr, block->size);
    MAGNOLIA_ALLOC_DEBUG_LOG("job=%p realloc old=%p new=%p size=%zu",
                             target->job_id,
                             ptr,
                             new_ptr,
                             new_size);
    m_job_free(target, ptr);
    return new_ptr;
}

void m_job_free(job_ctx_t *ctx, void *ptr)
{
    if (ptr == NULL) {
        return;
    }

    job_ctx_t *target = m_alloc_effective_ctx(ctx);
    if (target == NULL) {
        return;
    }

    m_region_heap_t *heap = target->region_heap;
    bool is_system = (target == g_system_job_ctx);
    if (heap == NULL) {
        if (is_system) {
            m_alloc_fallback_free(ptr);
            return;
        }
        m_alloc_report_error(target, "free without heap", ptr);
        return;
    }

    m_region_block_t *block = m_region_block_from_ptr(ptr);
    if (block == NULL) {
        if (is_system) {
            m_alloc_fallback_free(ptr);
            return;
        }
        m_alloc_report_error(target, "free pointer mismatch", ptr);
        return;
    }
    if (block->owner != heap) {
        m_alloc_report_error(target, "free pointer mismatch", ptr);
        return;
    }

    portENTER_CRITICAL(&heap->lock);
    if (!block->allocated) {
        portEXIT_CRITICAL(&heap->lock);
        m_alloc_report_error(target, "double free", ptr);
        return;
    }
    m_region_heap_free_block(heap, block);
    MAGNOLIA_ALLOC_DEBUG_LOG("job=%p free ptr=%p",
                             target->job_id,
                             ptr);
    portEXIT_CRITICAL(&heap->lock);
}

void m_alloc_teardown_job_ctx(job_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    m_region_heap_t *heap = NULL;
    portENTER_CRITICAL(&ctx->lock);
    heap = ctx->region_heap;
    ctx->region_heap = NULL;
    portEXIT_CRITICAL(&ctx->lock);

    if (heap != NULL) {
        m_region_heap_destroy(heap);
    }
}

void m_alloc_get_job_stats(job_ctx_t *ctx, magnolia_alloc_job_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));

    if (ctx == NULL) {
        return;
    }

    m_region_heap_t *heap = NULL;
    portENTER_CRITICAL(&ctx->lock);
    heap = ctx->region_heap;
    portEXIT_CRITICAL(&ctx->lock);

    if (heap == NULL) {
        return;
    }

    portENTER_CRITICAL(&heap->lock);
    out->used_bytes = heap->used_bytes;
    out->peak_bytes = heap->peak_bytes;
    out->capacity_bytes = heap->total_capacity;
    out->region_count = heap->region_count;
    portEXIT_CRITICAL(&heap->lock);
}

void m_alloc_get_global_stats(magnolia_alloc_global_stats_t *out)
{
    if (out == NULL) {
        return;
    }

    portENTER_CRITICAL(&g_alloc_stats_lock);
    out->total_regions = g_alloc_globals.total_regions;
    out->total_psram_bytes = g_alloc_globals.total_psram_bytes;
    out->total_allocations = g_alloc_globals.total_allocations;
    out->total_frees = g_alloc_globals.total_frees;
    portEXIT_CRITICAL(&g_alloc_stats_lock);
}
