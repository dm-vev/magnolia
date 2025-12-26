#![no_std]

extern crate alloc;

use alloc::vec::Vec;

use magnolia_applet::errno::{Error, Result};
use magnolia_applet::sys;

fn test_allocator() -> Result<()> {
    let mut v = Vec::new();
    for i in 0..64u8 {
        v.push(i);
    }
    if v.len() != 64 || v[0] != 0 || v[63] != 63 {
        return Err(Error { errno: 5 }); // EIO
    }
    Ok(())
}

fn test_vfs_rw() -> Result<()> {
    let path = "/flash/rselftest_tmp";
    let file = magnolia_applet::fs::File::open(path, sys::O_CREAT | sys::O_TRUNC | sys::O_RDWR, 0o666)?;
    file.write_all(b"magnolia")?;

    let off = unsafe { sys::lseek(file.fd(), 0, sys::SEEK_SET) };
    if off < 0 {
        return Err(Error::last());
    }

    let mut buf = [0u8; 16];
    let n = file.read(&mut buf)?;
    if &buf[..n] != b"magnolia" {
        return Err(Error { errno: 5 }); // EIO
    }

    drop(file);
    magnolia_applet::fs::unlink(path)?;
    Ok(())
}

fn test_error_path() -> Result<()> {
    let rc = magnolia_applet::fs::File::open("/flash/no_such_file", sys::O_RDONLY, 0);
    if rc.is_ok() {
        return Err(Error { errno: 5 }); // EIO
    }
    if magnolia_applet::errno() == 0 {
        return Err(Error { errno: 5 }); // EIO
    }
    Ok(())
}

fn main(_args: magnolia_applet::Args) -> i32 {
    magnolia_applet::println!("rselftest start");

    let mut fails = 0;

    if let Err(e) = test_allocator() {
        fails += 1;
        magnolia_applet::eprintln!("allocator test failed: {:?}", e);
    } else {
        magnolia_applet::println!("allocator test ok");
    }

    if let Err(e) = test_vfs_rw() {
        fails += 1;
        magnolia_applet::eprintln!("vfs test failed: {:?}", e);
    } else {
        magnolia_applet::println!("vfs test ok");
    }

    if let Err(e) = test_error_path() {
        fails += 1;
        magnolia_applet::eprintln!("error-path test failed: {:?}", e);
    } else {
        magnolia_applet::println!("error-path test ok errno={}", magnolia_applet::errno());
    }

    magnolia_applet::println!("rselftest finished fails={}", fails);
    if fails == 0 { 0 } else { 1 }
}

magnolia_applet::entry!(main);

