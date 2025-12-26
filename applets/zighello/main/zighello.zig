const mg = @import("magnolia");

pub export fn app_main(argc: c_int, argv: [*]?[*:0]u8) callconv(.C) c_int {
    _ = argc;
    _ = argv;

    mg.io.puts("Hello from Zig (Magnolia SDK)!");
    return 0;
}
