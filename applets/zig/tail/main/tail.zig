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
    if (s[0] == '+' or s[0] == '-') return null;
    return std.fmt.parseInt(usize, s, 10) catch null;
}

fn tail_bytes(fd: c_int, count: usize) bool {
    if (count == 0) return true;
    var ring = std.heap.c_allocator.alloc(u8, count) catch return false;
    defer std.heap.c_allocator.free(ring);
    var pos: usize = 0;
    var total: usize = 0;
    var buf: [512]u8 = undefined;
    while (true) {
        const n = fs.readSome(fd, &buf) catch return false;
        if (n == 0) break;
        for (buf[0..n]) |b| {
            ring[pos] = b;
            pos = (pos + 1) % count;
            total += 1;
        }
    }
    const out_len = if (total < count) total else count;
    if (out_len == 0) return true;
    if (total >= count) {
        if (!writeBytes(ring[pos..])) return false;
        if (!writeBytes(ring[0..pos])) return false;
    } else {
        if (!writeBytes(ring[0..out_len])) return false;
    }
    return true;
}

const LineRing = struct {
    allocator: std.mem.Allocator,
    lines: []([]u8),
    count: usize,
    next: usize,

    fn init(allocator: std.mem.Allocator, cap: usize) !LineRing {
        var lines = try allocator.alloc([]u8, cap);
        for (lines) |*slot| slot.* = &[_]u8{};
        return .{ .allocator = allocator, .lines = lines, .count = 0, .next = 0 };
    }

    fn deinit(self: *LineRing) void {
        for (self.lines) |line| {
            if (line.len != 0) self.allocator.free(line);
        }
        self.allocator.free(self.lines);
    }

    fn push(self: *LineRing, line: []const u8) !void {
        if (self.lines.len == 0) return;
        if (self.count == self.lines.len) {
            const old = self.lines[self.next];
            if (old.len != 0) self.allocator.free(old);
        } else {
            self.count += 1;
        }
        self.lines[self.next] = try self.allocator.dupe(u8, line);
        self.next = (self.next + 1) % self.lines.len;
    }

    fn writeAll(self: *LineRing, reverse: bool) bool {
        if (self.count == 0) return true;
        const cap = self.lines.len;
        const start = if (self.count == cap) self.next else 0;
        if (!reverse) {
            var i: usize = 0;
            while (i < self.count) : (i += 1) {
                const idx = (start + i) % cap;
                if (!writeBytes(self.lines[idx])) return false;
            }
        } else {
            var i: usize = self.count;
            while (i > 0) : (i -= 1) {
                const idx = (start + i - 1) % cap;
                if (!writeBytes(self.lines[idx])) return false;
            }
        }
        return true;
    }
};

fn tail_lines(fd: c_int, count: usize, reverse: bool) bool {
    if (count == 0) return true;
    var ring = LineRing.init(std.heap.c_allocator, count) catch return false;
    defer ring.deinit();
    var current = std.ArrayList(u8).init(std.heap.c_allocator);
    defer current.deinit();

    var buf: [512]u8 = undefined;
    while (true) {
        const n = fs.readSome(fd, &buf) catch return false;
        if (n == 0) break;
        for (buf[0..n]) |b| {
            current.append(b) catch return false;
            if (b == '\n') {
                ring.push(current.items) catch return false;
                current.clearRetainingCapacity();
            }
        }
    }
    if (current.items.len > 0) {
        ring.push(current.items) catch return false;
    }
    return ring.writeAll(reverse);
}

fn usage() void {
    eprintf("usage: tail [-n lines] [-c bytes] [-qv] [-r] [file ...]\n", .{});
}

pub export fn app_main(argc: c_int, argv: [*]?[*:0]u8) callconv(.C) c_int {
    var it = args.Args.init(argc, argv);
    _ = it.next();

    var lines: usize = 10;
    var bytes: ?usize = null;
    var quiet = false;
    var verbose = false;
    var reverse = false;

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
                'r' => reverse = true,
                'f', 'F' => {
                    eprintf("tail: follow mode not supported\n", .{});
                    return 1;
                },
                else => {
                    usage();
                    return 1;
                },
            }
        }
    }

    if (bytes != null and reverse) {
        eprintf("tail: -r is only valid for line mode\n", .{});
        return 1;
    }

    if (files.items.len == 0) {
        if (bytes) |cnt| {
            return if (tail_bytes(constants.fd.stdin, cnt)) 0 else 1;
        }
        return if (tail_lines(constants.fd.stdin, lines, reverse)) 0 else 1;
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
                eprintf("tail: {s}: open failed\n", .{path});
                failed = true;
                break :blk constants.fd.stdin;
            };
        };
        const ok = if (bytes) |cnt| tail_bytes(fd, cnt) else tail_lines(fd, lines, reverse);
        if (fd != constants.fd.stdin) fs.close(fd) catch {};
        if (!ok) failed = true;
    }
    return if (failed) 1 else 0;
}
