const std = @import("std");
const mg = @import("magnolia");

const args = mg.args;
const constants = mg.constants;
const errno = mg.errno;
const fs = mg.fs;
const io = mg.io;
const mem = mg.mem;
const c = @cImport({
    @cInclude("errno.h");
});

const Range = struct {
    start: isize,
    end: isize,
};

fn eprintf(comptime fmt: []const u8, list: anytype) void {
    var buf: [256]u8 = undefined;
    const msg = std.fmt.bufPrint(&buf, fmt, list) catch return;
    _ = io.writeAll(constants.fd.stderr, msg) catch {};
}

fn usage() void {
    eprintf("usage: cut -b list [-n] [file ...]\n", .{});
    eprintf("       cut -c list [file ...]\n", .{});
    eprintf("       cut -f list [-d delim] [-s] [file ...]\n", .{});
}

fn errnoMessage(err: anyerror) []const u8 {
    if (err == error.Io) return "I/O error";
    if (err == error.OutOfMemory) return std.mem.span(errno.strerrorZ(c.ENOMEM));
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

fn writeBytes(bytes: []const u8) anyerror!void {
    try writeAllRetry(constants.fd.stdout, bytes);
}

fn parseNumber(s: []const u8, idx: *usize) ?isize {
    var i = idx.*;
    if (i >= s.len or !std.ascii.isDigit(s[i])) return null;
    var value: isize = 0;
    while (i < s.len and std.ascii.isDigit(s[i])) : (i += 1) {
        const digit: isize = @as(isize, s[i] - '0');
        if (value > (std.math.maxInt(isize) - digit) / 10) return null;
        value = value * 10 + digit;
    }
    idx.* = i;
    return value;
}

fn parseRanges(list: []const u8, allocator: std.mem.Allocator) ![]Range {
    if (list.len == 0) return error.InvalidList;
    var ranges = std.ArrayList(Range).init(allocator);
    errdefer ranges.deinit();

    var i: usize = 0;
    while (i < list.len) {
        var start: isize = -1;
        var end: isize = -1;
        if (list[i] == '-') {
            i += 1;
            const num = parseNumber(list, &i) orelse return error.InvalidList;
            if (num < 1) return error.InvalidList;
            start = 1;
            end = num;
        } else {
            const num = parseNumber(list, &i) orelse return error.InvalidList;
            if (num < 1) return error.InvalidList;
            start = num;
            if (i < list.len and list[i] == '-') {
                i += 1;
                if (i >= list.len or list[i] == ',') {
                    end = -1;
                } else {
                    const num2 = parseNumber(list, &i) orelse return error.InvalidList;
                    if (num2 < start) return error.InvalidList;
                    end = num2;
                }
            } else {
                end = start;
            }
        }
        try ranges.append(.{ .start = start, .end = end });
        if (i < list.len and list[i] == ',') {
            i += 1;
            if (i >= list.len) return error.InvalidList;
            continue;
        }
        if (i != list.len) return error.InvalidList;
    }
    return ranges.toOwnedSlice();
}

fn selected(idx: isize, ranges: []const Range) bool {
    for (ranges) |r| {
        if (idx < r.start) continue;
        if (r.end < 0 or idx <= r.end) return true;
    }
    return false;
}

fn cut_bytes(fd: c_int, ranges: []const Range) anyerror!void {
    var buf: [256]u8 = undefined;
    var pos: isize = 0;
    while (true) {
        const n = try readRetry(fd, &buf);
        if (n == 0) break;
        for (buf[0..n]) |b| {
            if (b == '\n') {
                pos = 0;
                try writeBytes("\n");
                continue;
            }
            pos += 1;
            if (selected(pos, ranges)) {
                try writeBytes(&[_]u8{b});
            }
        }
    }
}

fn cut_fields_line(line: []const u8, ranges: []const Range, delim: u8, suppress_no_delim: bool,
                   had_newline: bool) anyerror!void {
    const has_delim = std.mem.indexOfScalar(u8, line, delim) != null;
    if (!has_delim) {
        if (suppress_no_delim) {
            return;
        }
        if (line.len > 0) try writeBytes(line);
        if (had_newline) try writeBytes("\n");
        return;
    }
    var field_idx: isize = 1;
    var start: usize = 0;
    var wrote = false;
    var i: usize = 0;
    while (i <= line.len) : (i += 1) {
        const at_end = i == line.len;
        const is_delim = !at_end and line[i] == delim;
        if (!at_end and !is_delim) continue;
        if (selected(field_idx, ranges)) {
            if (wrote) {
                try writeBytes(&[_]u8{delim});
            }
            if (i > start) {
                try writeBytes(line[start..i]);
            }
            wrote = true;
        }
        field_idx += 1;
        start = i + 1;
    }
    if (had_newline) try writeBytes("\n");
}

fn cut_fields(fd: c_int, ranges: []const Range, delim: u8, suppress_no_delim: bool) anyerror!void {
    var buf: [256]u8 = undefined;
    var line = std.ArrayList(u8).init(mem.allocator);
    defer line.deinit();
    while (true) {
        const n = try readRetry(fd, &buf);
        if (n == 0) break;
        for (buf[0..n]) |b| {
            if (b == '\n') {
                try cut_fields_line(line.items, ranges, delim, suppress_no_delim, true);
                line.clearRetainingCapacity();
                continue;
            }
            try line.append(b);
        }
    }
    if (line.items.len > 0) {
        try cut_fields_line(line.items, ranges, delim, suppress_no_delim, false);
    }
}

pub export fn app_main(argc: c_int, argv: [*]?[*:0]u8) callconv(.C) c_int {
    var it = args.Args.init(argc, argv);
    _ = it.next();

    var list: []const u8 = &[_]u8{};
    var delim: u8 = '\t';
    var delim_set = false;
    var suppress_no_delim = false;
    var no_split = false;
    var bflag = false;
    var cflag = false;
    var fflag = false;

    var files = std.ArrayList([]const u8).init(mem.allocator);
    defer files.deinit();

    while (it.next()) |argz| {
        const arg = args.zslice(argz);
        if (arg.len == 0) continue;
        if (std.mem.eql(u8, arg, "--")) {
            while (it.next()) |rest| {
                files.append(args.zslice(rest)) catch {
                    eprintf("cut: {s}\n", .{errnoMessage(error.OutOfMemory)});
                    return 1;
                };
            }
            break;
        }
        if (arg[0] != '-' or arg.len == 1) {
            files.append(arg) catch {
                eprintf("cut: {s}\n", .{errnoMessage(error.OutOfMemory)});
                return 1;
            };
            continue;
        }
        var i: usize = 1;
        opt_loop: while (i < arg.len) {
            const opt = arg[i];
            switch (opt) {
                'b', 'c', 'f', 'd' => {
                    var optarg: []const u8 = undefined;
                    if (i + 1 < arg.len) {
                        optarg = arg[i + 1 ..];
                    } else {
                        const next = it.next() orelse {
                            eprintf("cut: option requires an argument -- {c}\n", .{opt});
                            usage();
                            return 1;
                        };
                        optarg = args.zslice(next);
                    }
                    switch (opt) {
                        'b' => {
                            list = optarg;
                            bflag = true;
                        },
                        'c' => {
                            list = optarg;
                            cflag = true;
                        },
                        'f' => {
                            list = optarg;
                            fflag = true;
                        },
                        'd' => {
                            if (optarg.len != 1) {
                                eprintf("cut: delimiter must be a single character\n", .{});
                                usage();
                                return 1;
                            }
                            delim = optarg[0];
                            delim_set = true;
                        },
                        else => {},
                    }
                    break :opt_loop;
                },
                's' => {
                    suppress_no_delim = true;
                    i += 1;
                },
                'n' => {
                    no_split = true;
                    i += 1;
                },
                else => {
                    eprintf("cut: illegal option -- {c}\n", .{opt});
                    usage();
                    return 1;
                },
            }
        }
    }

    const mode_count = @intFromBool(bflag) + @intFromBool(cflag) + @intFromBool(fflag);
    if (mode_count == 0) {
        usage();
        return 1;
    }
    if (mode_count > 1) {
        eprintf("cut: only one type of list may be specified\n", .{});
        usage();
        return 1;
    }
    if (!fflag and delim_set) {
        eprintf("cut: -d is only valid with -f\n", .{});
        usage();
        return 1;
    }
    if (!fflag and suppress_no_delim) {
        eprintf("cut: -s is only valid with -f\n", .{});
        usage();
        return 1;
    }
    if (!bflag and no_split) {
        eprintf("cut: -n is only valid with -b\n", .{});
        usage();
        return 1;
    }
    if (list.len == 0) {
        eprintf("cut: invalid byte, character, or field list\n", .{});
        return 1;
    }

    const ranges = parseRanges(list, mem.allocator) catch |err| {
        if (err == error.OutOfMemory) {
            eprintf("cut: {s}\n", .{errnoMessage(err)});
        } else {
            eprintf("cut: invalid byte, character, or field list\n", .{});
        }
        return 1;
    };
    defer mem.allocator.free(ranges);

    // TODO: Implement multibyte-aware -c/-n once locale support is available.

    const do_bytes = bflag or cflag;
    if (files.items.len == 0) {
        if (do_bytes) {
            cut_bytes(constants.fd.stdin, ranges) catch |err| {
                eprintf("cut: {s}\n", .{errnoMessage(err)});
                return 1;
            };
        } else {
            cut_fields(constants.fd.stdin, ranges, delim, suppress_no_delim) catch |err| {
                eprintf("cut: {s}\n", .{errnoMessage(err)});
                return 1;
            };
        }
        return 0;
    }

    var failed = false;
    for (files.items) |path| {
        const fd = if (std.mem.eql(u8, path, "-")) constants.fd.stdin else blk: {
            const zpath = mem.allocator.dupeZ(u8, path) catch {
                eprintf("cut: {s}: {s}\n", .{path, errnoMessage(error.OutOfMemory)});
                failed = true;
                break :blk constants.fd.stdin;
            };
            defer mem.allocator.free(zpath);
            break :blk fs.openZ(zpath, constants.open.O_RDONLY, null) catch |err| {
                eprintf("cut: {s}: {s}\n", .{path, errnoMessage(err)});
                failed = true;
                break :blk constants.fd.stdin;
            };
        };

        if (fd == constants.fd.stdin and !std.mem.eql(u8, path, "-")) {
            continue;
        }

        if (do_bytes) {
            cut_bytes(fd, ranges) catch |err| {
                const msg = errnoMessage(err);
                if (fd != constants.fd.stdin) fs.close(fd) catch {};
                eprintf("cut: {s}: {s}\n", .{path, msg});
                failed = true;
                continue;
            };
        } else {
            cut_fields(fd, ranges, delim, suppress_no_delim) catch |err| {
                const msg = errnoMessage(err);
                if (fd != constants.fd.stdin) fs.close(fd) catch {};
                eprintf("cut: {s}: {s}\n", .{path, msg});
                failed = true;
                continue;
            };
        }
        if (fd != constants.fd.stdin) fs.close(fd) catch {};
    }

    return if (failed) 1 else 0;
}
