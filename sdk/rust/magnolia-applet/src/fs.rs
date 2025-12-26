use alloc::ffi::CString;
use core::ffi::CStr;

use crate::errno::{Error, Result};
use crate::sys;

#[derive(Debug)]
pub struct File {
    fd: i32,
}

impl File {
    pub fn open_cstr(path: &CStr, flags: i32, mode: sys::mode_t) -> Result<Self> {
        let fd = unsafe { sys::open(path.as_ptr(), flags, mode) };
        if fd < 0 {
            return Err(Error::last());
        }
        Ok(Self { fd })
    }

    pub fn open(path: &str, flags: i32, mode: sys::mode_t) -> Result<Self> {
        let cpath = CString::new(path).map_err(|_| Error { errno: 22 })?; // EINVAL
        Self::open_cstr(&cpath, flags, mode)
    }

    pub fn fd(&self) -> i32 {
        self.fd
    }

    pub fn read(&self, buf: &mut [u8]) -> Result<usize> {
        let n = unsafe { sys::read(self.fd, buf.as_mut_ptr().cast(), buf.len()) };
        if n < 0 {
            return Err(Error::last());
        }
        Ok(n as usize)
    }

    pub fn write(&self, buf: &[u8]) -> Result<usize> {
        let n = unsafe { sys::write(self.fd, buf.as_ptr().cast(), buf.len()) };
        if n < 0 {
            return Err(Error::last());
        }
        Ok(n as usize)
    }

    pub fn write_all(&self, mut buf: &[u8]) -> Result<()> {
        while !buf.is_empty() {
            let n = self.write(buf)?;
            if n == 0 {
                return Err(Error { errno: 5 }); // EIO
            }
            buf = &buf[n..];
        }
        Ok(())
    }
}

impl Drop for File {
    fn drop(&mut self) {
        if self.fd >= 0 {
            unsafe {
                let _ = sys::close(self.fd);
            }
            self.fd = -1;
        }
    }
}

pub fn unlink(path: &str) -> Result<()> {
    let cpath = CString::new(path).map_err(|_| Error { errno: 22 })?; // EINVAL
    let rc = unsafe { sys::unlink(cpath.as_ptr()) };
    if rc != 0 {
        return Err(Error::last());
    }
    Ok(())
}

