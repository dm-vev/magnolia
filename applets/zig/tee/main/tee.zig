const std = @import("std");
const mg = @import("magnolia");

const args = mg.args;
const constants = mg.constants;
const errno = mg.errno;
const fs = mg.fs;
const io = mg.io;
const mem = mg.mem;
const c = @cImport({
    @cInclude("signal.h");
});

fn eprintf(comptime fmt: []const u8, list: anytype) void {
    var buf: [256]u8 = undefined;
    const msg = std.fmt.bufPrint(&buf, fmt, list) catch return;
    _ = io.writeAll(constants.fd.stderr, msg) catch {};
}

fn usage() void {
    eprintf("usage: tee [-ai] [file ...]\n", .{});
}

fn errnoMessage(err: anyerror) []const u8 {
    if (err == error.Io) return "I/O error";
    return std.mem.span(errno.strerrorZ(errno.get()));
}

fn readRetry(fd: c_int, buf: []u8) errno.PosixError!usize {
    while (true) {
        const n = fs.readSome(fd, buf) catch |err| switch (err) {
            error.Interrupted => continue,
            else => return err,
        };
        return n;
    }
}

fn writeAllRetry(fd: c_int, buf: []const u8) errno.PosixError!void {
    var off: usize = 0;
    while (off < buf.len) {
        const n = fs.writeSome(fd, buf[off..]) catch |err| switch (err) {
            error.Interrupted => continue,
            else => return err,
        };
        if (n == 0) return error.Io;
        off += n;
    }
}

const Output = struct {
    fd: c_int,
    path: []const u8,
};

pub export fn app_main(argc: c_int, argv: [*]?[*:0]u8) callconv(.C) c_int {
    var it = args.Args.init(argc, argv);
    _ = it.next();

    var append = false;
    var ignore_int = false;
    var files = std.ArrayList([]const u8).init(mem.allocator);
    defer files.deinit();

    while (it.next()) |argz| {
        const arg = args.zslice(argz);
        if (arg.len == 0) continue;
        if (std.mem.eql(u8, arg, "--")) {
            while (it.next()) |rest| {
                if (files.append(args.zslice(rest))) |_| {} else |_| {
                    eprintf("tee: out of memory\n", .{});
                    return 1;
                }
            }
            break;
        }
        if (arg[0] != '-' or arg.len == 1) {
            if (files.append(arg)) |_| {} else |_| {
                eprintf("tee: out of memory\n", .{});
                return 1;
            }
            continue;
        }
        var ok = true;
        for (arg[1..]) |ch| {
            switch (ch) {
                'a' => append = true,
                'i' => ignore_int = true,
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

    if (ignore_int) {
        _ = c.signal(c.SIGINT, c.SIG_IGN);
    }

    var outputs = std.ArrayList(Output).init(mem.allocator);
    defer {
        for (outputs.items) |out| {
            if (out.fd >= 0) fs.close(out.fd) catch {};
        }
        outputs.deinit();
    }

    var failed = false;
    for (files.items) |path| {
        if (std.mem.eql(u8, path, "-")) {
            continue;
        }
        const zpath = mem.allocator.dupeZ(u8, path) catch {
            eprintf("tee: out of memory\n", .{});
            failed = true;
            continue;
        };
        defer mem.allocator.free(zpath);
        const flags = if (append) constants.open.O_WRONLY | constants.open.O_CREAT | constants.open.O_APPEND else constants.open.O_WRONLY | constants.open.O_CREAT | constants.open.O_TRUNC;
        const fd = fs.openZ(zpath, flags, 0o666) catch |err| {
            eprintf("tee: {s}: {s}\n", .{path, errnoMessage(err)});
            failed = true;
            continue;
        };
        if (outputs.append(.{ .fd = fd, .path = path })) |_| {} else |_| {
            eprintf("tee: out of memory\n", .{});
            _ = fs.close(fd) catch {};
            return 1;
        }
    }

    var buf: [512]u8 = undefined;
    while (true) {
        const n = readRetry(constants.fd.stdin, &buf) catch |err| {
            eprintf("tee: read: {s}\n", .{errnoMessage(err)});
            failed = true;
            break;
        };
        if (n == 0) break;
        if (writeAllRetry(constants.fd.stdout, buf[0..n])) |_| {} else |err| {
            eprintf("tee: stdout: {s}\n", .{errnoMessage(err)});
            failed = true;
            break;
        }
        for (outputs.items) |*out| {
            if (out.fd < 0) {
                continue;
            }
            if (writeAllRetry(out.fd, buf[0..n])) |_| {} else |err| {
                eprintf("tee: {s}: {s}\n", .{out.path, errnoMessage(err)});
                failed = true;
                _ = fs.close(out.fd) catch {};
                out.fd = -1;
            }
        }
    }
    return if (failed) 1 else 0;
}
