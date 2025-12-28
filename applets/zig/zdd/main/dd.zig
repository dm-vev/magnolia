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

fn skipInput(fd: c_int, blocks: u64, ibs: usize) bool {
    if (blocks == 0) return true;
    const offset: sys.off_t = @intCast(@as(i64, @intCast(blocks)) * @as(i64, @intCast(ibs)));
    if (sys.lseek(fd, offset, constants.seek.cur) >= 0) return true;
    var buf = mem.allocSlice(u8, ibs) catch return false;
    defer mem.freeSlice(u8, buf);
    var left = blocks;
    while (left > 0) : (left -= 1) {
        const n = fs.readSome(fd, buf) catch return false;
        if (n == 0) return false;
    }
    return true;
}

fn seekOutput(fd: c_int, blocks: u64, obs: usize) bool {
    if (blocks == 0) return true;
    const offset: sys.off_t = @intCast(@as(i64, @intCast(blocks)) * @as(i64, @intCast(obs)));
    if (sys.lseek(fd, offset, constants.seek.cur) >= 0) return true;
    var zeros = mem.allocSlice(u8, obs) catch return false;
    defer mem.freeSlice(u8, zeros);
    @memset(zeros, 0);
    var left = blocks;
    while (left > 0) : (left -= 1) {
        fs.writeAll(fd, zeros) catch return false;
    }
    return true;
}

