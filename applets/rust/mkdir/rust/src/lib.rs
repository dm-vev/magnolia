#![no_std]

extern crate alloc;

use alloc::borrow::ToOwned;
use alloc::ffi::CString;
use alloc::string::String;
use core::ffi::CStr;

use magnolia_applet::dir;
use magnolia_applet::errno::{Error, Result, EINVAL, ENOENT};
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
const S_ISUID: sys::mode_t = 0o4000;
const S_ISGID: sys::mode_t = 0o2000;
const S_ISVTX: sys::mode_t = 0o1000;

const WHO_USER: sys::mode_t = S_IRUSR | S_IWUSR | S_IXUSR;
const WHO_GROUP: sys::mode_t = S_IRGRP | S_IWGRP | S_IXGRP;
const WHO_OTHER: sys::mode_t = S_IROTH | S_IWOTH | S_IXOTH;
const WHO_ALL: sys::mode_t = WHO_USER | WHO_GROUP | WHO_OTHER;

const EEXIST: i32 = 17;
const ENOTDIR: i32 = 20;

extern "C" {
    fn chmod(path: *const sys::c_char, mode: sys::mode_t) -> sys::c_int;
}

fn strerror(errno: i32) -> String {
    unsafe {
        let ptr = sys::strerror(errno);
        if ptr.is_null() {
            return "unknown error".to_owned();
        }
        let cstr = CStr::from_ptr(ptr);
        String::from_utf8_lossy(cstr.to_bytes()).into_owned()
    }
}

fn chmod_path(path: &str, mode: sys::mode_t) -> Result<()> {
    let cpath = CString::new(path).map_err(|_| Error { errno: EINVAL })?;
    let rc = unsafe { chmod(cpath.as_ptr(), mode) };
    if rc != 0 {
        return Err(Error::last());
    }
    Ok(())
}

fn parse_mode_octal(s: &str) -> Option<sys::mode_t> {
    if s.is_empty() || !s.bytes().all(|b| (b'0'..=b'7').contains(&b)) {
        return None;
    }
    u32::from_str_radix(s, 8).ok().map(|v| v as sys::mode_t)
}

fn who_mask(c: u8) -> sys::mode_t {
    match c {
        b'u' => WHO_USER,
        b'g' => WHO_GROUP,
        b'o' => WHO_OTHER,
        b'a' => WHO_ALL,
        _ => 0,
    }
}

fn class_shift(c: u8) -> i32 {
    match c {
        b'u' => 6,
        b'g' => 3,
        b'o' => 0,
        _ => 0,
    }
}

fn class_bits(mode: sys::mode_t, c: u8) -> sys::mode_t {
    match c {
        b'u' => mode & WHO_USER,
        b'g' => mode & WHO_GROUP,
        b'o' => mode & WHO_OTHER,
        _ => 0,
    }
}

fn shift_bits(bits: sys::mode_t, from: i32, to: i32) -> sys::mode_t {
    if from > to {
        bits >> (from - to)
    } else if from < to {
        bits << (to - from)
    } else {
        bits
    }
}

fn perm_bits_for_who(who: sys::mode_t, perm: u8) -> sys::mode_t {
    let mut bits = 0;
    if perm == b'r' {
        if (who & WHO_USER) != 0 {
            bits |= S_IRUSR;
        }
        if (who & WHO_GROUP) != 0 {
            bits |= S_IRGRP;
        }
        if (who & WHO_OTHER) != 0 {
            bits |= S_IROTH;
        }
        return bits;
    }
    if perm == b'w' {
        if (who & WHO_USER) != 0 {
            bits |= S_IWUSR;
        }
        if (who & WHO_GROUP) != 0 {
            bits |= S_IWGRP;
        }
        if (who & WHO_OTHER) != 0 {
            bits |= S_IWOTH;
        }
        return bits;
    }
    if perm == b'x' {
        if (who & WHO_USER) != 0 {
            bits |= S_IXUSR;
        }
        if (who & WHO_GROUP) != 0 {
            bits |= S_IXGRP;
        }
        if (who & WHO_OTHER) != 0 {
            bits |= S_IXOTH;
        }
        return bits;
    }
    0
}

fn copy_bits(mode: sys::mode_t, from: u8, who: sys::mode_t) -> sys::mode_t {
    let src = class_bits(mode, from);
    let src_shift = class_shift(from);
    let mut out = 0;
    if (who & WHO_USER) != 0 {
        out |= shift_bits(src, src_shift, class_shift(b'u'));
    }
    if (who & WHO_GROUP) != 0 {
        out |= shift_bits(src, src_shift, class_shift(b'g'));
    }
    if (who & WHO_OTHER) != 0 {
        out |= shift_bits(src, src_shift, class_shift(b'o'));
    }
    out
}

