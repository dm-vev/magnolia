#ifndef MAGNOLIA_VFS_DEVFS_FB_H
#define MAGNOLIA_VFS_DEVFS_FB_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEVFS_IOCTL_FB_GET_INFO 0x70
#define DEVFS_IOCTL_FB_BLIT     0x71

#define DEVFS_FB_BLIT_FLAG_NO_REFRESH 0x01

typedef enum {
    DEVFS_FB_FORMAT_RGB565 = 0,
} devfs_fb_format_t;

typedef enum {
    DEVFS_FB_BACKEND_NONE = 0,
    DEVFS_FB_BACKEND_QEMU_RGB = 1,
    DEVFS_FB_BACKEND_SPI_ST7786 = 2,
} devfs_fb_backend_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t size_bytes;
    uint8_t format;
    uint8_t bpp;
    uint8_t backend;
    uint8_t reserved;
} devfs_fb_info_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint8_t format;
    uint8_t flags;
    uint16_t reserved;
    const void *pixels;
} devfs_fb_blit_t;

bool devfs_fb_register(void);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_VFS_DEVFS_FB_H */
