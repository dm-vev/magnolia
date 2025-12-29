const std = @import("std");
const mg = @import("magnolia");

const args = mg.args;
const constants = mg.constants;
const errno = mg.errno;
const fs = mg.fs;
const io = mg.io;
const mem = mg.mem;

const NumberMode = enum {
    none,
    all,
    nonblank,
};

const CatOptions = struct {
    number_mode: NumberMode,
    squeeze_blank: bool,
    show_ends: bool,
    show_tabs: bool,
    show_nonprint: bool,
    unbuffered: bool,
};

const CatState = struct {
    // Persist across files to match BSD cat numbering and blank-line behavior.
    line_no: u64,
    at_line_start: bool,
    blank_run: bool,
};

const OutBuf = struct {
    fd: c_int,
    unbuffered: bool,
    buf: [4096]u8,
    len: usize,
};

fn eprintf(comptime fmt: []const u8, list: anytype) void {
    var buf: [256]u8 = undefined;
    const msg = std.fmt.bufPrint(&buf, fmt, list) catch return;
    _ = io.writeAll(constants.fd.stderr, msg) catch {};
}

fn usage() void {
    eprintf("usage: cat [-benstuv] [file ...]\n", .{});
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

fn outFlush(out: *OutBuf) errno.PosixError!void {
    if (out.len == 0) return;
    try writeAllRetry(out.fd, out.buf[0..out.len]);
    out.len = 0;
}

fn outWrite(out: *OutBuf, buf: []const u8) errno.PosixError!void {
    if (buf.len == 0) return;
    if (out.unbuffered) {
        return writeAllRetry(out.fd, buf);
    }
    if (buf.len >= out.buf.len) {
        try outFlush(out);
        return writeAllRetry(out.fd, buf);
    }
    if (out.len + buf.len > out.buf.len) {
        try outFlush(out);
    }
    std.mem.copyForwards(u8, out.buf[out.len .. out.len + buf.len], buf);
    out.len += buf.len;
}

fn emitLineNumber(out: *OutBuf, line_no: *u64) errno.PosixError!void {
    var buf: [32]u8 = undefined;
    const msg = std.fmt.bufPrint(&buf, "{d:>6}\t", .{line_no.*}) catch return error.Io;
    line_no.* += 1;
    return outWrite(out, msg);
}

fn emitVisibleByte(out: *OutBuf, b: u8, opt: *const CatOptions) errno.PosixError!void {
    if (b == '\t') {
        if (opt.show_tabs) {
            return outWrite(out, "^I");
        }
        var tab: [1]u8 = .{b};
        return outWrite(out, &tab);
    }
    if (!opt.show_nonprint) {
        var ch: [1]u8 = .{b};
        return outWrite(out, &ch);
    }

    var buf: [4]u8 = undefined;
    var len: usize = 0;
    var ch = b;
    if ((ch & 0x80) != 0) {
        buf[len] = 'M';
        len += 1;
        buf[len] = '-';
        len += 1;
        ch &= 0x7f;
    }
    if (ch < 0x20) {
        buf[len] = '^';
        len += 1;
        buf[len] = ch + 0x40;
        len += 1;
        return outWrite(out, buf[0..len]);
    }
    if (ch == 0x7f) {
        buf[len] = '^';
        len += 1;
        buf[len] = '?';
        len += 1;
        return outWrite(out, buf[0..len]);
    }
    buf[len] = ch;
    len += 1;
    return outWrite(out, buf[0..len]);
}

fn needsProcessing(opt: *const CatOptions) bool {
    return opt.number_mode != .none or opt.squeeze_blank or opt.show_ends or opt.show_tabs or opt.show_nonprint;
}

fn reportStdoutError(err: anyerror) bool {
    eprintf("cat: stdout: {s}\n", .{errnoMessage(err)});
    return false;
}

fn catPlain(fd: c_int, name: []const u8) bool {
    var buf: [512]u8 = undefined;
    while (true) {
        const n = readRetry(fd, &buf) catch |err| {
            eprintf("cat: {s}: {s}\n", .{name, errnoMessage(err)});
            return false;
        };
        if (n == 0) break;
        if (writeAllRetry(constants.fd.stdout, buf[0..n])) |_| {} else |err| {
            return reportStdoutError(err);
        }
    }
    return true;
}

fn catStream(fd: c_int, name: []const u8, opt: *const CatOptions, state: *CatState) bool {
    var out = OutBuf{
        .fd = constants.fd.stdout,
        .unbuffered = opt.unbuffered,
        .buf = undefined,
        .len = 0,
    };
    var buf: [512]u8 = undefined;
    while (true) {
        const n = readRetry(fd, &buf) catch |err| {
            const err_msg = errnoMessage(err);
            if (outFlush(&out)) |_| {} else |flush_err| {
                return reportStdoutError(flush_err);
            }
            eprintf("cat: {s}: {s}\n", .{name, err_msg});
            return false;
        };
        if (n == 0) break;
        var i: usize = 0;
        while (i < n) : (i += 1) {
            const b = buf[i];
            if (state.at_line_start) {
                if (b == '\n') {
                    if (opt.squeeze_blank and state.blank_run) {
                        continue;
                    }
                    if (opt.number_mode == .all) {
                        if (emitLineNumber(&out, &state.line_no)) |_| {} else |err| {
                            return reportStdoutError(err);
                        }
                    }
                    if (opt.show_ends) {
                        if (outWrite(&out, "$")) |_| {} else |err| {
                            return reportStdoutError(err);
                        }
                    }
                    if (outWrite(&out, "\n")) |_| {} else |err| {
                        return reportStdoutError(err);
                    }
                    state.blank_run = true;
                    state.at_line_start = true;
                    continue;
                }
                if (opt.squeeze_blank) {
                    state.blank_run = false;
                }
                if (opt.number_mode != .none) {
                    if (emitLineNumber(&out, &state.line_no)) |_| {} else |err| {
                        return reportStdoutError(err);
                    }
                }
                state.at_line_start = false;
            }

            if (b == '\n') {
                if (opt.show_ends) {
                    if (outWrite(&out, "$")) |_| {} else |err| {
                        return reportStdoutError(err);
                    }
                }
                if (outWrite(&out, "\n")) |_| {} else |err| {
                    return reportStdoutError(err);
                }
                state.at_line_start = true;
                continue;
            }
            if (emitVisibleByte(&out, b, opt)) |_| {} else |err| {
                return reportStdoutError(err);
            }
        }
    }
    if (outFlush(&out)) |_| {} else |err| {
        return reportStdoutError(err);
    }
    return true;
}

fn catFd(fd: c_int, name: []const u8, opt: *const CatOptions, state: *CatState) bool {
    if (!needsProcessing(opt)) {
        return catPlain(fd, name);
    }
    return catStream(fd, name, opt, state);
}

pub export fn app_main(argc: c_int, argv: [*]?[*:0]u8) callconv(.C) c_int {
    var it = args.Args.init(argc, argv);
    _ = it.next();

    var opt = CatOptions{
        .number_mode = .none,
        .squeeze_blank = false,
        .show_ends = false,
        .show_tabs = false,
        .show_nonprint = false,
        .unbuffered = false,
    };
    var state = CatState{
        .line_no = 1,
        .at_line_start = true,
        .blank_run = false,
    };

    var files = std.ArrayList([]const u8).init(mem.allocator);
    defer files.deinit();

    while (it.next()) |argz| {
        const arg = args.zslice(argz);
        if (arg.len == 0) continue;
        if (std.mem.eql(u8, arg, "--")) {
            while (it.next()) |rest| {
                const v = args.zslice(rest);
                if (files.append(v)) |_| {} else |_| {
                    eprintf("cat: out of memory\n", .{});
                    return 1;
                }
            }
            break;
        }
        if (arg[0] != '-' or arg.len == 1) {
            if (files.append(arg)) |_| {} else |_| {
                eprintf("cat: out of memory\n", .{});
                return 1;
            }
            continue;
        }
        var ok = true;
        for (arg[1..]) |ch| {
            switch (ch) {
                'b' => opt.number_mode = .nonblank,
                'e' => {
                    opt.show_ends = true;
                    opt.show_nonprint = true;
                },
                'n' => {
                    if (opt.number_mode != .nonblank) {
                        opt.number_mode = .all;
                    }
                },
                's' => opt.squeeze_blank = true,
                't' => {
                    opt.show_tabs = true;
                    opt.show_nonprint = true;
                },
                'u' => opt.unbuffered = true,
                'v' => opt.show_nonprint = true,
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

    if (files.items.len == 0) {
        return if (catFd(constants.fd.stdin, "-", &opt, &state)) 0 else 1;
    }

    var failed = false;
    for (files.items) |path| {
        if (std.mem.eql(u8, path, "-")) {
            if (!catFd(constants.fd.stdin, "-", &opt, &state)) {
                failed = true;
            }
            continue;
        }
        const zpath = mem.allocator.dupeZ(u8, path) catch {
            eprintf("cat: out of memory\n", .{});
            failed = true;
            continue;
        };
        defer mem.allocator.free(zpath);
        const fd = fs.openZ(zpath, constants.open.O_RDONLY, null) catch |err| {
            eprintf("cat: {s}: {s}\n", .{path, errnoMessage(err)});
            failed = true;
            continue;
        };
        if (!catFd(fd, path, &opt, &state)) {
            failed = true;
        }
        fs.close(fd) catch {};
    }
    return if (failed) 1 else 0;
}
