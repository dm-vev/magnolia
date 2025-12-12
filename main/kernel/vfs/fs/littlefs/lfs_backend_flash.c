#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"
#include "kernel/vfs/fs/littlefs/lfs_backend_flash.h"

#if !CONFIG_MAGNOLIA_LITTLEFS_ENABLED

int littlefs_backend_read(const struct lfs_config *c,
                          lfs_block_t block,
                          lfs_off_t off,
                          void *buffer,
                          lfs_size_t size)
{
    (void)c;
    (void)block;
    (void)off;
    (void)buffer;
    (void)size;
    return -1;
}

int littlefs_backend_prog(const struct lfs_config *c,
                          lfs_block_t block,
                          lfs_off_t off,
                          const void *buffer,
                          lfs_size_t size)
{
    (void)c;
    (void)block;
    (void)off;
    (void)buffer;
    (void)size;
    return -1;
}

int littlefs_backend_erase(const struct lfs_config *c, lfs_block_t block)
{
    (void)c;
    (void)block;
    return -1;
}

int littlefs_backend_sync(const struct lfs_config *c)
{
    (void)c;
    return 0;
}

#else

#include <inttypes.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_rom_sys.h"

#ifndef LFS_ERR_ROFS
#define LFS_ERR_ROFS LFS_ERR_IO
#endif

static const char *TAG = "littlefs_backend";

static inline littlefs_flash_ctx_t *
littlefs_ctx(const struct lfs_config *c)
{
    return (littlefs_flash_ctx_t *)c->context;
}

static bool
littlefs_backend_validate(const struct lfs_config *c,
                          lfs_block_t block,
                          lfs_off_t off,
                          lfs_size_t size)
{
    littlefs_flash_ctx_t *ctx = littlefs_ctx(c);
    if (ctx == NULL || ctx->partition == NULL) {
        return false;
    }

    uint32_t offset = (uint32_t)block * ctx->block_size + (uint32_t)off;
    if (offset + (uint32_t)size > ctx->size) {
        return false;
    }
    return true;
}

int littlefs_backend_read(const struct lfs_config *c,
                          lfs_block_t block,
                          lfs_off_t off,
                          void *buffer,
                          lfs_size_t size)
{
    if (!littlefs_backend_validate(c, block, off, size)) {
        return LFS_ERR_IO;
    }

    littlefs_flash_ctx_t *ctx = littlefs_ctx(c);
    uint32_t addr = ctx->base + (uint32_t)block * ctx->block_size + (uint32_t)off;
#if CONFIG_MAGNOLIA_LITTLEFS_TEST_LOG_IO
    esp_rom_printf("[LFS-TEST] read block=%"PRIu32" off=%"PRIu32" size=%"PRIu32" addr=0x%08"PRIx32"\n",
                   (uint32_t)block, (uint32_t)off, (uint32_t)size, addr);
#endif
    esp_err_t err = esp_partition_read(ctx->partition, addr, buffer, size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "read failed addr=0x%08"PRIx32" size=%"PRIu32" err=%d",
                 addr, (uint32_t)size, (int)err);
        return LFS_ERR_IO;
    }
    return 0;
}

int littlefs_backend_prog(const struct lfs_config *c,
                          lfs_block_t block,
                          lfs_off_t off,
                          const void *buffer,
                          lfs_size_t size)
{
    if (!littlefs_backend_validate(c, block, off, size)) {
        return LFS_ERR_IO;
    }

    littlefs_flash_ctx_t *ctx = littlefs_ctx(c);
    if (ctx->read_only) {
        return LFS_ERR_ROFS;
    }

    uint32_t addr = ctx->base + (uint32_t)block * ctx->block_size + (uint32_t)off;
#if CONFIG_MAGNOLIA_LITTLEFS_TEST_LOG_IO
    esp_rom_printf("[LFS-TEST] prog block=%"PRIu32" off=%"PRIu32" size=%"PRIu32" addr=0x%08"PRIx32"\n",
                   (uint32_t)block, (uint32_t)off, (uint32_t)size, addr);
#endif
    esp_err_t err = esp_partition_write(ctx->partition, addr, buffer, size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "prog failed addr=0x%08"PRIx32" size=%"PRIu32" err=%d",
                 addr, (uint32_t)size, (int)err);
        return LFS_ERR_IO;
    }
    return 0;
}

int littlefs_backend_erase(const struct lfs_config *c, lfs_block_t block)
{
    littlefs_flash_ctx_t *ctx = littlefs_ctx(c);
    if (ctx == NULL || ctx->partition == NULL) {
        return LFS_ERR_IO;
    }
    if (ctx->read_only) {
        return LFS_ERR_ROFS;
    }

    if (!littlefs_backend_validate(c, block, 0, ctx->block_size)) {
        return LFS_ERR_IO;
    }

    uint32_t addr = ctx->base + (uint32_t)block * ctx->block_size;
#if CONFIG_MAGNOLIA_LITTLEFS_TEST_LOG_IO
    esp_rom_printf("[LFS-TEST] erase block=%"PRIu32" addr=0x%08"PRIx32" size=%"PRIu32"\n",
                   (uint32_t)block, addr, ctx->block_size);
#endif
    esp_err_t err = esp_partition_erase_range(ctx->partition, addr, ctx->block_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "erase failed addr=0x%08"PRIx32" size=%"PRIu32" err=%d",
                 addr, ctx->block_size, (int)err);
        return LFS_ERR_IO;
    }
    return 0;
}

int littlefs_backend_sync(const struct lfs_config *c)
{
    (void)c;
    return 0;
}

#endif /* CONFIG_MAGNOLIA_LITTLEFS_ENABLED */
