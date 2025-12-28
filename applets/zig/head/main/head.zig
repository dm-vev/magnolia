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

fn writeBytes(bytes: []const u8) bool {
    io.writeAll(constants.fd.stdout, bytes) catch return false;
    return true;
}

fn parseCount(s: []const u8) ?usize {
    if (s.len == 0) return null;
    return std.fmt.parseInt(usize, s, 10) catch null;
}

fn head_bytes(fd: c_int, count: usize) bool {
    var left = count;
    var buf: [512]u8 = undefined;
    while (left > 0) {
        const want = if (left < buf.len) left else buf.len;
        const n = fs.readSome(fd, buf[0..want]) catch return false;
        if (n == 0) break;
        if (!writeBytes(buf[0..n])) return false;
        left -= n;
    }
    return true;
}

fn head_lines(fd: c_int, count: usize) bool {
    var left = count;
    var buf: [512]u8 = undefined;
    while (left > 0) {
        const n = fs.readSome(fd, &buf) catch return false;
        if (n == 0) break;
        var i: usize = 0;
        while (i < n) : (i += 1) {
            const b = buf[i];
            if (!writeBytes(buf[i .. i + 1])) return false;
            if (b == '\n') {
                left -= 1;
                if (left == 0) return true;
            }
        }
    }
    return true;
}

fn usage() void {
    eprintf("usage: head [-n lines] [-c bytes] [-qv] [file ...]\n", .{});
}

pub export fn app_main(argc: c_int, argv: [*]?[*:0]u8) callconv(.C) c_int {
    var it = args.Args.init(argc, argv);
    _ = it.next();

    var lines: usize = 10;
    var bytes: ?usize = null;
    var quiet = false;
    var verbose = false;

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
        if (arg.len > 1 and std.ascii.isDigit(arg[1])) {
            const v = parseCount(arg[1..]) orelse {
                usage();
                return 1;
            };
            lines = v;
            continue;
        }
        var j: usize = 1;
        while (j < arg.len) : (j += 1) {
            const ch = arg[j];
            switch (ch) {
                'n', 'c' => {
                    var value: []const u8 = undefined;
                    if (j + 1 < arg.len) {
                        value = arg[j + 1 ..];
                        j = arg.len;
                    } else if (it.next()) |next| {
                        value = args.zslice(next);
                    } else {
                        usage();
                        return 1;
                    }
                    const v = parseCount(value) orelse {
                        usage();
                        return 1;
                    };
                    if (ch == 'n') {
                        lines = v;
                        bytes = null;
                    } else {
                        bytes = v;
                    }
                    j = arg.len;
                },
                'q' => quiet = true,
                'v' => verbose = true,
                else => {
                    usage();
                    return 1;
                },
            }
        }
    }

    if (files.items.len == 0) {
        if (bytes) |cnt| {
            return if (head_bytes(constants.fd.stdin, cnt)) 0 else 1;
        }
        return if (head_lines(constants.fd.stdin, lines)) 0 else 1;
    }

    var failed = false;
    for (files.items, 0..) |path, idx| {
        const show_header = if (verbose) true else if (quiet) false else files.items.len > 1;
        if (show_header) {
            if (idx > 0) {
                _ = writeBytes("\n");
            }
            _ = writeBytes("==> ");
            _ = writeBytes(path);
            _ = writeBytes(" <==\n");
        }
        const fd = if (std.mem.eql(u8, path, "-")) constants.fd.stdin else blk: {
            const zpath = std.cstr.addNullByte(std.heap.c_allocator, path) catch {
                failed = true;
                break :blk constants.fd.stdin;
            };
            defer std.heap.c_allocator.free(zpath);
            break :blk fs.openZ(zpath, constants.open.O_RDONLY, null) catch {
                eprintf("head: {s}: open failed\n", .{path});
                failed = true;
                break :blk constants.fd.stdin;
            };
        };
        const ok = if (bytes) |cnt| head_bytes(fd, cnt) else head_lines(fd, lines);
        if (fd != constants.fd.stdin) fs.close(fd) catch {};
        if (!ok) failed = true;
    }
    return if (failed) 1 else 0;
}
