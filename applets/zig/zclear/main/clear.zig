const std = @import("std");
const mg = @import("magnolia");

const args = mg.args;
const constants = mg.constants;
const errno = mg.errno;
const io = mg.io;

extern fn getenv(name: [*:0]const u8) ?[*:0]const u8;

const clear_seq = "\x1b[H\x1b[2J";
const clear_scrollback = "\x1b[3J";

const TermEntry = struct {
    name: []const u8,
    clear_scrollback: bool,
};

const terms = [_]TermEntry{
    .{ .name = "xterm", .clear_scrollback = true },
    .{ .name = "xterm-256color", .clear_scrollback = true },
    .{ .name = "xterm-color", .clear_scrollback = true },
    .{ .name = "screen", .clear_scrollback = true },
    .{ .name = "screen-256color", .clear_scrollback = true },
    .{ .name = "tmux", .clear_scrollback = true },
    .{ .name = "tmux-256color", .clear_scrollback = true },
    .{ .name = "vt100", .clear_scrollback = false },
    .{ .name = "ansi", .clear_scrollback = false },
    .{ .name = "linux", .clear_scrollback = false },
};

fn writeErr(msg: []const u8) void {
    _ = io.writeAll(constants.fd.stderr, msg) catch {};
}

fn usage() void {
    writeErr("usage: clear [-T term] [-V] [-x]\n");
}

fn findTerm(term: []const u8) ?*const TermEntry {
    for (terms) |*entry| {
        if (std.mem.eql(u8, term, entry.name)) {
            return entry;
        }
    }
    return null;
}

fn emitWriteError() void {
    const err = errno.get();
    const msg = std.mem.span(errno.strerrorZ(err));
    var buf: [128]u8 = undefined;
    const len = std.fmt.bufPrint(&buf, "clear: stdout: {s}\n", .{msg}) catch return;
    writeErr(buf[0..len]);
}

fn emitUnknownTerm(term: []const u8) void {
    var buf: [256]u8 = undefined;
    const len = std.fmt.bufPrint(&buf, "clear: unknown terminal type {s}\n", .{term}) catch return;
    writeErr(buf[0..len]);
}

pub export fn app_main(argc: c_int, argv: [*]?[*:0]u8) callconv(.C) c_int {
    var it = args.Args.init(argc, argv);
    _ = it.next();

    var term_override: ?[]const u8 = null;
    var no_scrollback = false;
    var show_version = false;

    while (it.next()) |arg| {
        const s = std.mem.span(arg);
        if (std.mem.eql(u8, s, "--")) {
            if (it.next() != null) {
                usage();
                return 1;
            }
            break;
        }
        if (s.len >= 2 and s[0] == '-') {
            if (std.mem.eql(u8, s, "-V")) {
                show_version = true;
                continue;
            }
            if (std.mem.eql(u8, s, "-x")) {
                no_scrollback = true;
                continue;
            }
            if (std.mem.startsWith(u8, s, "-T")) {
                if (s.len > 2) {
                    term_override = s[2..];
                    continue;
                }
                if (it.next()) |next| {
                    const term = std.mem.span(next);
                    if (term.len == 0) {
                        writeErr("clear: option requires an argument -- T\n");
                        usage();
                        return 1;
                    }
                    term_override = term;
                    continue;
                }
                writeErr("clear: option requires an argument -- T\n");
                usage();
                return 1;
            }
            const opt = if (s.len > 1) s[1] else '?';
            var buf: [64]u8 = undefined;
            const len = std.fmt.bufPrint(&buf, "clear: illegal option -- {c}\n", .{opt}) catch return 1;
            writeErr(buf[0..len]);
            usage();
            return 1;
        }
        usage();
        return 1;
    }

    if (show_version) {
        _ = io.writeAll(constants.fd.stdout, "clear (Magnolia coreutils 0.1)\n") catch {};
        return 0;
    }

    var term: ?[]const u8 = term_override;
    if (term == null) {
        if (getenv(c"TERM")) |env| {
            term = std.mem.span(env);
        }
    }
    if (term == null or term.?.len == 0) {
        writeErr("clear: TERM environment variable not set.\n");
        return 1;
    }

    const entry = findTerm(term.?) orelse {
        emitUnknownTerm(term.?);
        return 1;
    };

    if (io.writeAll(constants.fd.stdout, clear_seq)) |_| {} else |_| {
        emitWriteError();
        return 1;
    }
    if (entry.clear_scrollback and !no_scrollback) {
        if (io.writeAll(constants.fd.stdout, clear_scrollback)) |_| {} else |_| {
            emitWriteError();
            return 1;
        }
    }
    return 0;
}
