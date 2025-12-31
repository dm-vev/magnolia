#![no_std]

extern crate alloc;

use alloc::string::{String, ToString};
use alloc::vec;
use alloc::vec::Vec;
use magnolia_applet::errno::{Error, EINTR};
use magnolia_applet::fs;
use magnolia_applet::io;
use magnolia_applet::sys;
use magnolia_applet::Args;

fn parse_token(input: &str) -> Option<(u64, &str)> {
    if input.is_empty() {
        return None;
    }
    let bytes = input.as_bytes();
    let mut idx = 0;
    let mut radix = 10u32;
    if bytes.len() >= 2 && bytes[0] == b'0' && (bytes[1] == b'x' || bytes[1] == b'X') {
        radix = 16;
        idx = 2;
    }
    let start = idx;
    while idx < bytes.len() {
        let b = bytes[idx];
        let ok = if radix == 16 {
            b.is_ascii_hexdigit()
        } else {
            b.is_ascii_digit()
        };
        if !ok {
            break;
        }
        idx += 1;
    }
    if idx == start {
        return None;
    }
    let num_str = &input[start..idx];
    let mut value = u64::from_str_radix(num_str, radix).ok()?;
    let mut rest = &input[idx..];
    if let Some(first) = rest.as_bytes().first().copied() {
        let mult = match first {
            b'b' => 512u64,
            b'k' | b'K' => 1024u64,
            b'm' | b'M' => 1024u64 * 1024u64,
            b'g' | b'G' => 1024u64 * 1024u64 * 1024u64,
            _ => 1u64,
        };
        if mult != 1 {
            value = value.checked_mul(mult)?;
            rest = &rest[1..];
        }
    }
    Some((value, rest))
}

fn parse_size(input: &str) -> Option<u64> {
    let mut rest = input;
    let mut total = 1u64;
    loop {
        let (value, next) = parse_token(rest)?;
        total = total.checked_mul(value)?;
        if next.is_empty() {
            return Some(total);
        }
        let b = next.as_bytes()[0];
        if b == b'x' || b == b'*' {
            rest = &next[1..];
            continue;
        }
        return None;
    }
}

fn write_all(fd: i32, mut buf: &[u8]) -> Result<(), Error> {
    while !buf.is_empty() {
        let n = unsafe { sys::write(fd, buf.as_ptr().cast(), buf.len()) };
        if n < 0 {
            let err = Error::last();
            if err.errno == EINTR {
                continue;
            }
            return Err(err);
        }
        if n == 0 {
            return Err(Error { errno: 5 });
        }
        buf = &buf[n as usize..];
    }
    Ok(())
}

fn read_retry(fd: i32, buf: &mut [u8]) -> Result<usize, Error> {
    loop {
        match io::read(fd, buf) {
            Ok(read) => return Ok(read),
            Err(err) if err.errno == EINTR => continue,
            Err(err) => return Err(err),
        }
    }
}

fn skip_input(fd: i32, blocks: u64, ibs: usize) -> bool {
    if blocks == 0 {
        return true;
    }
    // Avoid off_t overflow; fall back to read-and-discard when too large.
    if let Some(bytes) = blocks.checked_mul(ibs as u64) {
        if bytes <= sys::off_t::MAX as u64 {
            let offset = bytes as sys::off_t;
            let rc = unsafe { sys::lseek(fd, offset, sys::SEEK_CUR) };
            if rc >= 0 {
                return true;
            }
        }
    }
    let mut buf = vec![0u8; ibs];
    let mut left = blocks;
    while left > 0 {
        match read_retry(fd, &mut buf) {
            Ok(n) => {
                if n == 0 {
                    // EOF while skipping is not an error; stop skipping and proceed.
                    return true;
                }
            }
            Err(_) => return false,
        }
        left -= 1;
    }
    true
}

fn seek_output(fd: i32, blocks: u64, obs: usize) -> bool {
    if blocks == 0 {
        return true;
    }
    // Avoid off_t overflow; fall back to zero writes when too large.
    if let Some(bytes) = blocks.checked_mul(obs as u64) {
        if bytes <= sys::off_t::MAX as u64 {
            let offset = bytes as sys::off_t;
            let rc = unsafe { sys::lseek(fd, offset, sys::SEEK_CUR) };
            if rc >= 0 {
                return true;
            }
        }
    }
    let zeros = vec![0u8; obs];
    let mut left = blocks;
    while left > 0 {
        if write_all(fd, &zeros).is_err() {
            return false;
        }
        left -= 1;
    }
    true
}

