#![no_std]

extern crate alloc;

use alloc::format;
use alloc::string::{String, ToString};
use alloc::vec::Vec;
use magnolia_applet::fs;
use magnolia_applet::io;
use magnolia_applet::sys;
use magnolia_applet::Args;

const LINE_BYTES: usize = 16;

#[derive(Copy, Clone)]
enum FormatMode {
    Canonical,
    ByteOctal,
    Char,
    ShortDec,
    ShortOct,
    ShortHex,
}

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

fn push_hex(out: &mut String, mut value: u64, width: usize) {
    let mut buf = [b'0'; 16];
    let mut i = width;
    while i > 0 {
        let nib = (value & 0xF) as u8;
        buf[i - 1] = match nib {
            0..=9 => b'0' + nib,
            _ => b'a' + (nib - 10),
        };
        value >>= 4;
        i -= 1;
    }
    for b in &buf[..width] {
        out.push(*b as char);
    }
}

fn push_octal(out: &mut String, mut value: u64, width: usize) {
    let mut buf = [b'0'; 22];
    let mut i = width;
    while i > 0 {
        let digit = (value & 0x7) as u8;
        buf[i - 1] = b'0' + digit;
        value >>= 3;
        i -= 1;
    }
    for b in &buf[..width] {
        out.push(*b as char);
    }
}

fn render_char(b: u8, out: &mut String) {
    out.push(' ');
    out.push(' ');
    match b {
        b'\0' => {
            out.push('\\');
            out.push('0');
        }
        b'\n' => {
            out.push('\\');
            out.push('n');
        }
        b'\r' => {
            out.push('\\');
            out.push('r');
        }
        b'\t' => {
            out.push('\\');
            out.push('t');
        }
        b'\x08' => {
            out.push('\\');
            out.push('b');
        }
        b'\x0c' => {
            out.push('\\');
            out.push('f');
        }
        b'\x0b' => {
            out.push('\\');
            out.push('v');
        }
        b'\\' => {
            out.push('\\');
            out.push('\\');
        }
        _ => {
            if b >= 0x20 && b <= 0x7e {
                out.push(' ');
                out.push(b as char);
            } else {
                out.push('.');
                out.push(' ');
            }
        }
    }
}

