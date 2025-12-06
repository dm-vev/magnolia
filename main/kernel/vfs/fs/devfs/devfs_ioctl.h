#ifndef MAGNOLIA_VFS_DEVFS_IOCTL_H
#define MAGNOLIA_VFS_DEVFS_IOCTL_H

#include "sdkconfig.h"

#include "kernel/vfs/fs/devfs/devfs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEVFS_IOCTL_POLL_MASK 0x10
#define DEVFS_IOCTL_FLUSH     0x11
#define DEVFS_IOCTL_RESET     0x12
#define DEVFS_IOCTL_GET_INFO  0x13
#define DEVFS_IOCTL_DESTROY   0x14

#define DEVFS_IOCTL_PIPE_RESET      0x20
#define DEVFS_IOCTL_PIPE_GET_STATS  0x21

#define DEVFS_IOCTL_TTY_SET_MODE    0x30
#define DEVFS_IOCTL_TTY_GET_MODE    0x31
#define DEVFS_IOCTL_TTY_FLUSH       0x32
#define DEVFS_IOCTL_TTY_SET_ECHO    0x33
#define DEVFS_IOCTL_TTY_GET_ECHO    0x34
#define DEVFS_IOCTL_TTY_SET_CANON   0x35
#define DEVFS_IOCTL_TTY_GET_CANON   0x36

#define DEVFS_IOCTL_PTY_HANGUP      0x40

typedef struct {
    size_t used;
    size_t capacity;
} devfs_pipe_stats_t;

typedef struct {
    bool echo;
    bool canonical;
} devfs_tty_mode_t;

#ifdef __cplusplus
}
#endif

#endif /* MAGNOLIA_VFS_DEVFS_IOCTL_H */
