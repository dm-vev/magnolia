#![no_std]

extern crate alloc;

use alloc::string::{String, ToString};
use magnolia_applet::errno::{Error, Result};
use magnolia_applet::{eprintln, entry, Args};
use magnolia_applet::{dir, fs, io, sys};

const ENOENT: i32 = 2;

#[derive(Clone, Copy)]
struct MvOpts {
    interactive: bool,
    force: bool,
    no_overwrite: bool,
}

fn usage() {
    eprintln!("usage: mv [-f | -i | -n] source ... target");
}

fn prompt(path: &str) -> bool {
    let _ = io::write_all(sys::STDERR_FILENO, b"mv: overwrite ");
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

fn remove_dir_recursive(path: &str) -> Result<()> {
    let mut dir_handle = dir::Dir::open(path)?;
    while let Some(entry) = dir_handle.next()? {
        let name = String::from_utf8_lossy(&entry.name).to_string();
        if name == "." || name == ".." {
            continue;
        }
        let child = join_path(path, &name);
        remove_path(&child)?;
    }
    fs::remove(path)
}

fn remove_path(path: &str) -> Result<()> {
    let is_dir = fs::is_dir(path).unwrap_or(false);
    if is_dir {
        return remove_dir_recursive(path);
    }
    fs::unlink(path)
}

fn copy_file(src: &str, dst: &str) -> Result<()> {
    let src_file = fs::File::open(src, sys::O_RDONLY, 0)?;
    let dst_file = fs::File::open(dst, sys::O_WRONLY | sys::O_CREAT | sys::O_TRUNC, 0o666)?;
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

fn copy_dir(src: &str, dst: &str) -> Result<()> {
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
        copy_path(&src_child, &dst_child)?;
    }
    Ok(())
}

fn copy_path(src: &str, dst: &str) -> Result<()> {
    let is_dir = fs::is_dir(src).unwrap_or(false);
    if is_dir {
        return copy_dir(src, dst);
    }
    copy_file(src, dst)
}

fn main_inner(args: Args) -> i32 {
    let mut opts = MvOpts {
        interactive: false,
        force: false,
        no_overwrite: false,
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
                    opts.no_overwrite = false;
                }
                b'i' => {
                    opts.interactive = true;
                    opts.force = false;
                    opts.no_overwrite = false;
                }
                b'n' => {
                    opts.no_overwrite = true;
                    opts.force = false;
                    opts.interactive = false;
                }
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
            eprintln!("mv: invalid target");
            return 1;
        }
    };
    let dest_is_dir = fs::is_dir(dest_str).unwrap_or(false);
    if remaining > 2 && !dest_is_dir {
        eprintln!("mv: target is not a directory");
        return 1;
    }

    let mut failed = false;
    while i + 1 < args.len() {
        let src = args.get(i).unwrap();
        let src_str = match src.to_str() {
            Ok(v) => v,
            Err(_) => {
                eprintln!("mv: invalid source");
                failed = true;
                i += 1;
                continue;
            }
        };
        if !fs::exists(src_str) {
            eprintln!("mv: {}: errno={}", src_str, ENOENT);
            failed = true;
            i += 1;
            continue;
        }
        let dst_path = if dest_is_dir {
            join_path(dest_str, basename(src_str))
        } else {
            dest_str.to_string()
        };
        if src_str == dst_path {
            i += 1;
            continue;
        }
        if fs::exists(&dst_path) {
            if opts.no_overwrite {
                eprintln!("mv: {}: file exists", dst_path);
                failed = true;
                i += 1;
                continue;
            }
            if opts.interactive && !prompt(&dst_path) {
                i += 1;
                continue;
            }
            if opts.force {
                let _ = fs::remove(&dst_path);
            }
        }
        if let Err(err) = copy_path(src_str, &dst_path) {
            eprintln!("mv: {}: errno={}", src_str, err.errno);
            failed = true;
            i += 1;
            continue;
        }
        if let Err(err) = remove_path(src_str) {
            eprintln!("mv: {}: errno={}", src_str, err.errno);
            failed = true;
        }
        i += 1;
    }

    if failed { 1 } else { 0 }
}

entry!(main_inner);