fn ddMain(argv: []const []const u8) c_int {
    var ifile: ?[]const u8 = null;
    var ofile: ?[]const u8 = null;
    var ibs: u64 = 512;
    var obs: u64 = 512;
    var bs: u64 = 0;
    var count: u64 = 0;
    var skip: u64 = 0;
    var seek: u64 = 0;
    var use_count = false;
    var noerror = false;
    var sync = false;
    var notrunc = false;
    var status_none = false;

    for (argv) |arg| {
        const eq = std.mem.indexOfScalar(u8, arg, '=') orelse {
            eprintf("dd: invalid argument '{s}'\n", .{arg});
            return 1;
        };
        const key = arg[0..eq];
        const val = arg[eq + 1 ..];
        if (std.mem.eql(u8, key, "if")) {
            ifile = val;
        } else if (std.mem.eql(u8, key, "of")) {
            ofile = val;
        } else if (std.mem.eql(u8, key, "ibs")) {
            ibs = parseSize(val) catch {
                eprintf("dd: invalid ibs '{s}'\n", .{val});
                return 1;
            };
        } else if (std.mem.eql(u8, key, "obs")) {
            obs = parseSize(val) catch {
                eprintf("dd: invalid obs '{s}'\n", .{val});
                return 1;
            };
        } else if (std.mem.eql(u8, key, "bs")) {
            bs = parseSize(val) catch {
                eprintf("dd: invalid bs '{s}'\n", .{val});
                return 1;
            };
        } else if (std.mem.eql(u8, key, "count")) {
            count = parseSize(val) catch {
                eprintf("dd: invalid count '{s}'\n", .{val});
                return 1;
            };
            use_count = true;
        } else if (std.mem.eql(u8, key, "skip")) {
            skip = parseSize(val) catch {
                eprintf("dd: invalid skip '{s}'\n", .{val});
                return 1;
            };
        } else if (std.mem.eql(u8, key, "seek")) {
            seek = parseSize(val) catch {
                eprintf("dd: invalid seek '{s}'\n", .{val});
                return 1;
            };
        } else if (std.mem.eql(u8, key, "conv")) {
            var it = std.mem.splitScalar(u8, val, ',');
            while (it.next()) |part| {
                if (part.len == 0) continue;
                if (std.mem.eql(u8, part, "noerror")) {
                    noerror = true;
                } else if (std.mem.eql(u8, part, "sync")) {
                    sync = true;
                } else if (std.mem.eql(u8, part, "notrunc")) {
                    notrunc = true;
                } else {
                    eprintf("dd: unsupported conv '{s}'\n", .{part});
                    return 1;
                }
            }
        } else if (std.mem.eql(u8, key, "status")) {
            if (std.mem.eql(u8, val, "none")) {
                status_none = true;
            } else {
                eprintf("dd: unsupported status '{s}'\n", .{val});
                return 1;
            }
        } else {
            eprintf("dd: invalid argument '{s}'\n", .{arg});
            return 1;
        }
    }

    if (bs > 0) {
        ibs = bs;
        obs = bs;
    }
    if (ibs == 0 or obs == 0) {
        eprintf("dd: block size cannot be zero\n", .{});
        return 1;
    }

    var in_fd: c_int = constants.fd.stdin;
    var out_fd: c_int = constants.fd.stdout;
    if (ifile) |path| {
        const zpath = std.cstr.addNullByte(std.heap.page_allocator, path) catch return 1;
        defer std.heap.page_allocator.free(zpath);
        in_fd = fs.openZ(zpath, constants.open.O_RDONLY, null) catch {
            eprintf("dd: {s}: open failed\n", .{path});
            return 1;
        };
        defer fs.close(in_fd) catch {};
    }
    if (ofile) |path| {
        const zpath = std.cstr.addNullByte(std.heap.page_allocator, path) catch return 1;
        defer std.heap.page_allocator.free(zpath);
        var flags: c_int = constants.open.O_WRONLY | constants.open.O_CREAT;
        if (!notrunc) {
            flags |= constants.open.O_TRUNC;
        }
        out_fd = fs.openZ(zpath, flags, 0o666) catch {
            eprintf("dd: {s}: open failed\n", .{path});
            return 1;
        };
        defer fs.close(out_fd) catch {};
    }

    if (!skipInput(in_fd, skip, @intCast(ibs))) {
        eprintf("dd: skip failed\n", .{});
        return 1;
    }
    if (!seekOutput(out_fd, seek, @intCast(obs))) {
        eprintf("dd: seek failed\n", .{});
        return 1;
    }

    var ibuf = mem.allocSlice(u8, @intCast(ibs)) catch return 1;
    defer mem.freeSlice(u8, ibuf);
    var obuf = mem.allocSlice(u8, @intCast(obs)) catch return 1;
    defer mem.freeSlice(u8, obuf);

    var obuf_len: usize = 0;
    var in_full: u64 = 0;
    var in_part: u64 = 0;
    var out_full: u64 = 0;
    var out_part: u64 = 0;
    var blocks: u64 = 0;

    while (!use_count or blocks < count) : (blocks += 1) {
        const n = fs.readSome(in_fd, ibuf) catch {
            if (noerror) {
                eprintf("dd: read error\n", .{});
                continue;
            }
            eprintf("dd: read error\n", .{});
            break;
        };
        if (n == 0) break;
        if (n == ibs) {
            in_full += 1;
        } else {
            in_part += 1;
        }
        var chunk_len = n;
        if (sync and chunk_len < ibs) {
            @memset(ibuf[chunk_len..], 0);
            chunk_len = ibs;
        }
        if (obs == ibs) {
            fs.writeAll(out_fd, ibuf[0..chunk_len]) catch {
                eprintf("dd: write error\n", .{});
                break;
            };
            if (chunk_len == obs) {
                out_full += 1;
            } else {
                out_part += 1;
            }
        } else {
            var off: usize = 0;
            while (off < chunk_len) {
                const space = obs - obuf_len;
                const take = if (chunk_len - off < space) chunk_len - off else space;
                std.mem.copy(u8, obuf[obuf_len..obuf_len + take], ibuf[off..off + take]);
                obuf_len += take;
                off += take;
                if (obuf_len == obs) {
                    fs.writeAll(out_fd, obuf) catch {
                        eprintf("dd: write error\n", .{});
                        return 1;
                    };
                    out_full += 1;
                    obuf_len = 0;
                }
            }
        }
    }

    if (obuf_len > 0) {
        if (fs.writeAll(out_fd, obuf[0..obuf_len])) |_| {
            out_part += 1;
        } else |_| {
            eprintf("dd: write error\n", .{});
        }
    }

    if (!status_none) {
        eprintf("{d}+{d} records in\n", .{ in_full, in_part });
        eprintf("{d}+{d} records out\n", .{ out_full, out_part });
    }

    return 0;
}

pub export fn app_main(argc: c_int, argv: [*]?[*:0]u8) callconv(.C) c_int {
    var it = args.Args.init(argc, argv);
    _ = it.next();
    var list = std.ArrayList([]const u8).init(std.heap.page_allocator);
    defer list.deinit();
    while (it.next()) |p| {
        list.append(args.zslice(p)) catch {};
    }
    return ddMain(list.items);
}
