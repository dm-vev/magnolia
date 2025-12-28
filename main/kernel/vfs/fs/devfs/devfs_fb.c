#include "kernel/vfs/fs/devfs/devfs_fb.h"

#include <string.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"

#if CONFIG_MAGNOLIA_FB_BACKEND_QEMU
#include "esp_lcd_qemu_rgb.h"
#endif

#if CONFIG_MAGNOLIA_FB_BACKEND_SPI_ST7786
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_st7789.h"
#endif

#include "kernel/core/vfs/m_vfs.h"
#include "kernel/vfs/fs/devfs/devfs.h"

static const char *const FB_TAG = "devfs_fb";

typedef struct {
    esp_lcd_panel_handle_t panel;
#if CONFIG_MAGNOLIA_FB_BACKEND_QEMU
    void *qemu_fb;
#endif
#if CONFIG_MAGNOLIA_FB_BACKEND_SPI_ST7786
    esp_lcd_panel_io_handle_t io;
    spi_host_device_t host;
#endif
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t size_bytes;
    devfs_fb_format_t format;
    uint8_t bpp;
    devfs_fb_backend_t backend;
    bool ready;
} devfs_fb_device_t;

static devfs_fb_device_t g_devfs_fb;

static inline uint32_t devfs_fb_bpp_bytes(const devfs_fb_device_t *device)
{
    return device->bpp / 8U;
}

static bool devfs_fb_region_valid(const devfs_fb_device_t *device, uint32_t x, uint32_t y,
                                  uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0) {
        return false;
    }
    if (x >= device->width || y >= device->height) {
        return false;
    }
    if (x + width > device->width || y + height > device->height) {
        return false;
    }
    return true;
}

#if CONFIG_MAGNOLIA_FB_BACKEND_QEMU
static m_vfs_error_t devfs_fb_qemu_blit(devfs_fb_device_t *device,
                                        const devfs_fb_blit_t *blit)
{
    if (device->qemu_fb == NULL) {
        return M_VFS_ERR_IO;
    }

    const uint32_t bytes_per_pixel = devfs_fb_bpp_bytes(device);
    const uint32_t src_stride = (blit->stride == 0) ? blit->width : blit->stride;
    const uint8_t *src = (const uint8_t *)blit->pixels;
    uint8_t *dst = (uint8_t *)device->qemu_fb;
    uint8_t *dst_row = dst + (blit->y * device->stride + blit->x) * bytes_per_pixel;
    const uint32_t row_bytes = blit->width * bytes_per_pixel;
    const uint32_t src_row_bytes = src_stride * bytes_per_pixel;

    for (uint32_t row = 0; row < blit->height; ++row) {
        memcpy(dst_row, src, row_bytes);
        src += src_row_bytes;
        dst_row += device->stride * bytes_per_pixel;
    }

    if ((blit->flags & DEVFS_FB_BLIT_FLAG_NO_REFRESH) == 0) {
        esp_lcd_rgb_qemu_refresh(device->panel);
    }
    return M_VFS_ERR_OK;
}
#endif

static m_vfs_error_t devfs_fb_panel_blit(devfs_fb_device_t *device,
                                         const devfs_fb_blit_t *blit)
{
    const uint32_t src_stride = (blit->stride == 0) ? blit->width : blit->stride;
    const uint32_t bytes_per_pixel = devfs_fb_bpp_bytes(device);
    const uint8_t *src = (const uint8_t *)blit->pixels;

    if (src_stride == blit->width) {
        esp_err_t err = esp_lcd_panel_draw_bitmap(device->panel,
                                                  (int)blit->x,
                                                  (int)blit->y,
                                                  (int)(blit->x + blit->width),
                                                  (int)(blit->y + blit->height),
                                                  src);
        return (err == ESP_OK) ? M_VFS_ERR_OK : M_VFS_ERR_IO;
    }

    for (uint32_t row = 0; row < blit->height; ++row) {
        const uint8_t *row_ptr = src + row * src_stride * bytes_per_pixel;
        esp_err_t err = esp_lcd_panel_draw_bitmap(device->panel,
                                                  (int)blit->x,
                                                  (int)(blit->y + row),
                                                  (int)(blit->x + blit->width),
                                                  (int)(blit->y + row + 1),
                                                  row_ptr);
        if (err != ESP_OK) {
            return M_VFS_ERR_IO;
        }
    }

    return M_VFS_ERR_OK;
}