fn print_line(mode: FormatMode, offset: u64, buf: &[u8], len: usize) {
    let mut line = String::new();
    push_hex(&mut line, offset, 8);
    line.push(' ');
    line.push(' ');
    match mode {
        FormatMode::Canonical => {
            for i in 0..LINE_BYTES {
                if i < len {
                    line.push_str(&format!("{:02x} ", buf[i]));
                } else {
                    line.push_str("   ");
                }
                if i == 7 {
                    line.push(' ');
                }
            }
            line.push(' ');
            line.push('|');
            for i in 0..LINE_BYTES {
                if i < len {
                    let b = buf[i];
                    if b >= 0x20 && b <= 0x7e {
                        line.push(b as char);
                    } else {
                        line.push('.');
                    }
                } else {
                    line.push(' ');
                }
            }
            line.push('|');
        }
        FormatMode::ByteOctal => {
            for i in 0..LINE_BYTES {
                if i < len {
                    line.push(' ');
                    push_octal(&mut line, buf[i] as u64, 3);
                } else {
                    line.push_str("    ");
                }
            }
        }
        FormatMode::Char => {
            for i in 0..LINE_BYTES {
                if i < len {
                    render_char(buf[i], &mut line);
                } else {
                    line.push_str("   ");
                }
            }
        }
        FormatMode::ShortDec | FormatMode::ShortOct | FormatMode::ShortHex => {
            let width = match mode {
                FormatMode::ShortDec => 5,
                FormatMode::ShortOct => 6,
                FormatMode::ShortHex => 4,
                _ => 4,
            };
            for i in (0..LINE_BYTES).step_by(2) {
                if i + 1 < len {
                    let word = buf[i] as u16 | ((buf[i + 1] as u16) << 8);
                    line.push(' ');
                    match mode {
                        FormatMode::ShortDec => {
                            let s = format!("{:0width$}", word, width = width);
                            line.push_str(&s);
                        }
                        FormatMode::ShortOct => {
                            let mut tmp = String::new();
                            push_octal(&mut tmp, word as u64, width);
                            line.push_str(&tmp);
                        }
                        _ => {
                            let mut tmp = String::new();
                            push_hex(&mut tmp, word as u64, width);
                            line.push_str(&tmp);
                        }
                    }
                } else {
                    for _ in 0..(width + 1) {
                        line.push(' ');
                    }
                }
            }
        }
    }
    line.push('\n');
    let _ = io::write_all(sys::STDOUT_FILENO, line.as_bytes());
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

fn hexdump_fd(fd: i32, name: &str, mode: FormatMode, verbose: bool, offset: &mut u64,
              remaining: Option<&mut u64>, skip: &mut u64) -> bool {
    let mut prev = [0u8; LINE_BYTES];
    let mut prev_len = 0usize;
    let mut suppressed = false;
    let mut remaining = remaining;

    loop {
        if *skip > 0 {
            if !skip_bytes(fd, skip) {
                return false;
            }
        }
        let mut want = LINE_BYTES;
        if let Some(rem) = remaining.as_deref_mut() {
            if *rem == 0 {
                break;
            }
            if *rem < want as u64 {
                want = *rem as usize;
            }
        }
        let mut buf = [0u8; LINE_BYTES];
        let n = match io::read(fd, &mut buf[..want]) {
            Ok(v) => v,
            Err(_) => {
                magnolia_applet::eprintln!("hexdump: {}: read error", name);
                return false;
            }
        };
        if n == 0 {
            break;
        }
        if let Some(rem) = remaining.as_deref_mut() {
            *rem -= n as u64;
        }
        let same = !verbose && prev_len == n && prev[..n] == buf[..n];
        if same {
            if !suppressed {
                let _ = io::write_all(sys::STDOUT_FILENO, b"*\n");
                suppressed = true;
            }
        } else {
            suppressed = false;
            print_line(mode, *offset, &buf, n);
            prev[..n].copy_from_slice(&buf[..n]);
            prev_len = n;
        }
        *offset += n as u64;
    }
    true
}

fn hexdump_main(args: &[String]) -> i32 {
    let mut mode = FormatMode::Canonical;
    let mut verbose = false;
    let mut length = 0u64;
    let mut skip = 0u64;
    let mut use_length = false;

    let mut files: Vec<String> = Vec::new();
    let mut i = 0usize;
    while i < args.len() {
        let arg = &args[i];
        if arg == "-b" {
            mode = FormatMode::ByteOctal;
        } else if arg == "-c" {
            mode = FormatMode::Char;
        } else if arg == "-C" {
            mode = FormatMode::Canonical;
        } else if arg == "-d" {
            mode = FormatMode::ShortDec;
        } else if arg == "-o" {
            mode = FormatMode::ShortOct;
        } else if arg == "-x" {
            mode = FormatMode::ShortHex;
        } else if arg == "-v" {
            verbose = true;
        } else if arg == "-n" {
            i += 1;
            if i >= args.len() {
                magnolia_applet::eprintln!("hexdump: -n requires value");
                return 1;
            }
            match parse_size(&args[i]) {
                Some(v) => {
                    length = v;
                    use_length = true;
                }
                None => {
                    magnolia_applet::eprintln!("hexdump: invalid length '{}'", args[i]);
                    return 1;
                }
            }
        } else if arg == "-s" {
            i += 1;
            if i >= args.len() {
                magnolia_applet::eprintln!("hexdump: -s requires value");
                return 1;
            }
            match parse_size(&args[i]) {
                Some(v) => skip = v,
                None => {
                    magnolia_applet::eprintln!("hexdump: invalid skip '{}'", args[i]);
                    return 1;
                }
            }
        } else if arg.starts_with('-') {
            magnolia_applet::eprintln!("usage: hexdump [-bcdoxC] [-n length] [-s offset] [-v] [file ...]");
            return 1;
        } else {
            files.push(arg.to_string());
        }
        i += 1;
    }

    let mut offset = 0u64;
    let mut remaining = length;
    let mut rc = 0;

    if files.is_empty() {
        let ok = hexdump_fd(sys::STDIN_FILENO, "-", mode, verbose, &mut offset,
                            if use_length { Some(&mut remaining) } else { None },
                            &mut skip);
        if !ok {
            rc = 1;
        }
    } else {
        for path in files {
            let fd = match fs::File::open(&path, sys::O_RDONLY, 0) {
                Ok(f) => f.fd(),
                Err(err) => {
                    magnolia_applet::eprintln!("hexdump: {}: errno={}", path, err.errno);
                    rc = 1;
                    continue;
                }
            };
            let ok = hexdump_fd(fd, &path, mode, verbose, &mut offset,
                                if use_length { Some(&mut remaining) } else { None },
                                &mut skip);
            if !ok {
                rc = 1;
            }
            if use_length && remaining == 0 {
                break;
            }
        }
    }

    let mut tail = String::new();
    push_hex(&mut tail, offset, 8);
    tail.push('\n');
    let _ = io::write_all(sys::STDOUT_FILENO, tail.as_bytes());
    rc
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
    hexdump_main(&argv)
}

magnolia_applet::entry!(main);
