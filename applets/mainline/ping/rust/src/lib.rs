#![no_std]

extern crate alloc;

use alloc::format;
use alloc::string::String;
use core::mem;
use core::ptr;
use magnolia_applet::cli;
use magnolia_applet::errno::{Error, EAGAIN, ETIMEDOUT};
use magnolia_applet::time;
use magnolia_applet::{entry, Args};
use magnolia_applet::{eprintln, println};
use magnolia_applet::sys;

const DEFAULT_COUNT: u32 = 4;
const DEFAULT_TIMEOUT_MS: u32 = 1000;
const ECHO_PORT: u16 = 7;

fn usage() {
    println!("usage: ping [--count N] [--timeout MS] <host>");
}

fn parse_ipv4(value: &str) -> Option<u32> {
    let mut parts = value.split('.');
    let a = parts.next()?.parse::<u8>().ok()?;
    let b = parts.next()?.parse::<u8>().ok()?;
    let c = parts.next()?.parse::<u8>().ok()?;
    let d = parts.next()?.parse::<u8>().ok()?;
    if parts.next().is_some() {
        return None;
    }
    Some(u32::from_be_bytes([a, b, c, d]))
}

fn ipv4_to_string(addr: u32) -> String {
    let octets = addr.to_be_bytes();
    format!("{}.{}.{}.{}", octets[0], octets[1], octets[2], octets[3])
}

fn htons(value: u16) -> u16 {
    value.to_be()
}

fn set_timeout(sock: i32, timeout_ms: u32) -> Result<(), Error> {
    let tv = sys::timeval {
        tv_sec: (timeout_ms / 1000) as sys::time_t,
        tv_usec: ((timeout_ms % 1000) * 1000) as sys::c_long,
    };
    let rc = unsafe {
        sys::setsockopt(
            sock,
            sys::SOL_SOCKET,
            sys::SO_RCVTIMEO,
            &tv as *const _ as *const sys::c_void,
            mem::size_of::<sys::timeval>() as sys::socklen_t,
        )
    };
    if rc != 0 {
        return Err(Error::last());
    }
    let rc = unsafe {
        sys::setsockopt(
            sock,
            sys::SOL_SOCKET,
            sys::SO_SNDTIMEO,
            &tv as *const _ as *const sys::c_void,
            mem::size_of::<sys::timeval>() as sys::socklen_t,
        )
    };
    if rc != 0 {
        return Err(Error::last());
    }
    Ok(())
}

fn main(args: Args) -> i32 {
    let cli = match cli::parse(args) {
        Ok(cli) => cli,
        Err(_) => {
            eprintln!("ping: invalid arguments");
            return 2;
        }
    };

    if cli.json {
        eprintln!("ping: --json not supported");
        return 3;
    }

    let mut host: Option<String> = None;
    let mut count = DEFAULT_COUNT;
    let mut timeout_ms = DEFAULT_TIMEOUT_MS;

    let args = cli.args();
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--help" | "-h" => {
                usage();
                return 0;
            }
            "--count" => {
                if i + 1 >= args.len() {
                    eprintln!("ping: missing count");
                    return 2;
                }
                count = match args[i + 1].parse::<u32>() {
                    Ok(v) => v,
                    Err(_) => {
                        eprintln!("ping: invalid count");
                        return 2;
                    }
                };
                i += 2;
            }
            "--timeout" => {
                if i + 1 >= args.len() {
                    eprintln!("ping: missing timeout");
                    return 2;
                }
                timeout_ms = match args[i + 1].parse::<u32>() {
                    Ok(v) => v,
                    Err(_) => {
                        eprintln!("ping: invalid timeout");
                        return 2;
                    }
                };
                i += 2;
            }
            _ => {
                if host.is_some() {
                    eprintln!("ping: unexpected argument");
                    return 2;
                }
                host = Some(args[i].clone());
                i += 1;
            }
        }
    }

    let host = match host {
        Some(host) => host,
        None => {
            eprintln!("ping: missing host");
            return 2;
        }
    };

    let addr = match parse_ipv4(&host) {
        Some(addr) => addr,
        None => {
            eprintln!("ping: only IPv4 literals are supported");
            return 3;
        }
    };

    let sock = unsafe { sys::socket(sys::AF_INET, sys::SOCK_DGRAM, sys::IPPROTO_UDP) };
    if sock < 0 {
        let err = Error::last();
        eprintln!("ping: socket failed errno={}", err.errno);
        return err.errno.max(1).min(255);
    }

    if let Err(err) = set_timeout(sock, timeout_ms) {
        eprintln!("ping: setsockopt failed errno={}", err.errno);
        unsafe { sys::close(sock) };
        return err.errno.max(1).min(255);
    }

    let mut dest = sys::sockaddr_in {
        sin_family: sys::AF_INET as u16,
        sin_port: htons(ECHO_PORT),
        sin_addr: sys::in_addr { s_addr: addr },
        sin_zero: [0u8; 8],
    };

    let mut transmitted = 0u32;
    let mut received = 0u32;

    if !cli.quiet {
        println!("ping {}: udp echo", host);
    }

    for seq in 0..count {
        transmitted += 1;
        let payload = format!("magnolia ping seq={}", seq);
        let start = match time::monotonic_duration() {
            Ok(t) => t,
            Err(err) => {
                eprintln!("ping: time failed errno={}", err.errno);
                break;
            }
        };

        let sent = unsafe {
            sys::sendto(
                sock,
                payload.as_ptr().cast(),
                payload.len(),
                0,
                &dest as *const _ as *const sys::sockaddr,
                mem::size_of::<sys::sockaddr_in>() as sys::socklen_t,
            )
        };
        if sent < 0 {
            let err = Error::last();
            eprintln!("ping: send failed errno={}", err.errno);
            break;
        }

        let mut buf = [0u8; 256];
        let mut addr_len = mem::size_of::<sys::sockaddr_in>() as sys::socklen_t;
        let n = unsafe {
            sys::recvfrom(
                sock,
                buf.as_mut_ptr().cast(),
                buf.len(),
                0,
                ptr::null_mut(),
                &mut addr_len,
            )
        };
        if n < 0 {
            let err = Error::last();
            if err.errno == ETIMEDOUT || err.errno == EAGAIN {
                if !cli.quiet {
                    println!("seq={} timeout", seq);
                }
                continue;
            }
            if !cli.quiet {
                println!("seq={} error errno={}", seq, err.errno);
            }
            break;
        }

        let end = match time::monotonic_duration() {
            Ok(t) => t,
            Err(err) => {
                eprintln!("ping: time failed errno={}", err.errno);
                break;
            }
        };

        received += 1;
        let rtt = end.saturating_sub(start);
        let rtt_ms = rtt.as_micros() as u64 / 1000;

        if !cli.quiet {
            println!("seq={} bytes={} time={}ms", seq, n, rtt_ms);
        }
    }

    unsafe { sys::close(sock) };

    let loss = if transmitted > 0 {
        100 - (received * 100 / transmitted)
    } else {
        100
    };

    if !cli.quiet {
        println!("--- {} ping statistics ---", host);
        println!("{} packets transmitted, {} received, {}% loss", transmitted, received, loss);
    }

    if received == transmitted {
        0
    } else {
        1
    }
}

entry!(main);
