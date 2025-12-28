const std = @import("std");
const mg = @import("magnolia");

const args = mg.args;
const constants = mg.constants;
const fs = mg.fs;
const io = mg.io;
const mem = mg.mem;

const Range = struct {
    start: isize,
    end: isize,
};

fn eprintf(comptime fmt: []const u8, list: anytype) void {
    var buf: [256]u8 = undefined;
    const msg = std.fmt.bufPrint(&buf, fmt, list) catch return;
    _ = io.writeAll(constants.fd.stderr, msg) catch {};
}

fn writeBytes(bytes: []const u8) bool {
    io.writeAll(constants.fd.stdout, bytes) catch return false;
    return true;
}

fn parseNumber(s: []const u8, idx: *usize) ?isize {
    var i = idx.*;
    if (i >= s.len or !std.ascii.isDigit(s[i])) return null;
    var value: isize = 0;
    while (i < s.len and std.ascii.isDigit(s[i])) : (i += 1) {
        value = value * 10 + @as(isize, s[i] - '0');
    }
    idx.* = i;
    return value;
}

fn parseRanges(list: []const u8, allocator: std.mem.Allocator) ?[]Range {
    if (list.len == 0) return null;
    var ranges = std.ArrayList(Range).init(allocator);
    errdefer ranges.deinit();

    var i: usize = 0;
    while (i < list.len) {
        if (ranges.items.len >= 256) return null;
        var start: isize = -1;
        var end: isize = -1;
        if (list[i] == '-') {
            i += 1;
            start = 1;
            const num = parseNumber(list, &i) orelse return null;
            if (num < 1) return null;
            end = num;
        } else {
            const num = parseNumber(list, &i) orelse return null;
            if (num < 1) return null;
            start = num;
            if (i < list.len and list[i] == '-') {
                i += 1;
                if (i >= list.len or list[i] == ',') {
                    end = -1;
                } else {
                    const num2 = parseNumber(list, &i) orelse return null;
                    if (num2 < start) return null;
                    end = num2;
                }
            } else {
                end = start;
            }
        }
        ranges.append(.{ .start = start, .end = end }) catch return null;
        if (i < list.len and list[i] == ',') {
            i += 1;
            continue;
        }
        if (i != list.len) return null;
    }
    return ranges.toOwnedSlice() catch null;
}

fn selected(idx: isize, ranges: []const Range) bool {
    for (ranges) |r| {
        if (idx < r.start) continue;
        if (r.end < 0 or idx <= r.end) return true;
    }
    return false;
}

fn cut_bytes(fd: c_int, ranges: []const Range) bool {
    var buf: [256]u8 = undefined;
    var pos: isize = 0;
    while (true) {
        const n = fs.readSome(fd, &buf) catch return false;
        if (n == 0) break;
        for (buf[0..n]) |b| {
            pos += 1;
            if (b == '\n') {
                pos = 0;
                if (!writeBytes("\n")) return false;
                continue;
            }
            if (selected(pos, ranges)) {
                if (!writeBytes(&[_]u8{b})) return false;
            }
        }
    }
    return true;
}

fn cut_fields_line(line: []const u8, ranges: []const Range, delim: u8, suppress_no_delim: bool) bool {
    var has_delim = false;
    for (line) |b| {
        if (b == delim) {
            has_delim = true;
            break;
        }
    }
    if (!has_delim and suppress_no_delim) {
        return true;
    }
    if (!has_delim) {
        if (!writeBytes(line)) return false;
        if (!writeBytes("\n")) return false;
        return true;
    }
    var field_idx: isize = 1;
    var start: usize = 0;
    var wrote = false;
    var i: usize = 0;
    while (i <= line.len) : (i += 1) {
        const is_delim = i == line.len or line[i] == delim;
        if (!is_delim) continue;
        if (selected(field_idx, ranges)) {
            if (wrote) {
                if (!writeBytes(&[_]u8{delim})) return false;
            }
            if (!writeBytes(line[start..i])) return false;
            wrote = true;
        }
        field_idx += 1;
        start = i + 1;
    }
    if (!writeBytes("\n")) return false;
    return true;
}

