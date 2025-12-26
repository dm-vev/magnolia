const sys = @import("sys.zig");
const errno = @import("errno.zig");

pub const Dir = struct {
    handle: *sys.DIR,

    pub fn open(path: [*:0]const u8) errno.PosixError!Dir {
        const h = sys.opendir(path) orelse return errno.last();
        return .{ .handle = h };
    }

    pub fn close(self: *Dir) void {
        _ = sys.closedir(self.handle);
    }

    pub fn rewind(self: *Dir) void {
        sys.rewinddir(self.handle);
    }

    pub fn next(self: *Dir) ?*sys.Dirent {
        return sys.readdir(self.handle);
    }
};

