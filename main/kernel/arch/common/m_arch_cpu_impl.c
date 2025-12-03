#include "kernel/arch/m_arch.h"

#include <stdint.h>

#include "sdkconfig.h"

#include "esp_attr.h"
#include "esp_err.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

struct m_arch_irq_handle {
    intr_handle_t intr_handle;
    m_arch_irq_handler_t handler;
    void *arg;
};

#ifndef M_ARCH_CPU_IMPL_WFI_INSTR
#define M_ARCH_CPU_IMPL_WFI_INSTR() __asm__ volatile ("nop")
#endif

#ifndef M_ARCH_CPU_IMPL_WFE_INSTR
#define M_ARCH_CPU_IMPL_WFE_INSTR() M_ARCH_CPU_IMPL_WFI_INSTR()
#endif

static void IRAM_ATTR m_arch_irq_dispatch(void *ctx_arg)
{
    struct m_arch_irq_handle *ctx = (struct m_arch_irq_handle *)ctx_arg;
    if (ctx == NULL || ctx->handler == NULL) {
        return;
    }

    ctx->handler(ctx->arg);
}

void m_arch_init_cores(void)
{
    (void)portNUM_PROCESSORS;
}

int m_arch_core_id(void)
{
    return xPortGetCoreID();
}

int m_arch_core_count(void)
{
    return portNUM_PROCESSORS;
}

void m_arch_disable_interrupts(void)
{
    portDISABLE_INTERRUPTS();
}

void m_arch_enable_interrupts(void)
{
    portENABLE_INTERRUPTS();
}

bool m_arch_is_irq_enabled(void)
{
    return true;
}

void m_arch_halt(void)
{
    for (;;) {
        m_arch_wfi();
    }
}

void m_arch_panic(const char *message)
{
    if (message) {
        ESP_LOGE("m_arch", "Kernel panic: %s", message);
    } else {
        ESP_LOGE("m_arch", "Kernel panic triggered without message");
    }
    esp_system_abort(message ? message : "Kernel panic");
}

void m_arch_yield(void)
{
    taskYIELD();
}

void m_arch_switch_context(void *current, void *next)
{
    (void)current;
    (void)next;
    taskYIELD();
}

void m_arch_idle(void)
{
    vTaskDelay(pdMS_TO_TICKS(1));
}

void m_arch_wfi(void)
{
    M_ARCH_CPU_IMPL_WFI_INSTR();
}

void m_arch_wfe(void)
{
    M_ARCH_CPU_IMPL_WFE_INSTR();
}

m_arch_irq_handle_t *m_arch_irq_attach_handler(int irq,
                                               m_arch_irq_handler_t handler,
                                               void *arg)
{
    if (handler == NULL) {
        return NULL;
    }

    struct m_arch_irq_handle *ctx = pvPortMalloc(sizeof(*ctx));
    if (ctx == NULL) {
        return NULL;
    }

    ctx->handler = handler;
    ctx->arg = arg;

    esp_err_t err = esp_intr_alloc(irq,
                                   ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_IRAM,
                                   m_arch_irq_dispatch,
                                   ctx,
                                   &ctx->intr_handle);
    if (err != ESP_OK) {
        vPortFree(ctx);
        return NULL;
    }

    return ctx;
}

void m_arch_irq_detach_handler(m_arch_irq_handle_t *handle)
{
    if (handle == NULL) {
        return;
    }

    esp_intr_free(handle->intr_handle);
    vPortFree(handle);
}

void m_arch_irq_ack(m_arch_irq_handle_t *handle)
{
    if (handle == NULL) {
        return;
    }

    esp_intr_disable(handle->intr_handle);
    esp_intr_enable(handle->intr_handle);
}

void m_arch_irq_trigger(m_arch_irq_handle_t *handle)
{
    if (handle == NULL) {
        return;
    }

    esp_intr_enable(handle->intr_handle);
}
