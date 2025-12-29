#![no_std]

extern crate alloc;

use alloc::vec::Vec;
use magnolia_applet::cli;
use magnolia_applet::errno::{Error, ENOTSUP};
use magnolia_applet::{entry, Args};
use magnolia_applet::{eprintln, println};
use magnolia_applet::sys;

fn usage() {
    println!("usage: fbctl [--quiet|--verbose|--json] <info|clear|test> [args]");
    println!("  fbctl info");
    println!("  fbctl clear");
    println!("  fbctl test pattern|bars|noise");
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

fn rgb565(r: u8, g: u8, b: u8) -> u16 {
    let r = (r as u16 & 0xF8) << 8;
    let g = (g as u16 & 0xFC) << 3;
    let b = (b as u16 & 0xF8) >> 3;
    r | g | b
}

fn fb_open() -> Result<i32, Error> {
    let fd = unsafe { sys::open(b"/dev/fb0\0".as_ptr().cast(), sys::O_RDWR, 0) };
    if fd < 0 {
        return Err(Error::last());
    }
    Ok(fd)
}

fn fb_info(fd: i32) -> Result<sys::devfs_fb_info_t, Error> {
    let mut info = sys::devfs_fb_info_t {
        width: 0,
        height: 0,
        stride: 0,
        size_bytes: 0,
        format: 0,
        bpp: 0,
        backend: 0,
        reserved: 0,
    };
    let rc = unsafe {
        sys::ioctl(
            fd,
            sys::DEVFS_IOCTL_FB_GET_INFO,
            &mut info as *mut _ as *mut sys::c_void,
        )
    };
    if rc != 0 {
        return Err(Error::last());
    }
    Ok(info)
}

fn fb_blit(fd: i32, info: &sys::devfs_fb_info_t, buffer: &[u8]) -> Result<(), Error> {
    let blit = sys::devfs_fb_blit_t {
        x: 0,
        y: 0,
        width: info.width,
        height: info.height,
        stride: info.stride,
        format: info.format,
        flags: 0,
        reserved: 0,
        pixels: buffer.as_ptr().cast(),
    };
    let rc = unsafe {
        sys::ioctl(
            fd,
            sys::DEVFS_IOCTL_FB_BLIT,
            &blit as *const _ as *const sys::c_void,
        )
    };
    if rc != 0 {
        return Err(Error::last());
    }
    Ok(())
}

fn cmd_info(quiet: bool, verbose: bool) -> i32 {
    let fd = match fb_open() {
        Ok(fd) => fd,
        Err(err) => return err_exit("fbctl info failed", err, verbose),
    };

    let info = match fb_info(fd) {
        Ok(info) => info,
        Err(err) => {
            unsafe { sys::close(fd) };
            return err_exit("fbctl info failed", err, verbose);
        }
    };

    unsafe { sys::close(fd) };

    if !quiet {
        println!("width={} height={} stride={} size={} format={} bpp={} backend={}",
                 info.width,
                 info.height,
                 info.stride,
                 info.size_bytes,
                 info.format,
                 info.bpp,
                 info.backend);
    }
    0
}

fn cmd_clear(verbose: bool) -> i32 {
    let fd = match fb_open() {
        Ok(fd) => fd,
        Err(err) => return err_exit("fbctl clear failed", err, verbose),
    };

    let info = match fb_info(fd) {
        Ok(info) => info,
        Err(err) => {
            unsafe { sys::close(fd) };
            return err_exit("fbctl clear failed", err, verbose);
        }
    };

    let size = info.size_bytes as usize;
    let buffer = vec![0u8; size];

    let result = fb_blit(fd, &info, &buffer);
    unsafe { sys::close(fd) };
    match result {
        Ok(_) => 0,
        Err(err) => err_exit("fbctl clear failed", err, verbose),
    }
}

fn cmd_test(mode: &str, verbose: bool) -> i32 {
    let fd = match fb_open() {
        Ok(fd) => fd,
        Err(err) => return err_exit("fbctl test failed", err, verbose),
    };

    let info = match fb_info(fd) {
        Ok(info) => info,
        Err(err) => {
            unsafe { sys::close(fd) };
            return err_exit("fbctl test failed", err, verbose);
        }
    };

    let width = info.width as usize;
    let height = info.height as usize;
    let stride = info.stride as usize;
    let size = info.size_bytes as usize;
    if size == 0 || width == 0 || height == 0 {
        unsafe { sys::close(fd) };
        eprintln!("fbctl test: invalid framebuffer size");
        return 1;
    }

    let mut buffer = vec![0u8; size];

    match mode {
        "pattern" => {
            for y in 0..height {
                for x in 0..width {
                    let r = (x * 255 / width) as u8;
                    let g = (y * 255 / height) as u8;
                    let b = 128u8;
                    let color = rgb565(r, g, b);
                    let off = y * stride + x * 2;
                    if off + 1 < buffer.len() {
                        buffer[off] = (color & 0xFF) as u8;
                        buffer[off + 1] = (color >> 8) as u8;
                    }
                }
            }
        }
        "bars" => {
            let colors = [
                rgb565(255, 0, 0),
                rgb565(0, 255, 0),
                rgb565(0, 0, 255),
                rgb565(255, 255, 0),
                rgb565(0, 255, 255),
                rgb565(255, 0, 255),
                rgb565(255, 255, 255),
                rgb565(0, 0, 0),
            ];
            for y in 0..height {
                for x in 0..width {
                    let idx = (x * colors.len() / width).min(colors.len() - 1);
                    let color = colors[idx];
                    let off = y * stride + x * 2;
                    if off + 1 < buffer.len() {
                        buffer[off] = (color & 0xFF) as u8;
                        buffer[off + 1] = (color >> 8) as u8;
                    }
                }
            }
        }
        "noise" => {
            let mut seed: u32 = 0x1234abcd;
            for y in 0..height {
                for x in 0..width {
                    seed = seed.wrapping_mul(1664525).wrapping_add(1013904223);
                    let r = (seed & 0xFF) as u8;
                    let g = ((seed >> 8) & 0xFF) as u8;
                    let b = ((seed >> 16) & 0xFF) as u8;
                    let color = rgb565(r, g, b);
                    let off = y * stride + x * 2;
                    if off + 1 < buffer.len() {
                        buffer[off] = (color & 0xFF) as u8;
                        buffer[off + 1] = (color >> 8) as u8;
                    }
                }
            }
        }
        _ => {
            unsafe { sys::close(fd) };
            eprintln!("fbctl test: unknown mode");
            return 2;
        }
    }

    let result = fb_blit(fd, &info, &buffer);
    unsafe { sys::close(fd) };
    match result {
        Ok(_) => 0,
        Err(err) => err_exit("fbctl test failed", err, verbose),
    }
}

fn main(args: Args) -> i32 {
    let cli = match cli::parse(args) {
        Ok(cli) => cli,
        Err(_) => {
            eprintln!("fbctl: invalid arguments");
            return 2;
        }
    };

    if cli.json {
        eprintln!("fbctl: --json not supported");
        return 3;
    }

    let args = cli.args();
    if args.is_empty() || args[0] == "--help" || args[0] == "-h" {
        usage();
        return 0;
    }

    match args[0].as_str() {
        "info" => cmd_info(cli.quiet, cli.verbose),
        "clear" => cmd_clear(cli.verbose),
        "test" => {
            if args.len() < 2 {
                eprintln!("fbctl test: missing mode");
                return 2;
            }
            cmd_test(&args[1], cli.verbose)
        }
        _ => {
            eprintln!("fbctl: unknown command");
            2
        }
    }
}

entry!(main);
