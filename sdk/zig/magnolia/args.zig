pub const Args = struct {
    argc: c_int,
    argv: [*]?[*:0]u8,
    i: c_int = 0,

    pub fn init(argc: c_int, argv: [*]?[*:0]u8) Args {
        return .{ .argc = argc, .argv = argv, .i = 0 };
    }

    pub fn next(self: *Args) ?[*:0]const u8 {
        if (self.i >= self.argc) return null;
        const p = self.argv[@as(usize, @intCast(self.i))] orelse return null;
        self.i += 1;
        return @as([*:0]const u8, p);
    }
};

pub fn zlen(s: [*:0]const u8) usize {
    var n: usize = 0;
    while (s[n] != 0) : (n += 1) {}
    return n;
}

pub fn zslice(s: [*:0]const u8) []const u8 {
    return s[0..zlen(s)];
}
