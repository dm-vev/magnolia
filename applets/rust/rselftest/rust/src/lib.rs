#![no_std]

extern crate alloc;

use alloc::vec::Vec;
use core::time::Duration;

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

fn test_dir_iter() -> Result<()> {
    let dir_path = "/flash/rselftest_dir";
    let file_a = "/flash/rselftest_dir/a";
    let file_b = "/flash/rselftest_dir/b";

    if let Err(e) = magnolia_applet::fs::mkdir(dir_path, 0o777) {
        if e.errno != 17 {
            return Err(e); // EEXIST is ok
        }
    };

    let f = magnolia_applet::fs::File::open(file_a, sys::O_CREAT | sys::O_TRUNC | sys::O_RDWR, 0o666)?;
    f.write_all(b"a")?;
    drop(f);
    let f = magnolia_applet::fs::File::open(file_b, sys::O_CREAT | sys::O_TRUNC | sys::O_RDWR, 0o666)?;
    f.write_all(b"b")?;
    drop(f);

    let mut dir = magnolia_applet::dir::Dir::open(dir_path)?;
    let mut seen_a = false;
    let mut seen_b = false;
    while let Some(ent) = dir.next()? {
        if ent.name.as_slice() == b"a" {
            seen_a = true;
        }
        if ent.name.as_slice() == b"b" {
            seen_b = true;
        }
    }
    if !seen_a || !seen_b {
        return Err(Error { errno: 5 }); // EIO
    }

    magnolia_applet::fs::unlink(file_a)?;
    magnolia_applet::fs::unlink(file_b)?;
    let _ = magnolia_applet::fs::remove(dir_path);
    Ok(())
}

fn test_time() -> Result<()> {
    let t0 = magnolia_applet::time::monotonic_duration()?;
    magnolia_applet::time::nanosleep(Duration::from_millis(5))?;
    let t1 = magnolia_applet::time::monotonic_duration()?;
    if t1 <= t0 {
        return Err(Error { errno: 5 }); // EIO
    }
    Ok(())
}

fn test_cwd() -> Result<()> {
    let before = magnolia_applet::fs::getcwd()?;
    magnolia_applet::fs::chdir("/")?;
    let after = magnolia_applet::fs::getcwd()?;
    if after.as_slice() != b"/" {
        return Err(Error { errno: 5 }); // EIO
    }
    let before_s = core::str::from_utf8(before.as_slice()).unwrap_or("/");
    magnolia_applet::fs::chdir(before_s)?;
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

    if let Err(e) = test_dir_iter() {
        fails += 1;
        magnolia_applet::eprintln!("dir test failed: {:?}", e);
    } else {
        magnolia_applet::println!("dir test ok");
    }

    if let Err(e) = test_time() {
        fails += 1;
        magnolia_applet::eprintln!("time test failed: {:?}", e);
    } else {
        magnolia_applet::println!("time test ok");
    }

    if let Err(e) = test_cwd() {
        fails += 1;
        magnolia_applet::eprintln!("cwd test failed: {:?}", e);
    } else {
        magnolia_applet::println!("cwd test ok");
    }

    magnolia_applet::println!("rselftest finished fails={}", fails);
    if fails == 0 { 0 } else { 1 }
}

magnolia_applet::entry!(main);