fn parse_mode_symbolic(s: &str) -> Option<sys::mode_t> {
    if s.is_empty() {
        return None;
    }
    let mut mode: sys::mode_t = WHO_ALL;
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
        let cur_mode = mode;
        let mut perms: sys::mode_t = 0;
        let mut special: sys::mode_t = 0;
        let mut saw_perm = false;
        while i < bytes.len() && bytes[i] != b',' {
            let p = bytes[i];
            saw_perm = true;
            match p {
                b'r' | b'w' | b'x' => {
                    perms |= perm_bits_for_who(who, p);
                }
                b'X' => {
                    perms |= perm_bits_for_who(who, b'x');
                }
                b's' => {
                    if (who & WHO_USER) != 0 {
                        special |= S_ISUID;
                    }
                    if (who & WHO_GROUP) != 0 {
                        special |= S_ISGID;
                    }
                }
                b't' => {
                    special |= S_ISVTX;
                }
                b'u' | b'g' | b'o' => {
                    perms |= copy_bits(cur_mode, p, who);
                }
                _ => return None,
            }
            i += 1;
        }
        if op == b'=' {
            let mut clear = who;
            if (who & WHO_USER) != 0 {
                clear |= S_ISUID;
            }
            if (who & WHO_GROUP) != 0 {
                clear |= S_ISGID;
            }
            if (who & WHO_OTHER) != 0 {
                clear |= S_ISVTX;
            }
            mode &= !clear;
        }
        if saw_perm {
            if op == b'+' {
                mode |= perms | special;
            } else if op == b'-' {
                mode &= !(perms | special);
            } else {
                mode |= perms | special;
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

fn mkdir_component(
    path: &str,
    mode: sys::mode_t,
    allow_existing: bool,
    apply_mode: bool,
    require_dir: bool,
) -> Result<()> {
    match fs::mkdir(path, if apply_mode { mode } else { 0o777 }) {
        Ok(()) => {
            if apply_mode {
                chmod_path(path, mode)?;
            }
            Ok(())
        }
        Err(err) => {
            if allow_existing && err.errno == EEXIST {
                match dir::Dir::open(path) {
                    Ok(_) => Ok(()),
                    Err(e) => {
                        if e.errno == ENOTDIR {
                            return Err(Error {
                                errno: if require_dir { ENOTDIR } else { EEXIST },
                            });
                        }
                        Err(e)
                    }
                }
            } else {
                Err(err)
            }
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

fn mkdir_parents(path: &str, mode: sys::mode_t, apply_mode: bool) -> Result<()> {
    let path = trim_trailing_slashes(path);
    if path.is_empty() {
        return Err(Error { errno: ENOENT });
    }
    if path == "/" {
        return Ok(());
    }

    let parts: alloc::vec::Vec<&str> = path.split('/').filter(|p| !p.is_empty()).collect();
    if parts.is_empty() {
        return Ok(());
    }

    let mut current = String::new();
    if path.starts_with('/') {
        current.push('/');
    }
    for (idx, part) in parts.iter().enumerate() {
        if !current.ends_with('/') && !current.is_empty() {
            current.push('/');
        }
        current.push_str(part);
        let is_last = idx + 1 == parts.len();
        if is_last {
            mkdir_component(&current, mode, true, apply_mode, false)?;
        } else {
            mkdir_component(&current, 0o777, true, false, true)?;
        }
    }
    Ok(())
}

fn usage() {
    eprintln!("usage: mkdir [-p] [-m mode] directory ...");
}

fn main_inner(args: Args) -> i32 {
    let mut parents = false;
    let mut mode: sys::mode_t = 0;
    let mut mode_set = false;
    let mut i = 1usize;

    while i < args.len() {
        let arg = match args.get(i) {
            Some(v) => v,
            None => break,
        };
        let s = match arg.to_str() {
            Ok(v) => v,
            Err(_) => break,
        };
        if s == "--" {
            i += 1;
            break;
        }
        if !s.starts_with('-') || s == "-" {
            break;
        }

        let bytes = s.as_bytes();
        let mut j = 1;
        while j < bytes.len() {
            match bytes[j] {
                b'p' => {
                    parents = true;
                    j += 1;
                }
                b'm' => {
                    let val = if j + 1 < bytes.len() {
                        &s[j + 1..]
                    } else {
                        i += 1;
                        if i >= args.len() {
                            eprintln!("mkdir: option requires an argument -- m");
                            usage();
                            return 1;
                        }
                        match args.get(i).and_then(|v| v.to_str().ok()) {
                            Some(v) => v,
                            None => {
                                eprintln!("mkdir: option requires an argument -- m");
                                usage();
                                return 1;
                            }
                        }
                    };
                    match parse_mode(val) {
                        Some(v) => {
                            mode = v;
                            mode_set = true;
                        }
                        None => {
                            eprintln!("mkdir: invalid mode: {}", val);
                            return 1;
                        }
                    }
                    j = bytes.len();
                }
                other => {
                    eprintln!("mkdir: illegal option -- {}", other as char);
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
                eprintln!("mkdir: invalid path");
                failed = true;
                i += 1;
                continue;
            }
        };
        let result = if parents {
            mkdir_parents(path, mode, mode_set)
        } else {
            mkdir_component(path, mode, false, mode_set, false)
        };
        if let Err(err) = result {
            eprintln!("mkdir: {}: {}", path, strerror(err.errno));
            failed = true;
        }
        i += 1;
    }
    if failed { 1 } else { 0 }
}

entry!(main_inner);
