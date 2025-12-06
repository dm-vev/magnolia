#ifndef MAGNOLIA_DEVFS_SHM_H
#define MAGNOLIA_DEVFS_SHM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEVFS_SHM_IOCTL_BUFFER_INFO 0x01

typedef struct {
    size_t used;
    size_t capacity;
} devfs_shm_buffer_info_t;

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_DEVFS_SHM_H */