fn cut_fields(fd: c_int, ranges: []const Range, delim: u8, suppress_no_delim: bool) bool {
    var buf: [256]u8 = undefined;
    var line = std.ArrayList(u8).init(mem.allocator);
    defer line.deinit();
    while (true) {
        const n = fs.readSome(fd, &buf) catch return false;
        if (n == 0) break;
        for (buf[0..n]) |b| {
            line.append(b) catch return false;
            if (b == '\n') {
                const slice = line.items;
                if (!cut_fields_line(slice[0 .. slice.len - 1], ranges, delim, suppress_no_delim)) return false;
                line.clearRetainingCapacity();
            }
        }
    }
    if (line.items.len > 0) {
        if (!cut_fields_line(line.items, ranges, delim, suppress_no_delim)) return false;
    }
    return true;
}

fn usage() void {
    eprintf("usage: cut OPTION... [FILE]...\n  -b LIST  select bytes\n  -c LIST  select characters\n  -f LIST  select fields\n  -d DELIM use DELIM instead of TAB\n  -s       suppress lines without delimiters\n", .{});
}

pub export fn app_main(argc: c_int, argv: [*]?[*:0]u8) callconv(.C) c_int {
    var it = args.Args.init(argc, argv);
    _ = it.next();

    var mode: u8 = 0;
    var list: []const u8 = &[_]u8{};
    var delim: u8 = '\t';
    var suppress_no_delim = false;

    var files = std.ArrayList([]const u8).init(mem.allocator);
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
        if (arg.len == 2 and (arg[1] == 'b' or arg[1] == 'c' or arg[1] == 'f')) {
            const next = it.next() orelse {
                usage();
                return 1;
            };
            mode = arg[1];
            list = args.zslice(next);
            continue;
        }
        if (arg.len > 2 and (arg[1] == 'b' or arg[1] == 'c' or arg[1] == 'f')) {
            mode = arg[1];
            list = arg[2..];
            continue;
        }
        if (arg.len == 2 and arg[1] == 'd') {
            const next = it.next() orelse {
                usage();
                return 1;
            };
            const d = args.zslice(next);
            if (d.len != 1) {
                usage();
                return 1;
            }
            delim = d[0];
            continue;
        }
        if (arg.len > 2 and arg[1] == 'd') {
            if (arg.len != 3) {
                usage();
                return 1;
            }
            delim = arg[2];
            continue;
        }
        if (std.mem.eql(u8, arg, "-s")) {
            suppress_no_delim = true;
            continue;
        }
        usage();
        return 1;
    }

    if (mode == 0 or list.len == 0) {
        usage();
        return 1;
    }

    const ranges = parseRanges(list, mem.allocator) orelse {
        eprintf("cut: invalid list '{s}'\n", .{list});
        return 1;
    };
    defer mem.allocator.free(ranges);

    const do_bytes = mode == 'b' or mode == 'c';

    if (files.items.len == 0) {
        if (do_bytes) {
            return if (cut_bytes(constants.fd.stdin, ranges)) 0 else 1;
        }
        return if (cut_fields(constants.fd.stdin, ranges, delim, suppress_no_delim)) 0 else 1;
    }

    var failed = false;
    for (files.items) |path| {
        const fd = if (std.mem.eql(u8, path, "-")) constants.fd.stdin else blk: {
            const zpath = mem.allocator.dupeZ(u8, path) catch {
                failed = true;
                break :blk constants.fd.stdin;
            };
            defer mem.allocator.free(zpath);
            break :blk fs.openZ(zpath, constants.open.O_RDONLY, null) catch {
                eprintf("cut: {s}: open failed\n", .{path});
                failed = true;
                break :blk constants.fd.stdin;
            };
        };
        const ok = if (do_bytes) cut_bytes(fd, ranges) else cut_fields(fd, ranges, delim, suppress_no_delim);
        if (fd != constants.fd.stdin) fs.close(fd) catch {};
        if (!ok) failed = true;
    }
    return if (failed) 1 else 0;
}
