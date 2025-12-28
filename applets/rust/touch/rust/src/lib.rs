#![no_std]

extern crate alloc;

use magnolia_applet::errno::Result;
use magnolia_applet::{eprintln, entry, Args};
use magnolia_applet::{fs, sys};

fn usage() {
    eprintln!("usage: touch [-c] FILE...");
}

fn touch_one(path: &str, no_create: bool) -> Result<()> {
    if fs::exists(path) {
        let _ = fs::File::open(path, sys::O_RDONLY, 0);
        return Ok(());
    }
    if no_create {
        return Ok(());
    }
    let _ = fs::File::open(path, sys::O_WRONLY | sys::O_CREAT, 0o666);
    Ok(())
}

fn main_inner(args: Args) -> i32 {
    let mut no_create = false;
    let mut i = 1usize;
    while i < args.len() {
        let arg = args.get(i).unwrap();
        let s = arg.to_str().unwrap_or("");
        if s == "--" {
            i += 1;
            break;
        }
        if !s.starts_with('-') || s == "-" {
            break;
        }
        if s == "-c" {
            no_create = true;
            i += 1;
            continue;
        }
        if s == "-a" || s == "-m" {
            i += 1;
            continue;
        }
        usage();
        return 1;
    }

    if i >= args.len() {
        eprintln!("touch: missing file operand");
        return 1;
    }

    let mut failed = false;
    while i < args.len() {
        let arg = args.get(i).unwrap();
        let path = match arg.to_str() {
            Ok(v) => v,
            Err(_) => {
                eprintln!("touch: invalid path");
                failed = true;
                i += 1;
                continue;
            }
        };
        if let Err(err) = touch_one(path, no_create) {
            eprintln!("touch: {}: errno={}", path, err.errno);
            failed = true;
        }
        i += 1;
    }
    if failed { 1 } else { 0 }
}

entry!(main_inner);
