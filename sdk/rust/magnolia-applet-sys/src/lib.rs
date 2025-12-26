#![no_std]
#![allow(non_camel_case_types)]

pub use core::ffi::{c_char, c_int, c_long, c_uint, c_void};

pub type size_t = usize;
pub type ssize_t = isize;
pub type off_t = c_long;
pub type mode_t = c_uint;

pub const STDIN_FILENO: c_int = 0;
pub const STDOUT_FILENO: c_int = 1;
pub const STDERR_FILENO: c_int = 2;

pub const SEEK_SET: c_int = 0;
pub const SEEK_CUR: c_int = 1;
pub const SEEK_END: c_int = 2;

// Values match ESP-IDF's newlib for Xtensa (ESP32-S3).
pub const O_RDONLY: c_int = 0;
pub const O_WRONLY: c_int = 1;
pub const O_RDWR: c_int = 2;
pub const O_APPEND: c_int = 0x0008;
pub const O_CREAT: c_int = 0x0200;
pub const O_TRUNC: c_int = 0x0400;
pub const O_EXCL: c_int = 0x0800;
pub const O_NONBLOCK: c_int = 0x4000;
pub const O_CLOEXEC: c_int = 0x40000;

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
    pub fn lseek(fd: c_int, offset: off_t, whence: c_int) -> off_t;
    pub fn unlink(path: *const c_char) -> c_int;
    pub fn mkdir(path: *const c_char, mode: mode_t) -> c_int;
    pub fn chdir(path: *const c_char) -> c_int;
    pub fn getcwd(buf: *mut c_char, size: size_t) -> *mut c_char;

    // Memory management (Magnolia job allocator).
    pub fn malloc(size: size_t) -> *mut c_void;
    pub fn calloc(nmemb: size_t, size: size_t) -> *mut c_void;
    pub fn realloc(ptr: *mut c_void, size: size_t) -> *mut c_void;
    pub fn free(ptr: *mut c_void);

    // Time.
    pub fn sleep(seconds: c_uint) -> c_uint;
    pub fn usleep(usec: c_uint) -> c_int;

    // Errors / diagnostics.
    pub fn strerror(errnum: c_int) -> *const c_char;
}