static m_vfs_error_t devfs_fb_write(void *private_data,
                                    const void *buffer,
                                    size_t size,
                                    size_t *written)
{
    devfs_fb_device_t *device = (devfs_fb_device_t *)private_data;
    if (device == NULL || buffer == NULL || written == NULL) {
        return M_VFS_ERR_INVALID_PARAM;
    }
    if (!device->ready) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    const uint32_t frame_bytes = device->size_bytes;
    if (size != frame_bytes) {
        return M_VFS_ERR_NOT_SUPPORTED;
    }

    devfs_fb_blit_t blit = {
        .x = 0,
        .y = 0,
        .width = device->width,
        .height = device->height,
        .stride = device->stride,
        .format = device->format,
        .flags = 0,
        .reserved = 0,
        .pixels = buffer,
    };

    m_vfs_error_t err = M_VFS_ERR_NOT_SUPPORTED;
#if CONFIG_MAGNOLIA_FB_BACKEND_QEMU
    if (device->backend == DEVFS_FB_BACKEND_QEMU_RGB) {
        err = devfs_fb_qemu_blit(device, &blit);
    }
#endif
#if CONFIG_MAGNOLIA_FB_BACKEND_SPI_ST7786
    if (device->backend == DEVFS_FB_BACKEND_SPI_ST7786) {
        err = devfs_fb_panel_blit(device, &blit);
    }
#endif

    if (err == M_VFS_ERR_OK) {
        *written = size;
    }
    return err;
}

static m_vfs_error_t devfs_fb_flush(void *private_data)
{
    devfs_fb_device_t *device = (devfs_fb_device_t *)private_data;
    if (device == NULL || !device->ready) {
        return M_VFS_ERR_INVALID_PARAM;
    }
#if CONFIG_MAGNOLIA_FB_BACKEND_QEMU
    if (device->backend == DEVFS_FB_BACKEND_QEMU_RGB) {
        esp_lcd_rgb_qemu_refresh(device->panel);
    }
#endif
    return M_VFS_ERR_OK;
}

static m_vfs_error_t devfs_fb_ioctl(void *private_data,
                                    unsigned long request,
                                    void *arg)
{
    devfs_fb_device_t *device = (devfs_fb_device_t *)private_data;
    if (device == NULL || !device->ready) {
        return M_VFS_ERR_INVALID_PARAM;
    }

    switch (request) {
    case DEVFS_IOCTL_FB_GET_INFO: {
        devfs_fb_info_t *info = (devfs_fb_info_t *)arg;
        if (info == NULL) {
            return M_VFS_ERR_INVALID_PARAM;
        }
        info->width = device->width;
        info->height = device->height;
        info->stride = device->stride;
        info->size_bytes = device->size_bytes;
        info->format = device->format;
        info->bpp = device->bpp;
        info->backend = device->backend;
        info->reserved = 0;
        return M_VFS_ERR_OK;
    }
    case DEVFS_IOCTL_FB_BLIT: {
        devfs_fb_blit_t *blit = (devfs_fb_blit_t *)arg;
        if (blit == NULL || blit->pixels == NULL) {
            return M_VFS_ERR_INVALID_PARAM;
        }
        if (blit->format != device->format) {
            return M_VFS_ERR_NOT_SUPPORTED;
        }
        if (!devfs_fb_region_valid(device, blit->x, blit->y, blit->width, blit->height)) {
            return M_VFS_ERR_INVALID_PARAM;
        }

#if CONFIG_MAGNOLIA_FB_BACKEND_QEMU
        if (device->backend == DEVFS_FB_BACKEND_QEMU_RGB) {
            return devfs_fb_qemu_blit(device, blit);
        }
#endif
#if CONFIG_MAGNOLIA_FB_BACKEND_SPI_ST7786
        if (device->backend == DEVFS_FB_BACKEND_SPI_ST7786) {
            return devfs_fb_panel_blit(device, blit);
        }
#endif
        return M_VFS_ERR_NOT_SUPPORTED;
    }
    default:
        return M_VFS_ERR_NOT_SUPPORTED;
    }
}

