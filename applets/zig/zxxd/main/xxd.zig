const std = @import("std");
const mg = @import("magnolia");

const fs = mg.fs;
const io = mg.io;
const mem = mg.mem;
const args = mg.args;
const constants = mg.constants;
const sys = mg.sys;

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

fn hexValue(c: u8) ?u8 {
    return if (c >= '0' and c <= '9') c - '0'
    else if (c >= 'a' and c <= 'f') c - 'a' + 10
    else if (c >= 'A' and c <= 'F') c - 'A' + 10
    else null;
}

fn pushHexByte(list: *std.ArrayList(u8), b: u8, upper: bool) void {
    const map = if (upper) "0123456789ABCDEF" else "0123456789abcdef";
    _ = list.append(map[(b >> 4) & 0xF]) catch {};
    _ = list.append(map[b & 0xF]) catch {};
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
        const n = fs.readSome(fd, buf[0..want]) catch |err| {
            eprintf("xxd: read failed: {s}\n", .{@errorName(err)});
            return false;
        };
        if (n == 0) return false;
        skip.* -= @intCast(n);
    }
    return true;
}

fn reverseStream(fd: c_int) bool {
    var line: [256]u8 = undefined;
    var line_len: usize = 0;
    var half: ?u8 = null;
    var outbuf: [256]u8 = undefined;
    var out_len: usize = 0;

    var buf: [128]u8 = undefined;
    while (true) {
        const n = fs.readSome(fd, buf[0..]) catch |err| {
            eprintf("xxd: read failed: {s}\n", .{@errorName(err)});
            return false;
        };
        if (n == 0) break;
        for (buf[0..n]) |c| {
            if (c == '\n' or c == '\r') {
                const slice = line[0..line_len];
                var start: usize = 0;
                if (std.mem.indexOfScalar(u8, slice, ':')) |pos| {
                    if (pos <= 8) start = pos + 1;
                }
                for (slice[start..]) |ch| {
                    if (hexValue(ch)) |v| {
                        if (half) |h| {
                            outbuf[out_len] = (h << 4) | v;
                            out_len += 1;
                            half = null;
                            if (out_len == outbuf.len) {
                                _ = io.writeAll(constants.fd.stdout, outbuf[0..out_len]) catch {};
                                out_len = 0;
                            }
                        } else {
                            half = v;
                        }
                    }
                }
                line_len = 0;
                continue;
            }
            if (line_len + 1 < line.len) {
                line[line_len] = c;
                line_len += 1;
            }
        }
    }

    if (line_len > 0) {
        const slice = line[0..line_len];
        var start: usize = 0;
        if (std.mem.indexOfScalar(u8, slice, ':')) |pos| {
            if (pos <= 8) start = pos + 1;
        }
        for (slice[start..]) |ch| {
            if (hexValue(ch)) |v| {
                if (half) |h| {
                    outbuf[out_len] = (h << 4) | v;
                    out_len += 1;
                    half = null;
                    if (out_len == outbuf.len) {
                        _ = io.writeAll(constants.fd.stdout, outbuf[0..out_len]) catch {};
                        out_len = 0;
                    }
                } else {
                    half = v;
                }
            }
        }
    }

    if (out_len > 0) {
        _ = io.writeAll(constants.fd.stdout, outbuf[0..out_len]) catch {};
    }
    return true;
}

fn xxdForward(fd: c_int, skip: u64, length: u64, use_length: bool, columns: usize,
              group: usize, plain: bool, upper: bool) bool {
    var skip_left = skip;
    if (!skipBytes(fd, &skip_left)) return false;
    var offset: u64 = 0;
    var buf = std.ArrayList(u8).init(mem.allocator);
    defer buf.deinit();
    buf.resize(columns) catch {
        eprintf("xxd: out of memory\n", .{});
        return false;
    };

    var remaining = length;
    while (true) {
        var want = columns;
        if (use_length and remaining < want) want = @intCast(remaining);
        const n = fs.readSome(fd, buf.items[0..want]) catch |err| {
            eprintf("xxd: read failed: {s}\n", .{@errorName(err)});
            return false;
        };
        if (n == 0) break;
        if (plain) {
            var line = std.ArrayList(u8).init(mem.allocator);
            defer line.deinit();
            var i: usize = 0;
            while (i < n) : (i += 1) {
                pushHexByte(&line, buf.items[i], upper);
                if (i + 1 == n or ((i + 1) % columns) == 0) {
                    _ = line.append('\n') catch {};
                }
            }
            _ = io.writeAll(constants.fd.stdout, line.items) catch {};
        } else {
            var line = std.ArrayList(u8).init(mem.allocator);
            defer line.deinit();
            var header: [16]u8 = undefined;
            const h = std.fmt.bufPrint(&header, "{x:0>8}: ", .{offset}) catch "";
            _ = line.appendSlice(h) catch {};
            var i: usize = 0;
            while (i < columns) : (i += 1) {
                if (i < n) {
                    pushHexByte(&line, buf.items[i], upper);
                } else {
                    _ = line.append(' ') catch {};
                    _ = line.append(' ') catch {};
                }
                if (group > 0 and ((i + 1) % group) == 0) {
                    _ = line.append(' ') catch {};
                }
            }
            _ = line.append(' ') catch {};
            for (buf.items[0..n]) |b| {
                _ = line.append(if (b >= 0x20 and b <= 0x7e) b else '.') catch {};
            }
            _ = line.append('\n') catch {};
            _ = io.writeAll(constants.fd.stdout, line.items) catch {};
        }
        offset += n;
        if (use_length) {
            remaining -= n;
            if (remaining == 0) break;
        }
    }
    return true;
}

