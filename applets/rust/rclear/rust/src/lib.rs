#![no_std]

use magnolia_applet::{entry, Args};
use magnolia_applet::{io, sys};

fn usage() {
    let _ = io::write_all(sys::STDOUT_FILENO, b"usage: clear [--help] [--version]\n");
}

fn main_inner(args: Args) -> i32 {
    if args.len() == 2 {
        let arg = args.get(1).unwrap();
        let s = arg.to_str().unwrap_or("");
        if s == "--help" {
            usage();
            return 0;
        }
        if s == "--version" {
            let _ = io::write_all(sys::STDOUT_FILENO, b"clear (Magnolia coreutils 0.1)\n");
            return 0;
        }
    }

    if io::write_all(sys::STDOUT_FILENO, b"\x1b[2J\x1b[H").is_err() {
        return 1;
    }
    0
}

entry!(main_inner);
