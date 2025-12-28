const std = @import("std");
const mg = @import("magnolia");

const args = mg.args;
const constants = mg.constants;
const fs = mg.fs;
const io = mg.io;

fn eprintf(comptime fmt: []const u8, list: anytype) void {
    var buf: [256]u8 = undefined;
    const msg = std.fmt.bufPrint(&buf, fmt, list) catch return;
    _ = io.writeAll(constants.fd.stderr, msg) catch {};
}

fn usage() void {
    eprintf("usage: tee [-ai] [file ...]\n", .{});
}

pub export fn app_main(argc: c_int, argv: [*]?[*:0]u8) callconv(.C) c_int {
    var it = args.Args.init(argc, argv);
    _ = it.next();

    var append = false;
    var files = std.ArrayList([]const u8).init(std.heap.c_allocator);
    defer files.deinit();

    while (it.next()) |argz| {
        const arg = args.zslice(argz);
        if (arg.len == 0) continue;
        if (std.mem.eql(u8, arg, "--")) {
            while (it.next()) |rest| {
                files.append(args.zslice(rest)) catch {};
            }
            break;
        }
        if (arg[0] != '-' or arg.len == 1) {
            files.append(arg) catch {};
            continue;
        }
        var ok = true;
        for (arg[1..]) |ch| {
            switch (ch) {
                'a' => append = true,
                'i' => {},
                else => {
                    ok = false;
                    break;
                },
            }
        }
        if (!ok) {
            usage();
            return 1;
        }
    }

    var fds = std.ArrayList(c_int).init(std.heap.c_allocator);
    defer {
        for (fds.items) |fd| {
            if (fd >= 0) fs.close(fd) catch {};
        }
        fds.deinit();
    }

    for (files.items) |path| {
        const zpath = std.cstr.addNullByte(std.heap.c_allocator, path) catch {
            eprintf("tee: {s}: invalid path\n", .{path});
            continue;
        };
        defer std.heap.c_allocator.free(zpath);
        const flags = if (append) constants.open.O_WRONLY | constants.open.O_CREAT | constants.open.O_APPEND else constants.open.O_WRONLY | constants.open.O_CREAT | constants.open.O_TRUNC;
        const fd = fs.openZ(zpath, flags, 0o666) catch {
            eprintf("tee: {s}: open failed\n", .{path});
            continue;
        };
        fds.append(fd) catch {};
    }

    var buf: [512]u8 = undefined;
    while (true) {
        const n = fs.readSome(constants.fd.stdin, &buf) catch return 1;
        if (n == 0) break;
        _ = io.writeAll(constants.fd.stdout, buf[0..n]) catch return 1;
        for (fds.items) |fd| {
            _ = fs.writeAll(fd, buf[0..n]) catch {};
        }
    }
    return 0;
}