fn xxdMain(argv: []const []const u8) c_int {
    var columns: usize = 16;
    var group: usize = 2;
    var length: u64 = 0;
    var skip: u64 = 0;
    var use_length = false;
    var plain = false;
    var reverse = false;
    var upper = false;

    var files = std.ArrayList([]const u8).init(mem.allocator);
    defer files.deinit();

    var i: usize = 0;
    while (i < argv.len) : (i += 1) {
        const arg = argv[i];
        if (std.mem.eql(u8, arg, "-g")) {
            i += 1;
            if (i >= argv.len) {
                eprintf("xxd: -g requires value\n", .{});
                return 1;
            }
            group = std.fmt.parseInt(usize, argv[i], 10) catch 2;
        } else if (std.mem.eql(u8, arg, "-c")) {
            i += 1;
            if (i >= argv.len) {
                eprintf("xxd: -c requires value\n", .{});
                return 1;
            }
            columns = std.fmt.parseInt(usize, argv[i], 10) catch 16;
            if (columns == 0) columns = 16;
            if (columns > 256) columns = 256;
        } else if (std.mem.eql(u8, arg, "-l")) {
            i += 1;
            if (i >= argv.len) {
                eprintf("xxd: -l requires value\n", .{});
                return 1;
            }
            length = parseSize(argv[i]) catch {
                eprintf("xxd: invalid length '{s}'\n", .{argv[i]});
                return 1;
            };
            use_length = true;
        } else if (std.mem.eql(u8, arg, "-s")) {
            i += 1;
            if (i >= argv.len) {
                eprintf("xxd: -s requires value\n", .{});
                return 1;
            }
            skip = parseSize(argv[i]) catch {
                eprintf("xxd: invalid offset '{s}'\n", .{argv[i]});
                return 1;
            };
        } else if (std.mem.eql(u8, arg, "-p")) {
            plain = true;
        } else if (std.mem.eql(u8, arg, "-r")) {
            reverse = true;
        } else if (std.mem.eql(u8, arg, "-u")) {
            upper = true;
        } else if (arg.len > 0 and arg[0] == '-') {
            eprintf("usage: xxd [-g n] [-c n] [-l len] [-s offset] [-p] [-r] [-u] [file]\n", .{});
            return 1;
        } else {
            files.append(arg) catch {};
        }
    }

    if (plain and columns == 16) {
        columns = 30;
    }

    const path = if (files.items.len > 0) files.items[0] else null;
    var fd: c_int = constants.fd.stdin;
    if (path) |p| {
        const zpath = mem.allocator.dupeZ(u8, p) catch return 1;
        defer mem.allocator.free(zpath);
        fd = fs.openZ(zpath, constants.open.O_RDONLY, null) catch {
            eprintf("xxd: {s}: open failed\n", .{p});
            return 1;
        };
        defer fs.close(fd) catch {};
    }

    const ok = if (reverse) reverseStream(fd) else xxdForward(fd, skip, length, use_length, columns, group, plain, upper);
    if (!ok) {
        eprintf("xxd: error\n", .{});
        return 1;
    }
    return 0;
}

pub export fn app_main(argc: c_int, argv: [*]?[*:0]u8) callconv(.C) c_int {
    var it = args.Args.init(argc, argv);
    _ = it.next();
    var list = std.ArrayList([]const u8).init(mem.allocator);
    defer list.deinit();
    while (it.next()) |p| {
        list.append(args.zslice(p)) catch {};
    }
    return xxdMain(list.items);
}
