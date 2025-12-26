const sys = @import("sys.zig");
const constants = @import("constants.zig");
const errno = @import("errno.zig");

pub fn openZ(path: [*:0]const u8, flags: c_int, mode: ?c_uint) errno.PosixError!c_int {
    const fd = if (mode) |m| sys.open(path, flags, @as(c_uint, m)) else sys.open(path, flags);
    if (fd < 0) return errno.last();
    return fd;
}

pub fn close(fd: c_int) errno.PosixError!void {
    if (sys.close(fd) < 0) return errno.last();
}

pub fn readSome(fd: c_int, buf: []u8) errno.PosixError!usize {
    const n = sys.read(fd, buf.ptr, buf.len);
    if (n < 0) return errno.last();
    return @as(usize, @intCast(n));
}

pub fn writeSome(fd: c_int, buf: []const u8) errno.PosixError!usize {
    const n = sys.write(fd, buf.ptr, buf.len);
    if (n < 0) return errno.last();
    return @as(usize, @intCast(n));
}

pub fn writeAll(fd: c_int, buf: []const u8) errno.PosixError!void {
    var off: usize = 0;
    while (off < buf.len) {
        const n = try writeSome(fd, buf[off..]);
        if (n == 0) return error.Io;
        off += n;
    }
}

pub fn unlinkZ(path: [*:0]const u8) errno.PosixError!void {
    if (sys.unlink(path) < 0) return errno.last();
}

pub fn mkdirZ(path: [*:0]const u8, mode: c_uint) errno.PosixError!void {
    if (sys.mkdir(path, mode) < 0) return errno.last();
}

pub fn chdirZ(path: [*:0]const u8) errno.PosixError!void {
    if (sys.chdir(path) < 0) return errno.last();
}

pub fn getcwd(buf: []u8) errno.PosixError![*:0]const u8 {
    const p = sys.getcwd(buf.ptr, buf.len);
    if (p == null) return errno.last();
    return @as([*:0]const u8, p.?);
}

pub fn accessZ(path: [*:0]const u8, mode: c_int) errno.PosixError!void {
    if (sys.access(path, mode) < 0) return errno.last();
}

pub fn existsZ(path: [*:0]const u8) bool {
    return sys.access(path, constants.access.F_OK) == 0;
}
