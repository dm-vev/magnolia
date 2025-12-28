const std = @import("std");
const mg = @import("magnolia");

const fs = mg.fs;
const io = mg.io;
const mem = mg.mem;
const args = mg.args;
const constants = mg.constants;
const sys = mg.sys;

const LINE_BYTES: usize = 16;

const FormatMode = enum {
    Canonical,
    ByteOctal,
    Char,
    ShortDec,
    ShortOct,
    ShortHex,
};

fn eprintf(comptime fmt: []const u8, args_list: anytype) void {
    var buf: [256]u8 = undefined;
    const msg = std.fmt.bufPrint(&buf, fmt, args_list) catch return;
    _ = io.writeAll(constants.fd.stderr, msg) catch {};
}

fn parseToken(s: []const u8) !struct { value: u64, rest: []const u8 } {
    if (s.len == 0) return error.Invalid;
    var idx: usize = 0;
    var radix: u8 = 10;
    if (s.len >= 2 and s[0] == '0' and (s[1] == 'x' or s[1] == 'X')) {
        radix = 16;
        idx = 2;
    }
    const start = idx;
    while (idx < s.len) : (idx += 1) {
        const c = s[idx];
        const ok = if (radix == 16) std.ascii.isHex(c) else std.ascii.isDigit(c);
        if (!ok) break;
    }
    if (idx == start) return error.Invalid;
    const num_str = s[start..idx];
    var value = try std.fmt.parseInt(u64, num_str, radix);
    var rest = s[idx..];
    if (rest.len > 0) {
        const mult: u64 = switch (rest[0]) {
            'b' => 512,
            'k', 'K' => 1024,
            'm', 'M' => 1024 * 1024,
            'g', 'G' => 1024 * 1024 * 1024,
            else => 1,
        };
        if (mult != 1) {
            value = std.math.mul(u64, value, mult) catch return error.Invalid;
            rest = rest[1..];
        }
    }
    return .{ .value = value, .rest = rest };
}

fn parseSize(s: []const u8) !u64 {
    var rest = s;
    var total: u64 = 1;
    while (true) {
        const tok = try parseToken(rest);
        total = std.math.mul(u64, total, tok.value) catch return error.Invalid;
        if (tok.rest.len == 0) return total;
        if (tok.rest[0] == 'x' or tok.rest[0] == '*') {
            rest = tok.rest[1..];
            continue;
        }
        return error.Invalid;
    }
}

fn pushHex(list: *std.ArrayList(u8), value: u64, width: usize) void {
    var buf: [16]u8 = undefined;
    var v = value;
    var i: usize = width;
    while (i > 0) : (i -= 1) {
        const nib: u8 = @intCast(v & 0xF);
        buf[i - 1] = if (nib < 10) ('0' + nib) else ('a' + nib - 10);
        v >>= 4;
    }
    _ = list.appendSlice(buf[0..width]) catch {};
}

fn pushOctal(list: *std.ArrayList(u8), value: u64, width: usize) void {
    var buf: [22]u8 = undefined;
    var v = value;
    var i: usize = width;
    while (i > 0) : (i -= 1) {
        const digit: u8 = @intCast(v & 0x7);
        buf[i - 1] = '0' + digit;
        v >>= 3;
    }
    _ = list.appendSlice(buf[0..width]) catch {};
}

fn renderChar(list: *std.ArrayList(u8), b: u8) void {
    _ = list.append(' ') catch {};
    _ = list.append(' ') catch {};
    switch (b) {
        0 => {
            _ = list.append('\\') catch {};
            _ = list.append('0') catch {};
        },
        '\n' => {
            _ = list.append('\\') catch {};
            _ = list.append('n') catch {};
        },
        '\r' => {
            _ = list.append('\\') catch {};
            _ = list.append('r') catch {};
        },
        '\t' => {
            _ = list.append('\\') catch {};
            _ = list.append('t') catch {};
        },
        '\b' => {
            _ = list.append('\\') catch {};
            _ = list.append('b') catch {};
        },
        '\f' => {
            _ = list.append('\\') catch {};
            _ = list.append('f') catch {};
        },
        '\v' => {
            _ = list.append('\\') catch {};
            _ = list.append('v') catch {};
        },
        '\\' => {
            _ = list.append('\\') catch {};
            _ = list.append('\\') catch {};
        },
        else => {
            if (b >= 0x20 and b <= 0x7e) {
                _ = list.append(' ') catch {};
                _ = list.append(b) catch {};
            } else {
                _ = list.append('.') catch {};
                _ = list.append(' ') catch {};
            }
        },
    }
}