static uint32_t devfs_fb_poll(void *private_data)
{
    (void)private_data;
    return DEVFS_EVENT_WRITABLE;
}

#if CONFIG_MAGNOLIA_FB_BACKEND_QEMU
static bool devfs_fb_init_qemu(devfs_fb_device_t *device)
{
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_rgb_qemu_config_t cfg = {
        .width = CONFIG_MAGNOLIA_FB_WIDTH,
        .height = CONFIG_MAGNOLIA_FB_HEIGHT,
        .bpp = RGB_QEMU_BPP_16,
    };

    esp_err_t err = esp_lcd_new_rgb_qemu(&cfg, &panel);
    if (err != ESP_OK) {
        ESP_LOGW(FB_TAG, "qemu panel init failed err=%s", esp_err_to_name(err));
        return false;
    }

    void *fb = NULL;
    err = esp_lcd_rgb_qemu_get_frame_buffer(panel, &fb);
    if (err != ESP_OK || fb == NULL) {
        ESP_LOGW(FB_TAG, "qemu framebuffer unavailable err=%s", esp_err_to_name(err));
        return false;
    }

    device->panel = panel;
    device->qemu_fb = fb;
    device->backend = DEVFS_FB_BACKEND_QEMU_RGB;
    return true;
}
#endif

#if CONFIG_MAGNOLIA_FB_BACKEND_SPI_ST7786
static bool devfs_fb_spi_pins_valid(void)
{
    if (CONFIG_MAGNOLIA_FB_SPI_MOSI < 0 || CONFIG_MAGNOLIA_FB_SPI_SCLK < 0 ||
            CONFIG_MAGNOLIA_FB_SPI_DC < 0) {
        return false;
    }
    return true;
}

