// Raw ABI bindings to Magnolia's exported symbols (resolved by the ELF loader).

pub const ssize_t = isize;
pub const size_t = usize;
pub const off_t = c_long;

pub const DIR = extern struct {
    dd_vfs_idx: u16,
    dd_rsv: u16,
};

pub const Dirent = extern struct {
    d_ino: c_ushort,
    d_type: u8,
    d_name: [256]u8,
};

pub extern fn open(path: [*:0]const u8, flags: c_int, ...) c_int;
pub extern fn close(fd: c_int) c_int;
pub extern fn read(fd: c_int, buf: [*]u8, len: size_t) ssize_t;
pub extern fn write(fd: c_int, buf: [*]const u8, len: size_t) ssize_t;
pub extern fn lseek(fd: c_int, offset: off_t, whence: c_int) off_t;
pub extern fn dup(oldfd: c_int) c_int;
pub extern fn dup2(oldfd: c_int, newfd: c_int) c_int;
pub extern fn unlink(path: [*:0]const u8) c_int;
pub extern fn mkdir(path: [*:0]const u8, mode: c_uint) c_int;
pub extern fn chdir(path: [*:0]const u8) c_int;
pub extern fn getcwd(buf: [*]u8, size: size_t) ?[*:0]u8;
pub extern fn isatty(fd: c_int) c_int;
pub extern fn access(path: [*:0]const u8, mode: c_int) c_int;
pub extern fn remove(path: [*:0]const u8) c_int;

pub extern fn opendir(path: [*:0]const u8) ?*DIR;
pub extern fn readdir(dirp: ?*DIR) ?*Dirent;
pub extern fn closedir(dirp: ?*DIR) c_int;
pub extern fn rewinddir(dirp: ?*DIR) void;

pub extern fn malloc(size: size_t) ?*anyopaque;
pub extern fn calloc(nmemb: size_t, size: size_t) ?*anyopaque;
pub extern fn realloc(ptr: ?*anyopaque, size: size_t) ?*anyopaque;
pub extern fn free(ptr: ?*anyopaque) void;

pub extern fn exit(status: c_int) noreturn;
pub extern fn _exit(status: c_int) noreturn;
pub extern fn abort() noreturn;

pub extern fn printf(fmt: [*:0]const u8, ...) c_int;
pub extern fn snprintf(buf: [*]u8, len: size_t, fmt: [*:0]const u8, ...) c_int;
pub extern fn vsnprintf(buf: [*]u8, len: size_t, fmt: [*:0]const u8, ap: *anyopaque) c_int;
pub extern fn perror(s: [*:0]const u8) void;
pub extern fn strerror(err: c_int) [*:0]const u8;

