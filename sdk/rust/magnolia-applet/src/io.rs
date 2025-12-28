use core::fmt;
use core::fmt::Write;

use crate::errno::{Error, Result};
use crate::sys;

pub fn isatty(fd: i32) -> bool {
    unsafe { sys::isatty(fd) == 1 }
}

pub fn dup(fd: i32) -> Result<i32> {
    let out = unsafe { sys::dup(fd) };
    if out < 0 {
        return Err(Error::last());
    }
    Ok(out)
}

pub fn dup2(oldfd: i32, newfd: i32) -> Result<i32> {
    let out = unsafe { sys::dup2(oldfd, newfd) };
    if out < 0 {
        return Err(Error::last());
    }
    Ok(out)
}

pub fn write_all(fd: i32, mut buf: &[u8]) -> Result<()> {
    while !buf.is_empty() {
        let n = unsafe { sys::write(fd, buf.as_ptr().cast(), buf.len()) };
        if n < 0 {
            return Err(Error::last());
        }
        if n == 0 {
            return Err(Error { errno: 5 }); // EIO
        }
        buf = &buf[n as usize..];
    }
    Ok(())
}

pub fn read(fd: i32, buf: &mut [u8]) -> Result<usize> {
    let n = unsafe { sys::read(fd, buf.as_mut_ptr().cast(), buf.len()) };
    if n < 0 {
        return Err(Error::last());
    }
    Ok(n as usize)
}

pub fn read_exact(fd: i32, mut buf: &mut [u8]) -> Result<()> {
    while !buf.is_empty() {
        let n = read(fd, buf)?;
        if n == 0 {
            return Err(Error { errno: 5 }); // EIO
        }
        let tmp = buf;
        buf = &mut tmp[n..];
    }
    Ok(())
}

pub struct Stdout;
pub struct Stderr;

impl fmt::Write for Stdout {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        write_all(sys::STDOUT_FILENO, s.as_bytes()).map_err(|_| fmt::Error)
    }
}

impl fmt::Write for Stderr {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        write_all(sys::STDERR_FILENO, s.as_bytes()).map_err(|_| fmt::Error)
    }
}

#[doc(hidden)]
pub fn _print(args: fmt::Arguments) {
    let _ = Stdout.write_fmt(args);
}

#[doc(hidden)]
pub fn _eprint(args: fmt::Arguments) {
    let _ = Stderr.write_fmt(args);
}

#[macro_export]
macro_rules! print {
    ($($arg:tt)*) => {{
        $crate::io::_print(core::format_args!($($arg)*));
    }};
}

#[macro_export]
macro_rules! println {
    () => {{
        $crate::print!("\n");
    }};
    ($fmt:expr) => {{
        $crate::print!(concat!($fmt, "\n"));
    }};
    ($fmt:expr, $($arg:tt)*) => {{
        $crate::print!(concat!($fmt, "\n"), $($arg)*);
    }};
}

#[macro_export]
macro_rules! eprint {
    ($($arg:tt)*) => {{
        $crate::io::_eprint(core::format_args!($($arg)*));
    }};
}

#[macro_export]
macro_rules! eprintln {
    () => {{
        $crate::eprint!("\n");
    }};
    ($fmt:expr) => {{
        $crate::eprint!(concat!($fmt, "\n"));
    }};
    ($fmt:expr, $($arg:tt)*) => {{
        $crate::eprint!(concat!($fmt, "\n"), $($arg)*);
    }};
}