fn printLine(mode: FormatMode, offset: u64, buf: []const u8) void {
    var list = std.ArrayList(u8).init(std.heap.page_allocator);
    defer list.deinit();
    pushHex(&list, offset, 8);
    _ = list.append(' ') catch {};
    _ = list.append(' ') catch {};
    switch (mode) {
        .Canonical => {
            var i: usize = 0;
            while (i < LINE_BYTES) : (i += 1) {
                if (i < buf.len) {
                    pushHex(&list, buf[i], 2);
                    _ = list.append(' ') catch {};
                } else {
                    _ = list.appendSlice("   ") catch {};
                }
                if (i == 7) {
                    _ = list.append(' ') catch {};
                }
            }
            _ = list.append(' ') catch {};
            _ = list.append('|') catch {};
            i = 0;
            while (i < LINE_BYTES) : (i += 1) {
                if (i < buf.len) {
                    const b = buf[i];
                    _ = list.append(if (b >= 0x20 and b <= 0x7e) b else '.') catch {};
                } else {
                    _ = list.append(' ') catch {};
                }
            }
            _ = list.append('|') catch {};
        },
        .ByteOctal => {
            var i: usize = 0;
            while (i < LINE_BYTES) : (i += 1) {
                if (i < buf.len) {
                    _ = list.append(' ') catch {};
                    pushOctal(&list, buf[i], 3);
                } else {
                    _ = list.appendSlice("    ") catch {};
                }
            }
        },
        .Char => {
            var i: usize = 0;
            while (i < LINE_BYTES) : (i += 1) {
                if (i < buf.len) {
                    renderChar(&list, buf[i]);
                } else {
                    _ = list.appendSlice("   ") catch {};
                }
            }
        },
        .ShortDec, .ShortOct, .ShortHex => {
            const width: usize = switch (mode) { .ShortDec => 5, .ShortOct => 6, .ShortHex => 4, else => 4 };
            var i: usize = 0;
            while (i < LINE_BYTES) : (i += 2) {
                if (i + 1 < buf.len) {
                    const word: u16 = @intCast(buf[i]) | (@as(u16, @intCast(buf[i + 1])) << 8);
                    _ = list.append(' ') catch {};
                    switch (mode) {
                        .ShortDec => {
                            var tmp: [8]u8 = undefined;
                            const s = std.fmt.bufPrint(&tmp, "{d:0>5}", .{word}) catch "";
                            _ = list.appendSlice(s) catch {};
                        },
                        .ShortOct => {
                            pushOctal(&list, word, width);
                        },
                        .ShortHex => {
                            pushHex(&list, word, width);
                        },
                        else => {},
                    }
                } else {
                    var pad = width + 1;
                    while (pad > 0) : (pad -= 1) {
                        _ = list.append(' ') catch {};
                    }
                }
            }
        },
    }
    _ = list.append('\n') catch {};
    _ = io.writeAll(constants.fd.stdout, list.items) catch {};
}

fn skipBytes(fd: c_int, skip: *u64) bool {
    if (skip.* == 0) return true;
    const offset: sys.off_t = @intCast(skip.*);
    if (sys.lseek(fd, offset, constants.seek.cur) >= 0) {
        skip.* = 0;
        return true;
    }
    var buf: [256]u8 = undefined;
    while (skip.* > 0) {
        const want = if (skip.* < buf.len) @as(usize, @intCast(skip.*)) else buf.len;
        const n = fs.readSome(fd, buf[0..want]) catch return false;
        if (n == 0) return false;
        skip.* -= @intCast(n);
    }
    return true;
}

