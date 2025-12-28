#![no_std]

extern crate alloc;

use alloc::string::{String, ToString};
use magnolia_applet::errno::{Error, Result};
use magnolia_applet::{eprint, eprintln, entry, Args};
use magnolia_applet::{dir, fs, io, sys};

const ENOENT: i32 = 2;
const EISDIR: i32 = 21;

#[derive(Clone, Copy)]
struct RmOpts {
    force: bool,
    interactive: bool,
    recursive: bool,
    dir: bool,
}

fn usage() {
    eprintln!("usage: rm [-f | -i] [-dPRr] file ...");
}

fn prompt(path: &str) -> bool {
    let _ = io::write_all(sys::STDERR_FILENO, b"rm: remove ");
    let _ = io::write_all(sys::STDERR_FILENO, path.as_bytes());
    let _ = io::write_all(sys::STDERR_FILENO, b"? ");
    let mut buf = [0u8; 4];
    match io::read(sys::STDIN_FILENO, &mut buf) {
        Ok(n) if n > 0 => {
            let b = buf[0];
            b == b'y' || b == b'Y'
        }
        _ => false,
    }
}

fn join_path(base: &str, name: &str) -> String {
    let mut out = String::with_capacity(base.len() + 1 + name.len());
    out.push_str(base);
    if !base.ends_with('/') {
        out.push('/');
    }
    out.push_str(name);
    out
}

fn remove_dir_recursive(path: &str, opts: RmOpts) -> Result<()> {
    let mut dir_handle = dir::Dir::open(path)?;
    while let Some(entry) = dir_handle.next()? {
        let name = String::from_utf8_lossy(&entry.name).to_string();
        if name == "." || name == ".." {
            continue;
        }
        let child = join_path(path, &name);
        remove_path(&child, opts)?;
    }
    fs::remove(path)
}

fn remove_path(path: &str, opts: RmOpts) -> Result<()> {
    let is_dir = fs::is_dir(path).unwrap_or(false);
    if is_dir {
        if !opts.recursive && !opts.dir {
            return Err(Error { errno: EISDIR });
        }
        if opts.interactive && !opts.force && !prompt(path) {
            return Ok(());
        }
        if opts.recursive {
            return remove_dir_recursive(path, opts);
        }
        return fs::remove(path);
    }

    if opts.interactive && !opts.force && !prompt(path) {
        return Ok(());
    }
    match fs::unlink(path) {
        Ok(()) => Ok(()),
        Err(err) => {
            if opts.force && err.errno == ENOENT {
                Ok(())
            } else {
                Err(err)
            }
        }
    }
}

fn main_inner(args: Args) -> i32 {
    let mut opts = RmOpts {
        force: false,
        interactive: false,
        recursive: false,
        dir: false,
    };
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
        for b in s.as_bytes().iter().skip(1) {
            match *b {
                b'f' => {
                    opts.force = true;
                    opts.interactive = false;
                }
                b'i' => {
                    opts.interactive = true;
                    opts.force = false;
                }
                b'r' | b'R' => opts.recursive = true,
                b'd' => opts.dir = true,
                _ => {
                    usage();
                    return 1;
                }
            }
        }
        i += 1;
    }

    if i >= args.len() {
        usage();
        return 1;
    }

    let mut failed = false;
    while i < args.len() {
        let arg = args.get(i).unwrap();
        let path = match arg.to_str() {
            Ok(v) => v,
            Err(_) => {
                eprintln!("rm: invalid path");
                failed = true;
                i += 1;
                continue;
            }
        };
        if let Err(err) = remove_path(path, opts) {
            if !(opts.force && err.errno == ENOENT) {
                eprint!("rm: {}: errno={}\n", path, err.errno);
                failed = true;
            }
        }
        i += 1;
    }
    if failed { 1 } else { 0 }
}

entry!(main_inner);
