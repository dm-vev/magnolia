#![no_std]

extern crate alloc;

use alloc::vec::Vec;
use core::str;

use magnolia_applet::cli;
use magnolia_applet::errno::{Error, ENOTSUP};
use magnolia_applet::fs;
use magnolia_applet::io;
use magnolia_applet::time;
use magnolia_applet::{entry, Args};
use magnolia_applet::{eprintln, println};
use magnolia_applet::sys;

fn usage() {
    println!("usage: dmesg [--follow] [--lines N]");
}

fn err_exit(op: &str, err: Error, verbose: bool) -> i32 {
    if verbose {
        eprintln!("{}: errno={}", op, err.errno);
    } else {
        eprintln!("{}", op);
    }
    if err.errno == ENOTSUP {
        return 3;
    }
    if err.errno > 0 && err.errno < 256 {
        err.errno
    } else {
        1
    }
}

fn output_last_lines(buf: &[u8], lines: usize) {
    let text = str::from_utf8(buf).unwrap_or("");
    let all: Vec<&str> = text.lines().collect();
    let start = all.len().saturating_sub(lines);
    for line in &all[start..] {
        println!("{}", line);
    }
}

fn main(args: Args) -> i32 {
    let cli = match cli::parse(args) {
        Ok(cli) => cli,
        Err(_) => {
            eprintln!("dmesg: invalid arguments");
            return 2;
        }
    };

    if cli.json {
        eprintln!("dmesg: --json not supported");
        return 3;
    }

    let mut follow = false;
    let mut lines: Option<usize> = None;

    let args = cli.args();
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--help" | "-h" => {
                usage();
                return 0;
            }
            "--follow" => {
                follow = true;
                i += 1;
            }
            "--lines" => {
                if i + 1 >= args.len() {
                    eprintln!("dmesg: missing line count");
                    return 2;
                }
                lines = match args[i + 1].parse::<usize>() {
                    Ok(v) => Some(v),
                    Err(_) => {
                        eprintln!("dmesg: invalid line count");
                        return 2;
                    }
                };
                i += 2;
            }
            _ => {
                eprintln!("dmesg: unexpected argument");
                return 2;
            }
        }
    }

    let file = match fs::File::open("/dev/kmsg", sys::O_RDONLY, 0) {
        Ok(file) => file,
        Err(err) => return err_exit("dmesg: open failed", err, cli.verbose),
    };

    let mut buf = vec![0u8; 4096];
    let n = match file.read(&mut buf) {
        Ok(n) => n,
        Err(err) => return err_exit("dmesg: read failed", err, cli.verbose),
    };

    if !cli.quiet {
        if let Some(lines) = lines {
            output_last_lines(&buf[..n], lines);
        } else if n > 0 {
            let _ = io::write_all(sys::STDOUT_FILENO, &buf[..n]);
        }
    }

    if follow {
        let mut follow_buf = [0u8; 512];
        loop {
            let n = match file.read(&mut follow_buf) {
                Ok(n) => n,
                Err(err) => {
                    let _ = err_exit("dmesg: read failed", err, cli.verbose);
                    break;
                }
            };
            if n == 0 {
                time::usleep(100000);
                continue;
            }
            if !cli.quiet {
                let _ = io::write_all(sys::STDOUT_FILENO, &follow_buf[..n]);
            }
        }
    }

    0
}

entry!(main);
