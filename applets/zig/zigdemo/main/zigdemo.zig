const mg = @import("magnolia");

fn printArgv(argc: c_int, argv: [*]?[*:0]u8) void {
    var it = mg.args.Args.init(argc, argv);
    var idx: c_int = 0;
    while (it.next()) |argz| : (idx += 1) {
        const arg = mg.args.zslice(argz);
        _ = mg.sys.printf("argv[%d]=%.*s\n", idx, @as(c_int, @intCast(arg.len)), arg.ptr);
    }
}

fn demoCwd() void {
    var buf: [128]u8 = undefined;
    const cwd = mg.fs.getcwd(buf[0..]) catch {
        const e = mg.errno.get();
        _ = mg.sys.printf("getcwd failed errno=%d %s\n", e, mg.errno.strerrorZ(e));
        return;
    };
    _ = mg.sys.printf("cwd=%s\n", cwd);
}

fn demoListBin() void {
    _ = mg.sys.printf("listing /bin:\n");
    var d = mg.dir.Dir.open("/bin") catch {
        const e = mg.errno.get();
        _ = mg.sys.printf("opendir(/bin) failed errno=%d %s\n", e, mg.errno.strerrorZ(e));
        return;
    };
    defer d.close();

    var count: usize = 0;
    while (d.next()) |ent| {
        const name: [*:0]const u8 = @ptrCast(&ent.d_name[0]);
        _ = mg.sys.printf("  %s\n", name);
        count += 1;
        if (count >= 32) break;
    }
}

fn demoFlashRoundtrip() void {
    const path: [*:0]const u8 = "/flash/zigdemo.txt";
    const flags = mg.constants.open.O_WRONLY | mg.constants.open.O_CREAT | mg.constants.open.O_TRUNC;

    const fd = mg.fs.openZ(path, flags, 0o644) catch {
        const e = mg.errno.get();
        _ = mg.sys.printf("open(%s) failed errno=%d %s\n", path, e, mg.errno.strerrorZ(e));
        return;
    };
    defer mg.fs.close(fd) catch {};

    mg.fs.writeAll(fd, "Hello from zigdemo!\n") catch {
        const e = mg.errno.get();
        _ = mg.sys.printf("writeAll failed errno=%d %s\n", e, mg.errno.strerrorZ(e));
        return;
    };
    _ = mg.sys.printf("wrote %s\n", path);
}

pub export fn app_main(argc: c_int, argv: [*]?[*:0]u8) callconv(.C) c_int {
    _ = mg.sys.printf("zigdemo: Magnolia Zig SDK demo\n");
    _ = mg.sys.printf("argc=%d\n", argc);

    printArgv(argc, argv);
    demoCwd();
    demoListBin();
    demoFlashRoundtrip();

    return 0;
}
