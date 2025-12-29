#![no_std]

use core::ffi::CStr;
use core::str;

use magnolia_applet::{entry, Args};
use magnolia_applet::{io, sys};

extern "C" {
    fn getenv(name: *const sys::c_char) -> *const sys::c_char;
}

const VERSION: &str = "Magnolia coreutils 0.1";
const CLEAR_SEQ: &[u8] = b"\x1b[H\x1b[2J";
const CLEAR_SCROLLBACK: &[u8] = b"\x1b[3J";

struct TermEntry {
    name: &'static [u8],
    clear_scrollback: bool,
}

const TERMS: &[TermEntry] = &[
    TermEntry { name: b"xterm", clear_scrollback: true },
    TermEntry { name: b"xterm-256color", clear_scrollback: true },
    TermEntry { name: b"xterm-color", clear_scrollback: true },
    TermEntry { name: b"screen", clear_scrollback: true },
    TermEntry { name: b"screen-256color", clear_scrollback: true },
    TermEntry { name: b"tmux", clear_scrollback: true },
    TermEntry { name: b"tmux-256color", clear_scrollback: true },
    TermEntry { name: b"vt100", clear_scrollback: false },
    TermEntry { name: b"ansi", clear_scrollback: false },
    TermEntry { name: b"linux", clear_scrollback: false },
];

fn usage() {
    magnolia_applet::eprintln!("usage: clear [-T term] [-V] [-x]");
}

fn find_term(term: &[u8]) -> Option<bool> {
    for entry in TERMS {
        if term == entry.name {
            return Some(entry.clear_scrollback);
        }
    }
    None
}

fn getenv_term() -> Option<&'static [u8]> {
    let key = CStr::from_bytes_with_nul(b"TERM\0").ok()?;
    let value = unsafe { getenv(key.as_ptr()) };
    if value.is_null() {
        return None;
    }
    Some(unsafe { CStr::from_ptr(value) }.to_bytes())
}

fn emit_write_error() {
    let err = magnolia_applet::errno::errno();
    let msg = unsafe { sys::strerror(err) };
    if msg.is_null() {
        magnolia_applet::eprintln!("clear: stdout: unknown error");
        return;
    }
    let text = unsafe { CStr::from_ptr(msg) }.to_str().unwrap_or("unknown error");
    magnolia_applet::eprintln!("clear: stdout: {}", text);
}

fn main_inner(args: Args) -> i32 {
    let mut term_override: Option<&[u8]> = None;
    let mut no_scrollback = false;
    let mut show_version = false;

    let mut i = 1;
    while i < args.len() {
        let arg = match args.get(i) {
            Some(v) => v.to_bytes(),
            None => {
                i += 1;
                continue;
            }
        };
        if arg == b"--" {
            i += 1;
            break;
        }
        if arg.len() >= 2 && arg[0] == b'-' {
            if arg == b"-V" {
                show_version = true;
                i += 1;
                continue;
            }
            if arg == b"-x" {
                no_scrollback = true;
                i += 1;
                continue;
            }
            if arg.starts_with(b"-T") {
                if arg.len() > 2 {
                    term_override = Some(&arg[2..]);
                    i += 1;
                    continue;
                }
                i += 1;
                let next = match args.get(i) {
                    Some(v) => v.to_bytes(),
                    None => b"",
                };
                if next.is_empty() {
                    magnolia_applet::eprintln!("clear: option requires an argument -- T");
                    usage();
                    return 1;
                }
                term_override = Some(next);
                i += 1;
                continue;
            }
            let opt = if arg.len() > 1 { arg[1] } else { b'?' };
            magnolia_applet::eprintln!("clear: illegal option -- {}", opt as char);
            usage();
            return 1;
        }
        usage();
        return 1;
    }

    if i < args.len() {
        usage();
        return 1;
    }

    if show_version {
        magnolia_applet::println!("clear ({})", VERSION);
        return 0;
    }

    let term = term_override.or_else(getenv_term).unwrap_or(b"");
    if term.is_empty() {
        magnolia_applet::eprintln!("clear: TERM environment variable not set.");
        return 1;
    }

    let clear_scrollback = match find_term(term) {
        Some(v) => v,
        None => {
            let term_str = str::from_utf8(term).unwrap_or("<non-utf8>");
            magnolia_applet::eprintln!("clear: unknown terminal type {}", term_str);
            return 1;
        }
    };

    if io::write_all(sys::STDOUT_FILENO, CLEAR_SEQ).is_err() {
        emit_write_error();
        return 1;
    }
    if clear_scrollback && !no_scrollback {
        if io::write_all(sys::STDOUT_FILENO, CLEAR_SCROLLBACK).is_err() {
            emit_write_error();
            return 1;
        }
    }
    0
}

entry!(main_inner);
