const sys = @import("sys.zig");
const constants = @import("constants.zig");
const errno = @import("errno.zig");

pub const IoError = error{WriteFailed, ReadFailed};

pub fn write(fd: c_int, bytes: []const u8) sys.ssize_t {
    return sys.write(fd, bytes.ptr, bytes.len);
}

pub fn writeAll(fd: c_int, bytes: []const u8) (IoError || errno.PosixError)!void {
    var off: usize = 0;
    while (off < bytes.len) {
        const n = sys.write(fd, bytes.ptr + off, bytes.len - off);
        if (n < 0) return errno.last();
        if (n == 0) return error.WriteFailed;
        off += @as(usize, @intCast(n));
    }
}

pub fn read(fd: c_int, buf: []u8) sys.ssize_t {
    return sys.read(fd, buf.ptr, buf.len);
}

pub fn readSome(fd: c_int, buf: []u8) (IoError || errno.PosixError)!usize {
    const n = sys.read(fd, buf.ptr, buf.len);
    if (n < 0) return errno.last();
    return @as(usize, @intCast(n));
}

pub fn puts(msg: []const u8) void {
    _ = writeAll(constants.fd.stdout, msg) catch {};
    _ = writeAll(constants.fd.stdout, "\n") catch {};
}

pub fn eputs(msg: []const u8) void {
    _ = writeAll(constants.fd.stderr, msg) catch {};
    _ = writeAll(constants.fd.stderr, "\n") catch {};
}

