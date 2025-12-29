const std = @import("std");
const mg = @import("magnolia");

const args = mg.args;
const constants = mg.constants;
const errno = mg.errno;
const fs = mg.fs;
const io = mg.io;

const HeaderMode = enum {
    auto,
    quiet,
    verbose,
};

const IoErrorKind = enum {
    ok,
    read,
    write,
};

const IoError = struct {
    kind: IoErrorKind = .ok,
    err: ?anyerror = null,
};

fn eprintf(comptime fmt: []const u8, list: anytype) void {
    var buf: [256]u8 = undefined;
    const msg = std.fmt.bufPrint(&buf, fmt, list) catch return;
    _ = io.writeAll(constants.fd.stderr, msg) catch {};
}

fn usage() void {
    eprintf("usage: head [-n lines | -c bytes] [-qv] [file ...]\n", .{});
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

fn parseCount(s: []const u8) ?usize {
    if (s.len == 0) return null;
    if (s[0] == '+' or s[0] == '-') return null;

    var value: usize = 0;
    for (s) |ch| {
        if (!std.ascii.isDigit(ch)) return null;
        const digit: usize = ch - '0';
        if (value > (std.math.maxInt(usize) - digit) / 10) return null;
        value = value * 10 + digit;
    }
    return value;
}

fn copyNBytes(fd: c_int, limit: usize, io_err: *IoError) bool {
    var remaining = limit;
    var buf: [512]u8 = undefined;
    while (remaining > 0) {
        const want = if (remaining < buf.len) remaining else buf.len;
        const n = readRetry(fd, buf[0..want]) catch |err| {
            io_err.* = .{ .kind = .read, .err = err };
            return false;
        };
        if (n == 0) return true;
        if (writeAllRetry(constants.fd.stdout, buf[0..n])) |_| {} else |err| {
            io_err.* = .{ .kind = .write, .err = err };
            return false;
        }
        remaining -= n;
    }
    return true;
}

fn copyNLines(fd: c_int, limit: usize, io_err: *IoError) bool {
    var buf: [512]u8 = undefined;
    var lines: usize = 0;
    while (lines < limit) {
        const n = readRetry(fd, &buf) catch |err| {
            io_err.* = .{ .kind = .read, .err = err };
            return false;
        };
        if (n == 0) return true;

        var out_len: usize = 0;
        var i: usize = 0;
        while (i < n) : (i += 1) {
            out_len += 1;
            if (buf[i] == '\n') {
                lines += 1;
                if (lines >= limit) break;
            }
        }
        if (out_len > 0) {
            if (writeAllRetry(constants.fd.stdout, buf[0..out_len])) |_| {} else |err| {
                io_err.* = .{ .kind = .write, .err = err };
                return false;
            }
        }
        if (lines >= limit) return true;
    }
    return true;
}

fn headFd(fd: c_int, by_bytes: bool, limit: usize, io_err: *IoError) bool {
    io_err.* = .{ .kind = .ok, .err = null };
    if (limit == 0) return true;
    return if (by_bytes) copyNBytes(fd, limit, io_err) else copyNLines(fd, limit, io_err);
}

fn printHeader(path: []const u8, first: bool) anyerror!void {
    // BSD prints a blank line between consecutive file headers.
    if (!first) {
        try writeAllRetry(constants.fd.stdout, "\n");
    }
    try writeAllRetry(constants.fd.stdout, "==> ");
    try writeAllRetry(constants.fd.stdout, path);
    try writeAllRetry(constants.fd.stdout, " <==\n");
}

pub export fn app_main(argc: c_int, argv: [*]?[*:0]u8) callconv(.C) c_int {
    const argc_usize: usize = if (argc > 0) @intCast(argc) else 0;
    if (argc_usize == 0) return 0;

    const argv_slice = argv[0..argc_usize];

    var lines: usize = 10;
    var bytes: usize = 0;
    var by_bytes = false;
    var header_mode: HeaderMode = .auto;

    var file_index: usize = argc_usize;
    var i: usize = 1;
    while (i < argc_usize) : (i += 1) {
        const arg_ptr = argv_slice[i] orelse continue;
        const arg = args.zslice(@as([*:0]const u8, arg_ptr));
        if (std.mem.eql(u8, arg, "--")) {
            file_index = i + 1;
            break;
        }
        if (arg.len == 0 or arg[0] != '-' or arg.len == 1) {
            file_index = i;
            break;
        }
        if (std.ascii.isDigit(arg[1])) {
            // BSD legacy: -N is treated as "first N lines".
            const count = parseCount(arg[1..]) orelse {
                eprintf("head: illegal line count -- {s}\n", .{arg[1..]});
                usage();
                return 1;
            };
            lines = count;
            by_bytes = false;
            continue;
        }

        var done = false;
        var j: usize = 1;
        while (j < arg.len) : (j += 1) {
            const ch = arg[j];
            switch (ch) {
                'n', 'c' => {
                    var value: []const u8 = undefined;
                    if (j + 1 < arg.len) {
                        value = arg[j + 1 ..];
                        j = arg.len;
                    } else {
                        if (i + 1 >= argc_usize) {
                            eprintf("head: option requires an argument -- {c}\n", .{ch});
                            usage();
                            return 1;
                        }
                        i += 1;
                        const next_ptr = argv_slice[i] orelse {
                            eprintf("head: option requires an argument -- {c}\n", .{ch});
                            usage();
                            return 1;
                        };
                        value = args.zslice(@as([*:0]const u8, next_ptr));
                    }
                    const count = parseCount(value) orelse {
                        if (ch == 'n') {
                            eprintf("head: illegal line count -- {s}\n", .{value});
                        } else {
                            eprintf("head: illegal byte count -- {s}\n", .{value});
                        }
                        usage();
                        return 1;
                    };
                    if (ch == 'n') {
                        lines = count;
                        by_bytes = false;
                    } else {
                        bytes = count;
                        by_bytes = true;
                    }
                    done = true;
                },
                'q' => header_mode = .quiet,
                'v' => header_mode = .verbose,
                else => {
                    eprintf("head: illegal option -- {c}\n", .{ch});
                    usage();
                    return 1;
                },
            }
            if (done) break;
        }
    }

    const limit = if (by_bytes) bytes else lines;
    if (file_index >= argc_usize) {
        var io_err = IoError{};
        if (!headFd(constants.fd.stdin, by_bytes, limit, &io_err)) {
            const err_msg = if (io_err.err) |e| errnoMessage(e) else "I/O error";
            if (io_err.kind == .write) {
                eprintf("head: stdout: {s}\n", .{err_msg});
            } else {
                eprintf("head: -: {s}\n", .{err_msg});
            }
            return 1;
        }
        return 0;
    }

    const file_count = argc_usize - file_index;
    var need_separator = false;
    var failed = false;
    var idx = file_index;
    while (idx < argc_usize) : (idx += 1) {
        const arg_ptr = argv_slice[idx] orelse continue;
        const path_z = @as([*:0]const u8, arg_ptr);
        const path = args.zslice(path_z);

        const show_header = switch (header_mode) {
            .verbose => true,
            .quiet => false,
            .auto => file_count > 1,
        };
        if (show_header) {
            if (printHeader(path, !need_separator)) |_| {} else |err| {
                eprintf("head: stdout: {s}\n", .{errnoMessage(err)});
                return 1;
            };
            need_separator = true;
        }

        var fd: c_int = constants.fd.stdin;
        if (!std.mem.eql(u8, path, "-")) {
            fd = fs.openZ(path_z, constants.open.O_RDONLY, null) catch |err| {
                eprintf("head: {s}: {s}\n", .{path, errnoMessage(err)});
                failed = true;
                continue;
            };
        }

        var io_err = IoError{};
        const ok = headFd(fd, by_bytes, limit, &io_err);
        if (fd != constants.fd.stdin) {
            _ = fs.close(fd) catch {};
        }
        if (!ok) {
            const err_msg = if (io_err.err) |e| errnoMessage(e) else "I/O error";
            if (io_err.kind == .write) {
                eprintf("head: stdout: {s}\n", .{err_msg});
                return 1;
            }
            eprintf("head: {s}: {s}\n", .{path, err_msg});
            failed = true;
        }
    }
    return if (failed) 1 else 0;
}
