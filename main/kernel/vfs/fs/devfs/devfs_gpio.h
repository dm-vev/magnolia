#ifndef MAGNOLIA_VFS_DEVFS_GPIO_H
#define MAGNOLIA_VFS_DEVFS_GPIO_H

#include <stdint.h>
#include <stdbool.h>

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEVFS_IOCTL_GPIO_CONFIG          0x60
#define DEVFS_IOCTL_GPIO_READ            0x61
#define DEVFS_IOCTL_GPIO_WRITE           0x62
#define DEVFS_IOCTL_GPIO_EDGE_SUBSCRIBE  0x63
#define DEVFS_IOCTL_GPIO_EDGE_UNSUBSCRIBE 0x64

typedef enum {
    DEVFS_GPIO_DIR_IN = 0,
    DEVFS_GPIO_DIR_OUT = 1,
    DEVFS_GPIO_DIR_IN_OUT = 2,
} devfs_gpio_direction_t;

typedef enum {
    DEVFS_GPIO_PULL_NONE = 0,
    DEVFS_GPIO_PULL_UP = 1,
    DEVFS_GPIO_PULL_DOWN = 2,
} devfs_gpio_pull_t;

typedef enum {
    DEVFS_GPIO_DRIVE_PUSH_PULL = 0,
    DEVFS_GPIO_DRIVE_OPEN_DRAIN = 1,
} devfs_gpio_drive_t;

typedef enum {
    DEVFS_GPIO_EDGE_NONE = 0,
    DEVFS_GPIO_EDGE_RISING = 1,
    DEVFS_GPIO_EDGE_FALLING = 2,
    DEVFS_GPIO_EDGE_BOTH = 3,
} devfs_gpio_edge_t;

typedef struct {
    uint64_t pin_mask;
    uint64_t values;
    uint8_t direction;
    uint8_t pull;
    uint8_t drive;
    uint8_t reserved;
} devfs_gpio_config_t;

typedef struct {
    uint64_t pin_mask;
    uint64_t values;
} devfs_gpio_values_t;

typedef struct {
    uint64_t pin_mask;
    uint8_t edge;
    uint8_t reserved[3];
    uint32_t debounce_us;
} devfs_gpio_edge_config_t;

typedef struct {
    uint32_t gpio_num;
    uint8_t edge;
    uint8_t reserved[3];
    uint64_t timestamp_us;
} devfs_gpio_event_t;

bool devfs_gpio_register(void);

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_VFS_DEVFS_GPIO_H */
