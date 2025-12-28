#![no_std]

extern crate alloc;

use alloc::string::{String, ToString};
use magnolia_applet::errno::{Error, Result};
use magnolia_applet::{eprintln, entry, Args};
use magnolia_applet::{fs, sys};

const S_IRUSR: sys::mode_t = 0o400;
const S_IWUSR: sys::mode_t = 0o200;
const S_IXUSR: sys::mode_t = 0o100;
const S_IRGRP: sys::mode_t = 0o040;
const S_IWGRP: sys::mode_t = 0o020;
const S_IXGRP: sys::mode_t = 0o010;
const S_IROTH: sys::mode_t = 0o004;
const S_IWOTH: sys::mode_t = 0o002;
const S_IXOTH: sys::mode_t = 0o001;

const EEXIST: i32 = 17;

fn parse_mode_octal(s: &str) -> Option<sys::mode_t> {
    if s.is_empty() {
        return None;
    }
    let mut value: sys::mode_t = 0;
    for b in s.bytes() {
        if !(b'0'..=b'7').contains(&b) {
            return None;
        }
        value = (value << 3) | sys::mode_t::from(b - b'0');
    }
    Some(value)
}

fn who_mask(c: u8) -> sys::mode_t {
    match c {
        b'u' => S_IRUSR | S_IWUSR | S_IXUSR,
        b'g' => S_IRGRP | S_IWGRP | S_IXGRP,
        b'o' => S_IROTH | S_IWOTH | S_IXOTH,
        b'a' => S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH,
        _ => 0,
    }
}

fn perm_bits_for_who(who: u8, perm: u8) -> sys::mode_t {
    match perm {
        b'r' => {
            if who == b'u' {
                S_IRUSR
            } else if who == b'g' {
                S_IRGRP
            } else {
                S_IROTH
            }
        }
        b'w' => {
            if who == b'u' {
                S_IWUSR
            } else if who == b'g' {
                S_IWGRP
            } else {
                S_IWOTH
            }
        }
        b'x' => {
            if who == b'u' {
                S_IXUSR
            } else if who == b'g' {
                S_IXGRP
            } else {
                S_IXOTH
            }
        }
        _ => 0,
    }
}

fn parse_mode_symbolic(s: &str) -> Option<sys::mode_t> {
    if s.is_empty() {
        return None;
    }
    let mut mode: sys::mode_t = 0o777;
    let bytes = s.as_bytes();
    let mut i = 0;
    while i < bytes.len() {
        let mut who: sys::mode_t = 0;
        while i < bytes.len() {
            let b = bytes[i];
            if b == b'u' || b == b'g' || b == b'o' || b == b'a' {
                who |= who_mask(b);
                i += 1;
                continue;
            }
            break;
        }
        if who == 0 {
            who = who_mask(b'a');
        }
        if i >= bytes.len() {
            return None;
        }
        let op = bytes[i];
        if op != b'+' && op != b'-' && op != b'=' {
            return None;
        }
        i += 1;
        let mut perms_u: sys::mode_t = 0;
        let mut perms_g: sys::mode_t = 0;
        let mut perms_o: sys::mode_t = 0;
        while i < bytes.len() {
            let p = bytes[i];
            if p == b'r' || p == b'w' || p == b'x' {
                perms_u |= perm_bits_for_who(b'u', p);
                perms_g |= perm_bits_for_who(b'g', p);
                perms_o |= perm_bits_for_who(b'o', p);
                i += 1;
                continue;
            }
            break;
        }
        if op == b'=' {
            mode &= !who;
        }
        if (who & who_mask(b'u')) != 0 {
            if op == b'+' {
                mode |= perms_u;
            } else if op == b'-' {
                mode &= !perms_u;
            } else {
                mode |= perms_u;
            }
        }
        if (who & who_mask(b'g')) != 0 {
            if op == b'+' {
                mode |= perms_g;
            } else if op == b'-' {
                mode &= !perms_g;
            } else {
                mode |= perms_g;
            }
        }
        if (who & who_mask(b'o')) != 0 {
            if op == b'+' {
                mode |= perms_o;
            } else if op == b'-' {
                mode &= !perms_o;
            } else {
                mode |= perms_o;
            }
        }
        if i < bytes.len() && bytes[i] == b',' {
            i += 1;
        }
    }
    Some(mode)
}

fn parse_mode(s: &str) -> Option<sys::mode_t> {
    parse_mode_octal(s).or_else(|| parse_mode_symbolic(s))
}

fn mkdir_one(path: &str, mode: sys::mode_t, allow_existing: bool) -> Result<()> {
    match fs::mkdir(path, mode) {
        Ok(()) => Ok(()),
        Err(err) => {
            if allow_existing && err.errno == EEXIST {
                if fs::is_dir(path)? {
                    return Ok(());
                }
            }
            Err(err)
        }
    }
}

fn trim_trailing_slashes(path: &str) -> &str {
    let mut end = path.len();
    let bytes = path.as_bytes();
    while end > 1 && bytes[end - 1] == b'/' {
        end -= 1;
    }
    &path[..end]
}

fn mkdir_parents(path: &str, mode: sys::mode_t) -> Result<()> {
    let path = trim_trailing_slashes(path);
    if path.is_empty() {
        return Err(Error { errno: 22 });
    }
    if path == "/" {
        return Ok(());
    }
    let mut current = String::new();
    if path.starts_with('/') {
        current.push('/');
    }
    for part in path.split('/') {
        if part.is_empty() {
            continue;
        }
        if !current.ends_with('/') && !current.is_empty() {
            current.push('/');
        }
        current.push_str(part);
        mkdir_one(&current, mode, true)?;
    }
    Ok(())
}

fn usage() {
    eprintln!("usage: mkdir [-p] [-m mode] dir ...");
}

fn main_inner(args: Args) -> i32 {
    let mut parents = false;
    let mut mode: sys::mode_t = 0o777;
    let mut i = 1usize;
    while i < args.len() {
        let arg = match args.get(i) {
            Some(v) => v,
            None => break,
        };
        let s = arg.to_str().unwrap_or("");
        if s == "--" {
            i += 1;
            break;
        }
        if !s.starts_with('-') || s == "-" {
            break;
        }
        if s == "-p" {
            parents = true;
            i += 1;
            continue;
        }
        if s.starts_with("-m") {
            let val = if s.len() > 2 {
                &s[2..]
            } else {
                i += 1;
                if i >= args.len() {
                    usage();
                    return 1;
                }
                args.get(i).and_then(|v| v.to_str().ok()).unwrap_or("")
            };
            match parse_mode(val) {
                Some(v) => mode = v,
                None => {
                    eprintln!("mkdir: invalid mode '{}'", val);
                    return 1;
                }
            }
            i += 1;
            continue;
        }
        usage();
        return 1;
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
                eprintln!("mkdir: invalid path");
                failed = true;
                i += 1;
                continue;
            }
        };
        let result = if parents {
            mkdir_parents(path, mode)
        } else {
            mkdir_one(path, mode, false)
        };
        if let Err(err) = result {
            eprintln!("mkdir: {}: errno={}", path, err.errno);
            failed = true;
        }
        i += 1;
    }
    if failed { 1 } else { 0 }
}

entry!(main_inner);
