const sys = @import("sys.zig");

pub const PosixError = error{
    AccessDenied,
    FileNotFound,
    NotDir,
    IsDir,
    AlreadyExists,
    InvalidArgument,
    NoSpaceLeft,
    NotEmpty,
    Interrupted,
    WouldBlock,
    BrokenPipe,
    Io,
    Unknown,
};

// Workaround: on Xtensa, Zig PIC currently has issues with dereferencing the
// `__errno()` pointer directly. We provide `magnolia_errno()` from C.
extern fn magnolia_errno() c_int;

pub fn get() c_int {
    return magnolia_errno();
}

pub fn toError(e: c_int) PosixError {
    return switch (e) {
        2 => error.FileNotFound, // ENOENT
        5 => error.Io, // EIO
        13 => error.AccessDenied, // EACCES
        17 => error.AlreadyExists, // EEXIST
        20 => error.NotDir, // ENOTDIR
        21 => error.IsDir, // EISDIR
        22 => error.InvalidArgument, // EINVAL
        28 => error.NoSpaceLeft, // ENOSPC
        39 => error.NotEmpty, // ENOTEMPTY
        4 => error.Interrupted, // EINTR
        11 => error.WouldBlock, // EAGAIN
        32 => error.BrokenPipe, // EPIPE
        else => error.Unknown,
    };
}

pub fn last() PosixError {
    return toError(get());
}

pub fn strerrorZ(e: c_int) [*:0]const u8 {
    return sys.strerror(e);
}

