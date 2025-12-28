const std = @import("std");
const mg = @import("magnolia");

const args = mg.args;
const constants = mg.constants;
const fs = mg.fs;
const io = mg.io;
const mem = mg.mem;

fn eprintf(comptime fmt: []const u8, list: anytype) void {
    var buf: [256]u8 = undefined;
    const msg = std.fmt.bufPrint(&buf, fmt, list) catch return;
    _ = io.writeAll(constants.fd.stderr, msg) catch {};
}

fn writeByte(b: u8) void {
    var buf: [1]u8 = .{b};
    _ = io.writeAll(constants.fd.stdout, &buf) catch {};
}

fn writeBytes(bytes: []const u8) void {
    _ = io.writeAll(constants.fd.stdout, bytes) catch {};
}

fn emitLineNumber(num: usize) void {
    var buf: [16]u8 = undefined;
    const msg = std.fmt.bufPrint(&buf, "{d:>6}\t", .{num}) catch return;
    writeBytes(msg);
}

fn emitVisualByte(b: u8, show_tabs: bool, show_nonprint: bool) void {
    if (b == '\t') {
        if (show_tabs) {
            writeBytes("^I");
        } else {
            writeByte('\t');
        }
        return;
    }
    if (!show_nonprint) {
        writeByte(b);
        return;
    }

    var ch = b;
    if (ch >= 0x80) {
        writeBytes("M-");
        ch &= 0x7f;
    }
    if (ch < 0x20) {
        writeByte('^');
        writeByte(ch + 0x40);
        return;
    }
    if (ch == 0x7f) {
        writeBytes("^?");
        return;
    }
    writeByte(ch);
}

fn cat_fd(fd: c_int, name: []const u8, number: bool, number_nonblank: bool, squeeze_blank: bool, show_ends: bool, show_tabs: bool, show_nonprint: bool) bool {
    var buf: [512]u8 = undefined;
    var line_no: usize = 1;
    var at_line_start = true;
    var last_blank = false;
    while (true) {
        const n = fs.readSome(fd, &buf) catch {
            eprintf("cat: {s}: read failed\n", .{name});
            return false;
        };
        if (n == 0) break;
        var i: usize = 0;
        while (i < n) : (i += 1) {
            const b = buf[i];
            if (at_line_start) {
                if (b == '\n') {
                    if (squeeze_blank and last_blank) {
                        continue;
                    }
                    if (number and !number_nonblank) {
                        emitLineNumber(line_no);
                        line_no += 1;
                    }
                    if (show_ends) writeByte('$');
                    writeByte('\n');
                    last_blank = true;
                    at_line_start = true;
                    continue;
                }
                if (number_nonblank) {
                    emitLineNumber(line_no);
                    line_no += 1;
                } else if (number) {
                    emitLineNumber(line_no);
                    line_no += 1;
                }
                at_line_start = false;
                last_blank = false;
            }

            if (b == '\n') {
                if (show_ends) writeByte('$');
                writeByte('\n');
                at_line_start = true;
                last_blank = true;
                continue;
            }
            emitVisualByte(b, show_tabs, show_nonprint);
        }
    }
    return true;
}

fn usage() void {
    eprintf("usage: cat [-benstuv] [file ...]\n", .{});
}

pub export fn app_main(argc: c_int, argv: [*]?[*:0]u8) callconv(.C) c_int {
    var it = args.Args.init(argc, argv);
    _ = it.next();

    var number = false;
    var number_nonblank = false;
    var squeeze_blank = false;
    var show_ends = false;
    var show_tabs = false;
    var show_nonprint = false;

    var files = std.ArrayList([]const u8).init(mem.allocator);
    defer files.deinit();

    while (it.next()) |argz| {
        const arg = args.zslice(argz);
        if (arg.len == 0) continue;
        if (std.mem.eql(u8, arg, "--")) {
            while (it.next()) |rest| {
                const v = args.zslice(rest);
                files.append(v) catch {};
            }
            break;
        }
        if (arg[0] != '-' or arg.len == 1) {
            files.append(arg) catch {};
            continue;
        }
        var ok = true;
        for (arg[1..]) |ch| {
            switch (ch) {
                'b' => number_nonblank = true,
                'e' => {
                    show_ends = true;
                    show_nonprint = true;
                },
                'n' => number = true,
                's' => squeeze_blank = true,
                't' => {
                    show_tabs = true;
                    show_nonprint = true;
                },
                'u' => {},
                'v' => show_nonprint = true,
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
        if (!cat_fd(constants.fd.stdin, "-", number, number_nonblank, squeeze_blank, show_ends, show_tabs, show_nonprint)) {
            return 1;
        }
        return 0;
    }

    var failed = false;
    for (files.items) |path| {
        if (std.mem.eql(u8, path, "-")) {
            if (!cat_fd(constants.fd.stdin, "-", number, number_nonblank, squeeze_blank, show_ends, show_tabs, show_nonprint)) {
                failed = true;
            }
            continue;
        }
        const zpath = mem.allocator.dupeZ(u8, path) catch {
            failed = true;
            continue;
        };
        defer mem.allocator.free(zpath);
        const fd = fs.openZ(zpath, constants.open.O_RDONLY, null) catch {
            eprintf("cat: {s}: open failed\n", .{path});
            failed = true;
            continue;
        };
        if (!cat_fd(fd, path, number, number_nonblank, squeeze_blank, show_ends, show_tabs, show_nonprint)) {
            failed = true;
        }
        fs.close(fd) catch {};
    }
    return if (failed) 1 else 0;
}
