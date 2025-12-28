#![no_std]

extern crate alloc;

use alloc::string::{String, ToString};
use alloc::vec::Vec;
use magnolia_applet::fs;
use magnolia_applet::io;
use magnolia_applet::sys;
use magnolia_applet::Args;

fn parse_token(input: &str) -> Option<(u64, &str)> {
    if input.is_empty() {
        return None;
    }
    let bytes = input.as_bytes();
    let mut idx = 0usize;
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

fn hex_value(c: u8) -> Option<u8> {
    match c {
        b'0'..=b'9' => Some(c - b'0'),
        b'a'..=b'f' => Some(c - b'a' + 10),
        b'A'..=b'F' => Some(c - b'A' + 10),
        _ => None,
    }
}

fn push_hex_byte(out: &mut String, b: u8, upper: bool) {
    let hi = b >> 4;
    let lo = b & 0xF;
    let map = if upper { b"0123456789ABCDEF" } else { b"0123456789abcdef" };
    out.push(map[hi as usize] as char);
    out.push(map[lo as usize] as char);
}

fn skip_bytes(fd: i32, skip: &mut u64) -> bool {
    if *skip == 0 {
        return true;
    }
    let offset = *skip as sys::off_t;
    let rc = unsafe { sys::lseek(fd, offset, sys::SEEK_CUR) };
    if rc >= 0 {
        *skip = 0;
        return true;
    }
    let mut buf = [0u8; 256];
    while *skip > 0 {
        let want = if *skip < buf.len() as u64 { *skip as usize } else { buf.len() };
        let n = io::read(fd, &mut buf[..want]);
        if let Ok(read) = n {
            if read == 0 {
                return false;
            }
            *skip -= read as u64;
        } else {
            return false;
        }
    }
    true
}

fn reverse_stream(fd: i32) -> bool {
    let mut line = [0u8; 256];
    let mut line_len = 0usize;
    let mut half: Option<u8> = None;
    let mut outbuf = [0u8; 256];
    let mut out_len = 0usize;

    let mut buf = [0u8; 128];
    loop {
        let r = io::read(fd, &mut buf);
        let n = match r {
            Ok(v) => v,
            Err(_) => return false,
        };
        if n == 0 {
            break;
        }
        for &c in &buf[..n] {
            if c == b'\n' || c == b'\r' {
                let slice = &line[..line_len];
                let mut start = 0usize;
                if let Some(pos) = slice.iter().position(|&b| b == b':') {
                    if pos <= 8 {
                        start = pos + 1;
                    }
                }
                for &ch in &slice[start..] {
                    if let Some(v) = hex_value(ch) {
                        if let Some(h) = half {
                            outbuf[out_len] = (h << 4) | v;
                            out_len += 1;
                            half = None;
                            if out_len == outbuf.len() {
                                let _ = io::write_all(sys::STDOUT_FILENO, &outbuf[..out_len]);
                                out_len = 0;
                            }
                        } else {
                            half = Some(v);
                        }
                    }
                }
                line_len = 0;
                continue;
            }
            if line_len + 1 < line.len() {
                line[line_len] = c;
                line_len += 1;
            }
        }
    }

    if line_len > 0 {
        let slice = &line[..line_len];
        let mut start = 0usize;
        if let Some(pos) = slice.iter().position(|&b| b == b':') {
            if pos <= 8 {
                start = pos + 1;
            }
        }
        for &ch in &slice[start..] {
            if let Some(v) = hex_value(ch) {
                if let Some(h) = half {
                    outbuf[out_len] = (h << 4) | v;
                    out_len += 1;
                    half = None;
                    if out_len == outbuf.len() {
                        let _ = io::write_all(sys::STDOUT_FILENO, &outbuf[..out_len]);
                        out_len = 0;
                    }
                } else {
                    half = Some(v);
                }
            }
        }
    }

    if out_len > 0 {
        let _ = io::write_all(sys::STDOUT_FILENO, &outbuf[..out_len]);
    }
    true
}

fn xxd_forward(fd: i32, mut skip: u64, mut length: u64, use_length: bool,
               mut columns: usize, group: usize, plain: bool, upper: bool) -> bool {
    if !skip_bytes(fd, &mut skip) {
        return false;
    }
    if plain && columns == 16 {
        columns = 30;
    }
    let mut offset = 0u64;
    let mut buf = vec![0u8; columns.max(1)];
    loop {
        let mut want = columns;
        if use_length && length < want as u64 {
            want = length as usize;
        }
        let n = match io::read(fd, &mut buf[..want]) {
            Ok(v) => v,
            Err(_) => return false,
        };
        if n == 0 {
            break;
        }
        if plain {
            let mut line = String::new();
            for (i, b) in buf[..n].iter().enumerate() {
                push_hex_byte(&mut line, *b, upper);
                if (i + 1) == n || ((i + 1) % columns) == 0 {
                    line.push('\n');
                }
            }
            let _ = io::write_all(sys::STDOUT_FILENO, line.as_bytes());
        } else {
            let mut line = String::new();
            line.push_str(&format!("{:08x}: ", offset));
            for i in 0..columns {
                if i < n {
                    push_hex_byte(&mut line, buf[i], upper);
                } else {
                    line.push(' ');
                    line.push(' ');
                }
                if group > 0 && (i + 1) % group == 0 {
                    line.push(' ');
                }
            }
            line.push(' ');
            for &b in &buf[..n] {
                if b >= 0x20 && b <= 0x7e {
                    line.push(b as char);
                } else {
                    line.push('.');
                }
            }
            line.push('\n');
            let _ = io::write_all(sys::STDOUT_FILENO, line.as_bytes());
        }
        offset += n as u64;
        if use_length {
            length -= n as u64;
            if length == 0 {
                break;
            }
        }
    }
    true
}

fn xxd_main(args: &[String]) -> i32 {
    let mut columns = 16usize;
    let mut group = 2usize;
    let mut length = 0u64;
    let mut skip = 0u64;
    let mut use_length = false;
    let mut plain = false;
    let mut reverse = false;
    let mut upper = false;

    let mut files: Vec<String> = Vec::new();
    let mut i = 0usize;
    while i < args.len() {
        let arg = &args[i];
        if arg == "-g" {
            i += 1;
            if i >= args.len() {
                magnolia_applet::eprintln!("xxd: -g requires value");
                return 1;
            }
            group = args[i].parse::<usize>().unwrap_or(2);
        } else if arg == "-c" {
            i += 1;
            if i >= args.len() {
                magnolia_applet::eprintln!("xxd: -c requires value");
                return 1;
            }
            columns = args[i].parse::<usize>().unwrap_or(16).clamp(1, 256);
        } else if arg == "-l" {
            i += 1;
            if i >= args.len() {
                magnolia_applet::eprintln!("xxd: -l requires value");
                return 1;
            }
            match parse_size(&args[i]) {
                Some(v) => {
                    length = v;
                    use_length = true;
                }
                None => {
                    magnolia_applet::eprintln!("xxd: invalid length '{}'", args[i]);
                    return 1;
                }
            }
        } else if arg == "-s" {
            i += 1;
            if i >= args.len() {
                magnolia_applet::eprintln!("xxd: -s requires value");
                return 1;
            }
            match parse_size(&args[i]) {
                Some(v) => skip = v,
                None => {
                    magnolia_applet::eprintln!("xxd: invalid offset '{}'", args[i]);
                    return 1;
                }
            }
        } else if arg == "-p" {
            plain = true;
        } else if arg == "-r" {
            reverse = true;
        } else if arg == "-u" {
            upper = true;
        } else if arg.starts_with('-') {
            magnolia_applet::eprintln!("usage: xxd [-g n] [-c n] [-l len] [-s offset] [-p] [-r] [-u] [file]");
            return 1;
        } else {
            files.push(arg.to_string());
        }
        i += 1;
    }

    let path = files.get(0).map(|s| s.as_str());
    let fd = if let Some(p) = path {
        match fs::File::open(p, sys::O_RDONLY, 0) {
            Ok(f) => f.fd(),
            Err(err) => {
                magnolia_applet::eprintln!("xxd: {}: errno={}", p, err.errno);
                return 1;
            }
        }
    } else {
        sys::STDIN_FILENO
    };

    let ok = if reverse {
        reverse_stream(fd)
    } else {
        xxd_forward(fd, skip, length, use_length, columns, group, plain, upper)
    };

    if !ok {
        magnolia_applet::eprintln!("xxd: error");
        return 1;
    }
    0
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
    xxd_main(&argv)
}

magnolia_applet::entry!(main);
