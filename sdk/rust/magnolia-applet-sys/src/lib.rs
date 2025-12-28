#![no_std]
#![allow(non_camel_case_types)]

pub use core::ffi::{c_char, c_int, c_long, c_uint, c_ulong, c_void};

pub type size_t = usize;
pub type ssize_t = isize;
pub type off_t = c_long;
pub type mode_t = c_uint;
pub type time_t = c_long;
pub type ino_t = c_ulong;

pub const STDIN_FILENO: c_int = 0;
pub const STDOUT_FILENO: c_int = 1;
pub const STDERR_FILENO: c_int = 2;

pub const SEEK_SET: c_int = 0;
pub const SEEK_CUR: c_int = 1;
pub const SEEK_END: c_int = 2;

pub const F_OK: c_int = 0;
pub const X_OK: c_int = 1;
pub const W_OK: c_int = 2;
pub const R_OK: c_int = 4;

// Values match ESP-IDF's newlib for Xtensa (ESP32-S3).
pub const O_RDONLY: c_int = 0;
pub const O_WRONLY: c_int = 1;
pub const O_RDWR: c_int = 2;
pub const O_APPEND: c_int = 0x0008;
pub const O_CREAT: c_int = 0x0200;
pub const O_TRUNC: c_int = 0x0400;
pub const O_EXCL: c_int = 0x0800;
pub const O_SYNC: c_int = 0x2000;
pub const O_NONBLOCK: c_int = 0x4000;
pub const O_NOCTTY: c_int = 0x8000;
pub const O_CLOEXEC: c_int = 0x40000;

pub const DT_UNKNOWN: u8 = 0;
pub const DT_REG: u8 = 1;
pub const DT_DIR: u8 = 2;

pub const CLOCK_REALTIME: c_int = 0;
// ESP-IDF / newlib on ESP32 uses 4 for CLOCK_MONOTONIC.
pub const CLOCK_MONOTONIC: c_int = 4;

#[repr(C)]
pub struct DIR {
    pub dd_vfs_idx: u16,
    pub dd_rsv: u16,
}

#[repr(C)]
pub struct dirent {
    pub d_ino: ino_t,
    pub d_type: u8,
    pub d_name: [c_char; 256],
}

#[repr(C)]
pub struct timespec {
    pub tv_sec: time_t,
    pub tv_nsec: c_long,
}

#[repr(C)]
pub struct timeval {
    pub tv_sec: time_t,
    pub tv_usec: c_long,
}

extern "C" {
    // errno (Magnolia provides job-local errno via exported __errno()).
    pub fn __errno() -> *mut c_int;

    // Termination (unwinds back to Magnolia ELF loader).
    pub fn exit(status: c_int) -> !;
    pub fn _exit(status: c_int) -> !;
    pub fn abort() -> !;

    // POSIX-ish I/O (Magnolia VFS-backed).
    pub fn open(path: *const c_char, flags: c_int, ...) -> c_int;
    pub fn close(fd: c_int) -> c_int;
    pub fn read(fd: c_int, buf: *mut c_void, count: size_t) -> ssize_t;
    pub fn write(fd: c_int, buf: *const c_void, count: size_t) -> ssize_t;
    pub fn ioctl(fd: c_int, request: c_ulong, ...) -> c_int;
    pub fn lseek(fd: c_int, offset: off_t, whence: c_int) -> off_t;
    pub fn dup(oldfd: c_int) -> c_int;
    pub fn dup2(oldfd: c_int, newfd: c_int) -> c_int;
    pub fn unlink(path: *const c_char) -> c_int;
    pub fn mkdir(path: *const c_char, mode: mode_t) -> c_int;
    pub fn chdir(path: *const c_char) -> c_int;
    pub fn getcwd(buf: *mut c_char, size: size_t) -> *mut c_char;
    pub fn isatty(fd: c_int) -> c_int;
    pub fn access(path: *const c_char, mode: c_int) -> c_int;
    pub fn remove(path: *const c_char) -> c_int;

    pub fn opendir(path: *const c_char) -> *mut DIR;
    pub fn readdir(dirp: *mut DIR) -> *mut dirent;
    pub fn closedir(dirp: *mut DIR) -> c_int;
    pub fn rewinddir(dirp: *mut DIR);

    // Memory management (Magnolia job allocator).
    pub fn malloc(size: size_t) -> *mut c_void;
    pub fn calloc(nmemb: size_t, size: size_t) -> *mut c_void;
    pub fn realloc(ptr: *mut c_void, size: size_t) -> *mut c_void;
    pub fn free(ptr: *mut c_void);

    // Time.
    pub fn clock_gettime(clock_id: c_int, tp: *mut timespec) -> c_int;
    pub fn gettimeofday(tv: *mut timeval, tz: *mut c_void) -> c_int;
    pub fn time(tloc: *mut time_t) -> time_t;
    pub fn sleep(seconds: c_uint) -> c_uint;
    pub fn usleep(usec: c_uint) -> c_int;
    pub fn nanosleep(req: *const timespec, rem: *mut timespec) -> c_int;

    // Errors / diagnostics.
    pub fn strerror(errnum: c_int) -> *const c_char;
    pub fn perror(s: *const c_char);
}

