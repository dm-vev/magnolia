const sys = @import("sys.zig");
const errno = @import("errno.zig");

pub const TestError = error{ExpectationFailed};

pub fn expect(ok: bool) TestError!void {
    if (!ok) return error.ExpectationFailed;
}

pub const Context = struct {
    argc: c_int,
    argv: [*]?[*:0]u8,
};

pub const Case = struct {
    name: [:0]const u8,
    func: fn (*const Context) anyerror!void,
};

pub fn run(ctx: *const Context, comptime cases: anytype) c_int {
    var failed: c_int = 0;

    inline for (cases) |case| {
        const res = case.func(ctx);
        if (res) |_| {
            _ = sys.printf("ok - %s\n", case.name.ptr);
        } else |_| {
            failed += 1;
            const e = errno.get();
            _ = sys.printf("not ok - %s (errno=%d %s)\n", case.name.ptr, e, errno.strerrorZ(e));
        }
    }

    _ = sys.printf("tests: %d failed\n", failed);
    return failed;
}
