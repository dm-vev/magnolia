const std = @import("std");
const sys = @import("sys.zig");
const errno = @import("errno.zig");

pub const AllocError = error{OutOfMemory} || errno.PosixError;

const max_align_log2: u8 = @intCast(std.math.log2_int(usize, @alignOf(usize)));

const MagnoliaAllocator = struct {
    const vtable = std.mem.Allocator.VTable{
        .alloc = alloc,
        .resize = resize,
        .free = MagnoliaAllocator.free,
    };

    fn alloc(_: *anyopaque, len: usize, ptr_align: u8, ret_addr: usize) ?[*]u8 {
        _ = ret_addr;
        if (ptr_align > max_align_log2) return null;
        const p = sys.malloc(len) orelse return null;
        return @as([*]u8, @ptrCast(p));
    }

    fn resize(_: *anyopaque, buf: []u8, buf_align: u8, new_len: usize, ret_addr: usize) bool {
        _ = buf_align;
        _ = ret_addr;
        if (new_len <= buf.len) return true;
        return false;
    }

    fn free(_: *anyopaque, buf: []u8, buf_align: u8, ret_addr: usize) void {
        _ = buf_align;
        _ = ret_addr;
        sys.free(@as(?*anyopaque, @ptrCast(buf.ptr)));
    }
};

pub const allocator = std.mem.Allocator{
    .ptr = undefined,
    .vtable = &MagnoliaAllocator.vtable,
};

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
