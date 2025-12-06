#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

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

#define LITTLEFS_BLOCK_SIZE CONFIG_MAGNOLIA_LITTLEFS_BLOCK_SIZE
#define LITTLEFS_BLOCK_COUNT CONFIG_MAGNOLIA_LITTLEFS_BLOCK_COUNT
#define LITTLEFS_STORAGE_BYTES (LITTLEFS_BLOCK_SIZE * LITTLEFS_BLOCK_COUNT)

static uint8_t g_littlefs_storage[LITTLEFS_STORAGE_BYTES]
        __attribute__((aligned(4)));
static portMUX_TYPE g_littlefs_lock =
        (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;

static bool
littlefs_backend_validate(lfs_block_t block,
                           lfs_off_t off,
                           lfs_size_t size)
{
    size_t offset = (size_t)block * LITTLEFS_BLOCK_SIZE + (size_t)off;
    if (offset + size > LITTLEFS_STORAGE_BYTES) {
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
    (void)c;
    if (!littlefs_backend_validate(block, off, size)) {
        return LFS_ERR_IO;
    }

    size_t offset = (size_t)block * LITTLEFS_BLOCK_SIZE + (size_t)off;
    portENTER_CRITICAL(&g_littlefs_lock);
    memcpy(buffer, &g_littlefs_storage[offset], size);
    portEXIT_CRITICAL(&g_littlefs_lock);
    return 0;
}

int littlefs_backend_prog(const struct lfs_config *c,
                          lfs_block_t block,
                          lfs_off_t off,
                          const void *buffer,
                          lfs_size_t size)
{
    (void)c;
    if (!littlefs_backend_validate(block, off, size)) {
        return LFS_ERR_IO;
    }

    size_t offset = (size_t)block * LITTLEFS_BLOCK_SIZE + (size_t)off;
    portENTER_CRITICAL(&g_littlefs_lock);
    memcpy(&g_littlefs_storage[offset], buffer, size);
    portEXIT_CRITICAL(&g_littlefs_lock);
    return 0;
}

int littlefs_backend_erase(const struct lfs_config *c, lfs_block_t block)
{
    (void)c;
    size_t count = CONFIG_MAGNOLIA_LITTLEFS_ERASE_BLOCKS;
    for (size_t i = 0; i < count; ++i) {
        lfs_block_t target = block + i;
        if (target >= LITTLEFS_BLOCK_COUNT) {
            return LFS_ERR_IO;
        }

        size_t offset = (size_t)target * LITTLEFS_BLOCK_SIZE;
        portENTER_CRITICAL(&g_littlefs_lock);
        memset(&g_littlefs_storage[offset], 0xFF, LITTLEFS_BLOCK_SIZE);
        portEXIT_CRITICAL(&g_littlefs_lock);
    }
    return 0;
}

int littlefs_backend_sync(const struct lfs_config *c)
{
    (void)c;
    return 0;
}

#endif /* CONFIG_MAGNOLIA_LITTLEFS_ENABLED */
