const std = @import("std");
const mg = @import("magnolia");

const args = mg.args;
const constants = mg.constants;
const io = mg.io;

fn writeAll(bytes: []const u8) void {
    _ = io.writeAll(constants.fd.stdout, bytes) catch {};
}

fn usage() void {
    _ = io.writeAll(constants.fd.stdout, "usage: clear [--help] [--version]\n") catch {};
}

pub export fn app_main(argc: c_int, argv: [*]?[*:0]u8) callconv(.C) c_int {
    var it = args.Args.init(argc, argv);
    _ = it.next();
    if (it.next()) |arg| {
        const s = std.mem.span(arg);
        if (std.mem.eql(u8, s, "--help")) {
            usage();
            return 0;
        }
        if (std.mem.eql(u8, s, "--version")) {
            writeAll("clear (Magnolia coreutils 0.1)\n");
            return 0;
        }
    }

    writeAll("\x1b[2J\x1b[H");
    return 0;
}
