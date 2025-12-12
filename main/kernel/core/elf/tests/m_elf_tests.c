/*
 * ELF loader selftests for Magnolia kernel.
 * Mirrors style of other kernel selftests.
 */

#if CONFIG_MAGNOLIA_ELF_ENABLED && CONFIG_MAGNOLIA_ELF_SELFTESTS

#include <string.h>
#include <sys/errno.h>

#include "esp_log.h"

#include "kernel/core/elf/m_elf_loader.h"
#include "kernel/core/elf/tests/m_elf_tests.h"

static const char *TAG = "elf_tests";

#define ELF_TEST_ASSERT(cond, label, fmt, ...) \
    do { \
        if (!(cond)) { \
            ESP_LOGE(TAG, "FAIL: " fmt, ##__VA_ARGS__); \
            goto label; \
        } \
    } while (0)

#if CONFIG_IDF_TARGET_ARCH_XTENSA
extern const uint8_t test_elf_start[] asm("_binary_kernel_test_xtensa_elf_start") __attribute__((weak));
extern const uint8_t test_elf_end[]   asm("_binary_kernel_test_xtensa_elf_end") __attribute__((weak));
#elif CONFIG_IDF_TARGET_ARCH_RISCV
extern const uint8_t test_elf_start[] asm("_binary_kernel_test_riscv_elf_start") __attribute__((weak));
extern const uint8_t test_elf_end[]   asm("_binary_kernel_test_riscv_elf_end") __attribute__((weak));
#else
static const uint8_t *test_elf_start;
static const uint8_t *test_elf_end;
#endif

static bool test_invalid_magic(void)
{
    uint8_t buf[8] = {0};
    m_elf_t elf;
    m_elf_init(&elf, NULL);
    int ret = m_elf_relocate(&elf, buf, sizeof(buf));
    m_elf_deinit(&elf);
    return (ret == -EINVAL || ret == -ENOTSUP);
}

static bool test_run_embedded(void)
{
    if (!test_elf_start || !test_elf_end || test_elf_end <= test_elf_start) {
        ESP_LOGW(TAG, "No embedded test ELF found, skipping run test");
        return true;
    }

    size_t len = (size_t)(test_elf_end - test_elf_start);
    int rc = 0;
    int ret = m_elf_run_buffer(test_elf_start, len, 0, NULL, &rc);
    if (ret < 0) {
        ESP_LOGE(TAG, "m_elf_run_buffer failed errno=%d", ret);
        return false;
    }
    ESP_LOGI(TAG, "embedded ELF rc=%d", rc);
    return (rc >= 0);
}

void m_elf_selftests_run(void)
{
    bool ok = true;
    ESP_LOGI(TAG, "ELF selftests start");

    ok &= test_invalid_magic();
    ELF_TEST_ASSERT(ok, done, "invalid magic test");

    ok &= test_run_embedded();
    ELF_TEST_ASSERT(ok, done, "embedded ELF run");

done:
    ESP_LOGI(TAG, "ELF selftests %s", ok ? "PASS" : "FAIL");
}

#endif /* CONFIG_MAGNOLIA_ELF_ENABLED && CONFIG_MAGNOLIA_ELF_SELFTESTS */

