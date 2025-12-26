const mg = @import("magnolia");

fn testArgs(ctx: *const mg.testing.Context) anyerror!void {
    // At minimum argv[0] should exist (command name).
    try mg.testing.expect(ctx.argc >= 1);
    const p0 = ctx.argv[0] orelse return error.ExpectationFailed;
    try mg.testing.expect(mg.args.zlen(p0) > 0);
}

fn testMalloc(ctx: *const mg.testing.Context) anyerror!void {
    _ = ctx;
    const buf = try mg.mem.allocSlice(u8, 16);
    defer mg.mem.freeSlice(u8, buf);

    for (buf, 0..) |*b, i| {
        b.* = @as(u8, @intCast(i));
    }
    try mg.testing.expect(buf[0] == 0 and buf[15] == 15);
}

fn testFlashRoundtrip(ctx: *const mg.testing.Context) anyerror!void {
    _ = ctx;
    const path: [*:0]const u8 = "/flash/zigtest.tmp";
    const wflags = mg.constants.open.O_WRONLY | mg.constants.open.O_CREAT | mg.constants.open.O_TRUNC;

    const fdw = try mg.fs.openZ(path, wflags, 0o644);
    defer mg.fs.close(fdw) catch {};
    try mg.fs.writeAll(fdw, "zigtest\n");

    const fdr = try mg.fs.openZ(path, mg.constants.open.O_RDONLY, null);
    defer mg.fs.close(fdr) catch {};

    var buf: [16]u8 = undefined;
    const n = try mg.fs.readSome(fdr, buf[0..]);
    try mg.testing.expect(n >= 7);

    // Best-effort cleanup.
    _ = mg.fs.unlinkZ(path) catch {};
}

fn testListBin(ctx: *const mg.testing.Context) anyerror!void {
    _ = ctx;
    var d = try mg.dir.Dir.open("/bin");
    defer d.close();

    const ent = d.next();
    try mg.testing.expect(ent != null);
}

pub export fn app_main(argc: c_int, argv: [*]?[*:0]u8) callconv(.C) c_int {
    const ctx: mg.testing.Context = .{ .argc = argc, .argv = argv };

    const cases = .{
        mg.testing.Case{ .name = "malloc", .func = testMalloc },
        mg.testing.Case{ .name = "flash_roundtrip", .func = testFlashRoundtrip },
        mg.testing.Case{ .name = "list_bin", .func = testListBin },
        // Keep last: it is intentionally minimal.
        mg.testing.Case{ .name = "args_sanity", .func = testArgs },
    };

    return mg.testing.run(&ctx, cases);
}
