#![no_std]

extern crate alloc;

use alloc::vec::Vec;
use magnolia_applet::errno::{Error, ENOTSUP};
use magnolia_applet::{eprintln, entry, println, Args};
use magnolia_applet::sys;

fn usage() {
    println!("usage: fbrgb");
}

fn rgb565(r: u8, g: u8, b: u8) -> u16 {
    let r = (r as u16 & 0xF8) << 8;
    let g = (g as u16 & 0xFC) << 3;
    let b = (b as u16 & 0xF8) >> 3;
    r | g | b
}

fn pixel565(format: u8, r: u8, g: u8, b: u8) -> u16 {
    if format == sys::DEVFS_FB_FORMAT_BGR565 {
        rgb565(b, g, r)
    } else {
        rgb565(r, g, b)
    }
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

fn has_help(args: Args) -> bool {
    for (idx, arg) in args.iter().enumerate() {
        if idx == 0 {
            continue;
        }
        let bytes = arg.to_bytes();
        if bytes == b"-h" || bytes == b"--help" {
            return true;
        }
    }
    false
}

fn err_exit(msg: &str, err: Error) -> i32 {
    eprintln!("{} (errno={})", msg, err.errno);
    if err.errno == ENOTSUP {
        3
    } else if err.errno > 0 && err.errno < 256 {
        err.errno
    } else {
        1
    }
}

fn main(args: Args) -> i32 {
    if has_help(args) {
        usage();
        return 0;
    }

    let fd = match fb_open() {
        Ok(fd) => fd,
        Err(err) => return err_exit("fbrgb: open /dev/fb0 failed", err),
    };

    let info = match fb_info(fd) {
        Ok(info) => info,
        Err(err) => {
            unsafe { sys::close(fd) };
            return err_exit("fbrgb: ioctl DEVFS_IOCTL_FB_GET_INFO failed", err);
        }
    };

    let width = info.width as usize;
    let height = info.height as usize;
    let stride = info.stride as usize;
    let size = info.size_bytes as usize;
    if width == 0 || height == 0 || size == 0 {
        unsafe { sys::close(fd) };
        eprintln!("fbrgb: invalid framebuffer size");
        return 1;
    }

    let mut buffer = vec![0u8; size];
    let stripe_w = width / 3;

    let red = pixel565(info.format, 255, 0, 0);
    let green = pixel565(info.format, 0, 255, 0);
    let blue = pixel565(info.format, 0, 0, 255);

    for y in 0..height {
        for x in 0..width {
            let color = if x < stripe_w {
                red
            } else if x < stripe_w * 2 {
                green
            } else {
                blue
            };
            let off = y * stride + x * 2;
            if off + 1 < buffer.len() {
                buffer[off] = (color & 0xFF) as u8;
                buffer[off + 1] = (color >> 8) as u8;
            }
        }
    }

    let result = fb_blit(fd, &info, &buffer);
    unsafe { sys::close(fd) };
    match result {
        Ok(_) => 0,
        Err(err) => err_exit("fbrgb: ioctl DEVFS_IOCTL_FB_BLIT failed", err),
    }
}

entry!(main);
