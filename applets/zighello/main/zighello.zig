extern fn write(fd: i32, buf: [*]const u8, len: usize) isize;

pub export fn app_main(argc: i32, argv: [*]?[*:0]u8) callconv(.C) i32 {
    _ = argc;
    _ = argv;

    const msg: []const u8 = "Hello world!\n";
    _ = write(1, msg.ptr, msg.len);
    return 0;
}