fn hexdumpFd(fd: c_int, name: []const u8, mode: FormatMode, verbose: bool,
             offset: *u64, remaining: ?*u64, skip: *u64) bool {
    var prev: [LINE_BYTES]u8 = undefined;
    var prev_len: usize = 0;
    var suppressed = false;

    while (true) {
        if (skip.* > 0) {
            if (!skipBytes(fd, skip)) return false;
        }
        var want: usize = LINE_BYTES;
        if (remaining) |rem| {
            if (rem.* == 0) break;
            if (rem.* < want) want = @intCast(rem.*);
        }
        var buf: [LINE_BYTES]u8 = undefined;
        const n = fs.readSome(fd, buf[0..want]) catch {
            eprintf("hexdump: {s}: read error\n", .{name});
            return false;
        };
        if (n == 0) break;
        if (remaining) |rem| {
            rem.* -= @intCast(n);
        }
        const same = !verbose and prev_len == n and std.mem.eql(u8, prev[0..n], buf[0..n]);
        if (same) {
            if (!suppressed) {
                _ = io.writeAll(constants.fd.stdout, "*\n") catch {};
                suppressed = true;
            }
        } else {
            suppressed = false;
            printLine(mode, offset.*, buf[0..n]);
            std.mem.copy(u8, prev[0..n], buf[0..n]);
            prev_len = n;
        }
        offset.* += @intCast(n);
    }
    return true;
}

fn hexdumpMain(argv: []const []const u8) c_int {
    var mode: FormatMode = .Canonical;
    var verbose = false;
    var length: u64 = 0;
    var skip: u64 = 0;
    var use_length = false;

    var files = std.ArrayList([]const u8).init(std.heap.page_allocator);
    defer files.deinit();

    var i: usize = 0;
    while (i < argv.len) : (i += 1) {
        const arg = argv[i];
        if (std.mem.eql(u8, arg, "-b")) {
            mode = .ByteOctal;
        } else if (std.mem.eql(u8, arg, "-c")) {
            mode = .Char;
        } else if (std.mem.eql(u8, arg, "-C")) {
            mode = .Canonical;
        } else if (std.mem.eql(u8, arg, "-d")) {
            mode = .ShortDec;
        } else if (std.mem.eql(u8, arg, "-o")) {
            mode = .ShortOct;
        } else if (std.mem.eql(u8, arg, "-x")) {
            mode = .ShortHex;
        } else if (std.mem.eql(u8, arg, "-v")) {
            verbose = true;
        } else if (std.mem.eql(u8, arg, "-n")) {
            i += 1;
            if (i >= argv.len) {
                eprintf("hexdump: -n requires value\n", .{});
                return 1;
            }
            length = parseSize(argv[i]) catch {
                eprintf("hexdump: invalid length '{s}'\n", .{argv[i]});
                return 1;
            };
            use_length = true;
        } else if (std.mem.eql(u8, arg, "-s")) {
            i += 1;
            if (i >= argv.len) {
                eprintf("hexdump: -s requires value\n", .{});
                return 1;
            }
            skip = parseSize(argv[i]) catch {
                eprintf("hexdump: invalid skip '{s}'\n", .{argv[i]});
                return 1;
            };
        } else if (arg.len > 0 and arg[0] == '-') {
            eprintf("usage: hexdump [-bcdoxC] [-n length] [-s offset] [-v] [file ...]\n", .{});
            return 1;
        } else {
            files.append(arg) catch {};
        }
    }

    var offset: u64 = 0;
    var remaining: u64 = length;
    var rc: c_int = 0;

    if (files.items.len == 0) {
        const ok = hexdumpFd(constants.fd.stdin, "-", mode, verbose, &offset,
            if (use_length) &remaining else null, &skip);
        if (!ok) rc = 1;
    } else {
        for (files.items) |path| {
            const zpath = std.cstr.addNullByte(std.heap.page_allocator, path) catch return 1;
            defer std.heap.page_allocator.free(zpath);
            const fd = fs.openZ(zpath, constants.open.O_RDONLY, null) catch {
                eprintf("hexdump: {s}: open failed\n", .{path});
                rc = 1;
                continue;
            };
            defer fs.close(fd) catch {};
            const ok = hexdumpFd(fd, path, mode, verbose, &offset,
                if (use_length) &remaining else null, &skip);
            if (!ok) rc = 1;
            if (use_length and remaining == 0) break;
        }
    }

    var tail_buf: [16]u8 = undefined;
    const tail = std.fmt.bufPrint(&tail_buf, "{x:0>8}\n", .{offset}) catch "";
    _ = io.writeAll(constants.fd.stdout, tail) catch {};
    return rc;
}

pub export fn app_main(argc: c_int, argv: [*]?[*:0]u8) callconv(.C) c_int {
    var it = args.Args.init(argc, argv);
    _ = it.next();
    var list = std.ArrayList([]const u8).init(std.heap.page_allocator);
    defer list.deinit();
    while (it.next()) |p| {
        list.append(args.zslice(p)) catch {};
    }
    return hexdumpMain(list.items);
}
