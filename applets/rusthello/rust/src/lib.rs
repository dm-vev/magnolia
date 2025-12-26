#![no_std]

fn main(args: magnolia_applet::Args) -> i32 {
    magnolia_applet::println!("Hello from Magnolia Rust SDK!");
    magnolia_applet::println!("argc={}", args.len());
    for (i, arg) in args.iter().enumerate() {
        match arg.to_str() {
            Ok(s) => magnolia_applet::println!("argv[{}]={}", i, s),
            Err(_) => magnolia_applet::println!("argv[{}]=<non-utf8>", i),
        }
    }
    0
}

magnolia_applet::entry!(main);

