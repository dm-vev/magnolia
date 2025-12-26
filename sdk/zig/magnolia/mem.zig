const sys = @import("sys.zig");
const errno = @import("errno.zig");

pub const AllocError = error{OutOfMemory} || errno.PosixError;

pub fn malloc(size: usize) AllocError!*anyopaque {
    const p = sys.malloc(size);
    if (p == null) return error.OutOfMemory;
    return p.?;
}

pub fn calloc(nmemb: usize, size: usize) AllocError!*anyopaque {
    const p = sys.calloc(nmemb, size);
    if (p == null) return error.OutOfMemory;
    return p.?;
}

pub fn realloc(ptr: ?*anyopaque, size: usize) AllocError!*anyopaque {
    const p = sys.realloc(ptr, size);
    if (p == null) return error.OutOfMemory;
    return p.?;
}

pub fn free(ptr: ?*anyopaque) void {
    sys.free(ptr);
}

pub fn allocSlice(comptime T: type, n: usize) AllocError![]T {
    if (n == 0) return &[_]T{};
    const bytes = n * @sizeOf(T);
    const raw = try malloc(bytes);
    const aligned: *align(@alignOf(T)) anyopaque = @alignCast(raw);
    const p: [*]T = @ptrCast(aligned);
    return p[0..n];
}

pub fn freeSlice(comptime T: type, slice: []T) void {
    if (slice.len == 0) return;
    free(@ptrCast(slice.ptr));
}
