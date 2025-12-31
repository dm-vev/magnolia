#![no_std]

//! BSD-compatible `cp` applet.
//!
//! Reference: FreeBSD `cp(1)`.
//! Notes:
//! - Only a subset of flags are implemented (-R/-r, -f, -i, -p).
//! - Errors are reported using errno values to preserve existing output format.

extern crate alloc;

use alloc::string::{String, ToString};
use magnolia_applet::errno::{Error, Result};
use magnolia_applet::{eprintln, entry, Args};
use magnolia_applet::{dir, fs, io, sys};

const ENOENT: i32 = 2;
const EEXIST: i32 = 17;

#[derive(Clone, Copy)]
struct CpOpts {
    recursive: bool,
    interactive: bool,
    force: bool,
}

fn usage() {
    eprintln!("usage: cp [-R [-H | -L | -P]] [-f | -i] [-p] source ... target");
}

fn prompt(path: &str) -> bool {
    let _ = io::write_all(sys::STDERR_FILENO, b"cp: overwrite ");
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

fn basename(path: &str) -> &str {
    let trimmed = path.trim_end_matches('/');
    trimmed.rsplit('/').next().unwrap_or(trimmed)
}

fn copy_file(src: &str, dst: &str, opts: CpOpts) -> Result<()> {
    let src_file = fs::File::open(src, sys::O_RDONLY, 0)?;
    if opts.force {
        match fs::remove(dst) {
            Ok(()) => {}
            Err(err) if err.errno == ENOENT => {}
            Err(err) => return Err(err),
        }
    }
    let flags = sys::O_WRONLY | sys::O_CREAT | sys::O_TRUNC;
    let dst_file = if opts.interactive {
        // Use O_EXCL to avoid overwriting when the file appears between check and open.
        match fs::File::open(dst, flags | sys::O_EXCL, 0o666) {
            Ok(file) => file,
            Err(err) if err.errno == EEXIST => {
                if !prompt(dst) {
                    return Ok(());
                }
                fs::File::open(dst, flags, 0o666)?
            }
            Err(err) => return Err(err),
        }
    } else {
        fs::File::open(dst, flags, 0o666)?
    };
    let mut buf = [0u8; 512];
    loop {
        let n = src_file.read(&mut buf)?;
        if n == 0 {
            break;
        }
        dst_file.write_all(&buf[..n])?;
    }
    Ok(())
}

fn copy_dir(src: &str, dst: &str, opts: CpOpts) -> Result<()> {
    if !opts.recursive {
        return Err(Error { errno: 21 });
    }
    if !fs::exists(dst) {
        fs::mkdir(dst, 0o777)?;
    } else if !fs::is_dir(dst)? {
        return Err(Error { errno: 20 });
    }
    let mut dir_handle = dir::Dir::open(src)?;
    while let Some(entry) = dir_handle.next()? {
        let name = String::from_utf8_lossy(&entry.name).to_string();
        if name == "." || name == ".." {
            continue;
        }
        let src_child = join_path(src, &name);
        let dst_child = join_path(dst, &name);
        copy_path(&src_child, &dst_child, opts)?;
    }
    Ok(())
}

fn copy_path(src: &str, dst: &str, opts: CpOpts) -> Result<()> {
    match fs::is_dir(src) {
        Ok(true) => copy_dir(src, dst, opts),
        Ok(false) => copy_file(src, dst, opts),
        Err(err) => Err(err),
    }
}

fn main_inner(args: Args) -> i32 {
    let mut opts = CpOpts {
        recursive: false,
        interactive: false,
        force: false,
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
                b'R' | b'r' => opts.recursive = true,
                b'f' => {
                    opts.force = true;
                    opts.interactive = false;
                }
                b'i' => {
                    opts.interactive = true;
                    opts.force = false;
                }
                b'p' => {}
                _ => {
                    usage();
                    return 1;
                }
            }
        }
        i += 1;
    }

    let remaining = args.len().saturating_sub(i);
    if remaining < 2 {
        usage();
        return 1;
    }

    let dest = args.get(args.len() - 1).unwrap();
    let dest_str = match dest.to_str() {
        Ok(v) => v,
        Err(_) => {
            eprintln!("cp: invalid target");
            return 1;
        }
    };
    let dest_is_dir = match fs::is_dir(dest_str) {
        Ok(v) => v,
        Err(err) if err.errno == ENOENT => {
            // Missing destination is allowed for a single source copy.
            false
        }
        Err(err) => {
            eprintln!("cp: {}: errno={}", dest_str, err.errno);
            return 1;
        }
    };
    if remaining > 2 && !dest_is_dir {
        eprintln!("cp: target is not a directory");
        return 1;
    }

    let mut failed = false;
    while i + 1 < args.len() {
        let src = args.get(i).unwrap();
        let src_str = match src.to_str() {
            Ok(v) => v,
            Err(_) => {
                eprintln!("cp: invalid source");
                failed = true;
                i += 1;
                continue;
            }
        };
        let dst_path = if dest_is_dir {
            join_path(dest_str, basename(src_str))
        } else {
            dest_str.to_string()
        };
        if let Err(err) = copy_path(src_str, &dst_path, opts) {
            eprintln!("cp: {}: errno={}", src_str, err.errno);
            failed = true;
        }
        i += 1;
    }
    if failed { 1 } else { 0 }
}

entry!(main_inner);