static bool devfs_fb_init_spi_st7786(devfs_fb_device_t *device)
{
    if (!devfs_fb_spi_pins_valid()) {
        ESP_LOGE(FB_TAG, "SPI pins are not configured");
        return false;
    }

    spi_host_device_t host = (spi_host_device_t)CONFIG_MAGNOLIA_FB_SPI_HOST;
    spi_bus_config_t buscfg = {
        .sclk_io_num = CONFIG_MAGNOLIA_FB_SPI_SCLK,
        .mosi_io_num = CONFIG_MAGNOLIA_FB_SPI_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)(CONFIG_MAGNOLIA_FB_WIDTH * CONFIG_MAGNOLIA_FB_HEIGHT * 2 + 8),
    };

    esp_err_t err = spi_bus_initialize(host, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(FB_TAG, "spi_bus_initialize failed err=%s", esp_err_to_name(err));
        return false;
    }

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = CONFIG_MAGNOLIA_FB_SPI_CS,
        .dc_gpio_num = CONFIG_MAGNOLIA_FB_SPI_DC,
        .spi_mode = 0,
        .pclk_hz = CONFIG_MAGNOLIA_FB_SPI_PCLK_HZ,
        .trans_queue_depth = CONFIG_MAGNOLIA_FB_SPI_QUEUE_DEPTH,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };

    err = esp_lcd_new_panel_io_spi(host, &io_cfg, &io);
    if (err != ESP_OK) {
        ESP_LOGE(FB_TAG, "panel io init failed err=%s", esp_err_to_name(err));
        return false;
    }

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = CONFIG_MAGNOLIA_FB_SPI_RST,
        .rgb_ele_order = CONFIG_MAGNOLIA_FB_BGR ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .flags = {
            .reset_active_high = CONFIG_MAGNOLIA_FB_RESET_ACTIVE_HIGH,
        },
    };

    err = esp_lcd_new_panel_st7789(io, &panel_cfg, &panel);
    if (err != ESP_OK) {
        ESP_LOGE(FB_TAG, "panel init failed err=%s", esp_err_to_name(err));
        return false;
    }

    err = esp_lcd_panel_reset(panel);
    if (err != ESP_OK) {
        ESP_LOGW(FB_TAG, "panel reset failed err=%s", esp_err_to_name(err));
    }
    err = esp_lcd_panel_init(panel);
    if (err != ESP_OK) {
        ESP_LOGE(FB_TAG, "panel init failed err=%s", esp_err_to_name(err));
        return false;
    }

    if (CONFIG_MAGNOLIA_FB_INVERT_COLOR) {
        esp_lcd_panel_invert_color(panel, true);
    }

    if (CONFIG_MAGNOLIA_FB_SWAP_XY) {
        esp_lcd_panel_swap_xy(panel, true);
    }
    if (CONFIG_MAGNOLIA_FB_MIRROR_X || CONFIG_MAGNOLIA_FB_MIRROR_Y) {
        esp_lcd_panel_mirror(panel, CONFIG_MAGNOLIA_FB_MIRROR_X, CONFIG_MAGNOLIA_FB_MIRROR_Y);
    }

    if (CONFIG_MAGNOLIA_FB_X_OFFSET != 0 || CONFIG_MAGNOLIA_FB_Y_OFFSET != 0) {
        esp_lcd_panel_set_gap(panel, CONFIG_MAGNOLIA_FB_X_OFFSET, CONFIG_MAGNOLIA_FB_Y_OFFSET);
    }

    esp_lcd_panel_disp_on_off(panel, true);

    if (CONFIG_MAGNOLIA_FB_BACKLIGHT_GPIO >= 0) {
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << CONFIG_MAGNOLIA_FB_BACKLIGHT_GPIO,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        gpio_set_level(CONFIG_MAGNOLIA_FB_BACKLIGHT_GPIO, 1);
    }

    device->panel = panel;
    device->io = io;
    device->host = host;
    device->backend = DEVFS_FB_BACKEND_SPI_ST7786;
    return true;
}
#endif

static bool devfs_fb_init_backend(devfs_fb_device_t *device)
{
    if (device == NULL) {
        return false;
    }

    device->width = CONFIG_MAGNOLIA_FB_WIDTH;
    device->height = CONFIG_MAGNOLIA_FB_HEIGHT;
    device->stride = CONFIG_MAGNOLIA_FB_WIDTH;
    device->bpp = 16;
    device->format = DEVFS_FB_FORMAT_RGB565;
    device->size_bytes = device->width * device->height * devfs_fb_bpp_bytes(device);
    device->backend = DEVFS_FB_BACKEND_NONE;

#if CONFIG_MAGNOLIA_FB_BACKEND_QEMU
    if (devfs_fb_init_qemu(device)) {
        return true;
    }
#endif
#if CONFIG_MAGNOLIA_FB_BACKEND_SPI_ST7786
    if (devfs_fb_init_spi_st7786(device)) {
        return true;
    }
#endif

    return false;
}

static const devfs_ops_t s_devfs_fb_ops = {
    .write = devfs_fb_write,
    .ioctl = devfs_fb_ioctl,
    .flush = devfs_fb_flush,
    .poll = devfs_fb_poll,
};

bool devfs_fb_register(void)
{
    devfs_fb_device_t *device = &g_devfs_fb;
    memset(device, 0, sizeof(*device));

    if (!devfs_fb_init_backend(device)) {
        ESP_LOGW(FB_TAG, "framebuffer backend unavailable");
        return false;
    }

    device->ready = true;

    m_vfs_error_t err = devfs_register("/dev/fb0", &s_devfs_fb_ops, device);
    if (err != M_VFS_ERR_OK) {
        ESP_LOGE(FB_TAG, "register /dev/fb0 failed err=%d", err);
        device->ready = false;
        return false;
    }

    ESP_LOGI(FB_TAG, "registered /dev/fb0 (%ux%u)", device->width, device->height);
    return true;
}