fn dd_main(args: &[String]) -> i32 {
    let mut ifile: Option<String> = None;
    let mut ofile: Option<String> = None;
    let mut ibs = 512u64;
    let mut obs = 512u64;
    let mut bs = 0u64;
    let mut count = 0u64;
    let mut skip = 0u64;
    let mut seek = 0u64;
    let mut use_count = false;
    let mut noerror = false;
    let mut sync = false;
    let mut notrunc = false;
    let mut status_none = false;

    for arg in args {
        let (key, val) = match arg.split_once('=') {
            Some(v) => v,
            None => {
                magnolia_applet::eprintln!("dd: invalid argument '{}'", arg);
                return 1;
            }
        };
        match key {
            "if" => ifile = Some(val.to_string()),
            "of" => ofile = Some(val.to_string()),
            "ibs" => match parse_size(val) {
                Some(v) => {
                    if v > usize::MAX as u64 {
                        magnolia_applet::eprintln!("dd: invalid ibs '{}'", val);
                        return 1;
                    }
                    ibs = v;
                }
                None => {
                    magnolia_applet::eprintln!("dd: invalid ibs '{}'", val);
                    return 1;
                }
            },
            "obs" => match parse_size(val) {
                Some(v) => {
                    if v > usize::MAX as u64 {
                        magnolia_applet::eprintln!("dd: invalid obs '{}'", val);
                        return 1;
                    }
                    obs = v;
                }
                None => {
                    magnolia_applet::eprintln!("dd: invalid obs '{}'", val);
                    return 1;
                }
            },
            "bs" => match parse_size(val) {
                Some(v) => {
                    if v > usize::MAX as u64 {
                        magnolia_applet::eprintln!("dd: invalid bs '{}'", val);
                        return 1;
                    }
                    bs = v;
                }
                None => {
                    magnolia_applet::eprintln!("dd: invalid bs '{}'", val);
                    return 1;
                }
            },
            "count" => match parse_size(val) {
                Some(v) => {
                    count = v;
                    use_count = true;
                }
                None => {
                    magnolia_applet::eprintln!("dd: invalid count '{}'", val);
                    return 1;
                }
            },
            "skip" => match parse_size(val) {
                Some(v) => skip = v,
                None => {
                    magnolia_applet::eprintln!("dd: invalid skip '{}'", val);
                    return 1;
                }
            },
            "seek" => match parse_size(val) {
                Some(v) => seek = v,
                None => {
                    magnolia_applet::eprintln!("dd: invalid seek '{}'", val);
                    return 1;
                }
            },
            "conv" => {
                for part in val.split(',') {
                    match part {
                        "noerror" => noerror = true,
                        "sync" => sync = true,
                        "notrunc" => notrunc = true,
                        "" => {}
                        _ => {
                            magnolia_applet::eprintln!("dd: unsupported conv '{}'", part);
                            return 1;
                        }
                    }
                }
            }
            "status" => {
                if val == "none" {
                    status_none = true;
                } else {
                    magnolia_applet::eprintln!("dd: unsupported status '{}'", val);
                    return 1;
                }
            }
            _ => {
                magnolia_applet::eprintln!("dd: invalid argument '{}'", arg);
                return 1;
            }
        }
    }

    if bs > 0 {
        ibs = bs;
        obs = bs;
    }
    if ibs == 0 || obs == 0 {
        magnolia_applet::eprintln!("dd: block size cannot be zero");
        return 1;
    }

    let mut in_file: Option<fs::File> = None;
    let mut out_file: Option<fs::File> = None;
    let in_fd = if let Some(path) = ifile.as_deref() {
        match fs::File::open(path, sys::O_RDONLY, 0) {
            Ok(f) => {
                let fd = f.fd();
                in_file = Some(f);
                fd
            }
            Err(err) => {
                magnolia_applet::eprintln!("dd: {}: errno={}", path, err.errno);
                return 1;
            }
        }
    } else {
        sys::STDIN_FILENO
    };

    let out_fd = if let Some(path) = ofile.as_deref() {
        let mut flags = sys::O_WRONLY | sys::O_CREAT;
        if !notrunc {
            flags |= sys::O_TRUNC;
        }
        match fs::File::open(path, flags, 0o666) {
            Ok(f) => {
                let fd = f.fd();
                out_file = Some(f);
                fd
            }
            Err(err) => {
                magnolia_applet::eprintln!("dd: {}: errno={}", path, err.errno);
                return 1;
            }
        }
    } else {
        sys::STDOUT_FILENO
    };

    if !skip_input(in_fd, skip, ibs as usize) {
        magnolia_applet::eprintln!("dd: skip failed");
        return 1;
    }
    if !seek_output(out_fd, seek, obs as usize) {
        magnolia_applet::eprintln!("dd: seek failed");
        return 1;
    }

    let mut ibuf = vec![0u8; ibs as usize];
    let mut obuf = vec![0u8; obs as usize];
    let mut obuf_len = 0usize;
    let mut in_full = 0u64;
    let mut in_part = 0u64;
    let mut out_full = 0u64;
    let mut out_part = 0u64;
    let mut blocks = 0u64;
    let mut exit_status = 0i32;

    while !use_count || blocks < count {
        let mut read_error_block = false;
        let mut have_block = false;
        let mut n = 0usize;
        let r = read_retry(in_fd, &mut ibuf);
        match r {
            Ok(v) => {
                if v == 0 {
                    break;
                }
                n = v;
                have_block = true;
            }
            Err(_) => {
                magnolia_applet::eprintln!("dd: read error");
                exit_status = 1;
                if noerror {
                    if sync {
                        for b in ibuf.iter_mut() {
                            *b = 0;
                        }
                        n = ibs as usize;
                        read_error_block = true;
                        have_block = true;
                    } else {
                        if ibs <= sys::off_t::MAX as u64 {
                            let rc = unsafe { sys::lseek(in_fd, ibs as sys::off_t, sys::SEEK_CUR) };
                            if rc >= 0 {
                                blocks += 1;
                                continue;
                            }
                        }
                        break;
                    }
                } else {
                    break;
                }
            }
        }
        if !have_block {
            break;
        }
        if read_error_block {
            in_part += 1;
        } else if n == ibs as usize {
            in_full += 1;
        } else {
            in_part += 1;
        }
        let mut chunk_len = n;
        if sync && chunk_len < ibs as usize {
            for b in ibuf[chunk_len..ibs as usize].iter_mut() {
                *b = 0;
            }
            chunk_len = ibs as usize;
        }
        if obs == ibs {
            if write_all(out_fd, &ibuf[..chunk_len]).is_err() {
                magnolia_applet::eprintln!("dd: write error");
                exit_status = 1;
                break;
            }
            if chunk_len == obs as usize {
                out_full += 1;
            } else {
                out_part += 1;
            }
        } else {
            let mut off = 0usize;
            while off < chunk_len {
                let space = obs as usize - obuf_len;
                let take = core::cmp::min(space, chunk_len - off);
                obuf[obuf_len..obuf_len + take].copy_from_slice(&ibuf[off..off + take]);
                obuf_len += take;
                off += take;
                if obuf_len == obs as usize {
                    if write_all(out_fd, &obuf).is_err() {
                        magnolia_applet::eprintln!("dd: write error");
                        exit_status = 1;
                        obuf_len = 0;
                        off = chunk_len;
                        break;
                    }
                    out_full += 1;
                    obuf_len = 0;
                }
            }
        }
        blocks += 1;
    }

    if obuf_len > 0 {
        if write_all(out_fd, &obuf[..obuf_len]).is_err() {
            magnolia_applet::eprintln!("dd: write error");
            exit_status = 1;
        } else {
            out_part += 1;
        }
    }

    if !status_none {
        magnolia_applet::eprintln!("{}+{} records in", in_full, in_part);
        magnolia_applet::eprintln!("{}+{} records out", out_full, out_part);
    }

    drop(in_file);
    drop(out_file);
    exit_status
}

fn args_to_vec(args: Args) -> Vec<String> {
    let mut out = Vec::new();
    for (i, arg) in args.iter().enumerate() {
        if i == 0 {
            continue;
        }
        if let Ok(s) = arg.to_str() {
            out.push(s.to_string());
        }
    }
    out
}

fn main(args: Args) -> i32 {
    let argv = args_to_vec(args);
    dd_main(&argv)
}

magnolia_applet::entry!(main);