pub const DEVFS_IOCTL_GPIO_CONFIG: c_ulong = 0x60;
pub const DEVFS_IOCTL_GPIO_READ: c_ulong = 0x61;
pub const DEVFS_IOCTL_GPIO_WRITE: c_ulong = 0x62;
pub const DEVFS_IOCTL_GPIO_EDGE_SUBSCRIBE: c_ulong = 0x63;
pub const DEVFS_IOCTL_GPIO_EDGE_UNSUBSCRIBE: c_ulong = 0x64;

pub const DEVFS_GPIO_DIR_IN: u8 = 0;
pub const DEVFS_GPIO_DIR_OUT: u8 = 1;
pub const DEVFS_GPIO_DIR_IN_OUT: u8 = 2;

pub const DEVFS_GPIO_PULL_NONE: u8 = 0;
pub const DEVFS_GPIO_PULL_UP: u8 = 1;
pub const DEVFS_GPIO_PULL_DOWN: u8 = 2;

pub const DEVFS_GPIO_DRIVE_PUSH_PULL: u8 = 0;
pub const DEVFS_GPIO_DRIVE_OPEN_DRAIN: u8 = 1;

pub const DEVFS_GPIO_EDGE_NONE: u8 = 0;
pub const DEVFS_GPIO_EDGE_RISING: u8 = 1;
pub const DEVFS_GPIO_EDGE_FALLING: u8 = 2;
pub const DEVFS_GPIO_EDGE_BOTH: u8 = 3;

#[repr(C)]
pub struct devfs_gpio_config_t {
    pub pin_mask: u64,
    pub values: u64,
    pub direction: u8,
    pub pull: u8,
    pub drive: u8,
    pub reserved: u8,
}

#[repr(C)]
pub struct devfs_gpio_values_t {
    pub pin_mask: u64,
    pub values: u64,
}

#[repr(C)]
pub struct devfs_gpio_edge_config_t {
    pub pin_mask: u64,
    pub edge: u8,
    pub reserved: [u8; 3],
    pub debounce_us: u32,
}

#[repr(C)]
pub struct devfs_gpio_event_t {
    pub gpio_num: u32,
    pub edge: u8,
    pub reserved: [u8; 3],
    pub timestamp_us: u64,
}

pub const DEVFS_IOCTL_FB_GET_INFO: c_ulong = 0x70;
pub const DEVFS_IOCTL_FB_BLIT: c_ulong = 0x71;

pub const DEVFS_FB_BLIT_FLAG_NO_REFRESH: u8 = 0x01;

pub const DEVFS_FB_FORMAT_RGB565: u8 = 0;

pub const DEVFS_FB_BACKEND_NONE: u8 = 0;
pub const DEVFS_FB_BACKEND_QEMU_RGB: u8 = 1;
pub const DEVFS_FB_BACKEND_SPI_ST7786: u8 = 2;

#[repr(C)]
pub struct devfs_fb_info_t {
    pub width: u32,
    pub height: u32,
    pub stride: u32,
    pub size_bytes: u32,
    pub format: u8,
    pub bpp: u8,
    pub backend: u8,
    pub reserved: u8,
}

#[repr(C)]
pub struct devfs_fb_blit_t {
    pub x: u32,
    pub y: u32,
    pub width: u32,
    pub height: u32,
    pub stride: u32,
    pub format: u8,
    pub flags: u8,
    pub reserved: u16,
    pub pixels: *const c_void,
}
