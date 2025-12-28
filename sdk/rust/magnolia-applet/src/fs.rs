use alloc::ffi::CString;
use alloc::vec::Vec;
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

pub fn chdir(path: &str) -> Result<()> {
    let cpath = CString::new(path).map_err(|_| Error { errno: 22 })?; // EINVAL
    let rc = unsafe { sys::chdir(cpath.as_ptr()) };
    if rc != 0 {
        return Err(Error::last());
    }
    Ok(())
}

pub fn getcwd() -> Result<Vec<u8>> {
    let mut buf = Vec::new();
    buf.resize(512, 0u8);
    let p = unsafe { sys::getcwd(buf.as_mut_ptr().cast(), buf.len()) };
    if p.is_null() {
        return Err(Error::last());
    }

    let len = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
    buf.truncate(len);
    Ok(buf)
}

pub fn unlink(path: &str) -> Result<()> {
    let cpath = CString::new(path).map_err(|_| Error { errno: 22 })?; // EINVAL
    let rc = unsafe { sys::unlink(cpath.as_ptr()) };
    if rc != 0 {
        return Err(Error::last());
    }
    Ok(())
}

pub fn mkdir(path: &str, mode: sys::mode_t) -> Result<()> {
    let cpath = CString::new(path).map_err(|_| Error { errno: 22 })?; // EINVAL
    let rc = unsafe { sys::mkdir(cpath.as_ptr(), mode) };
    if rc != 0 {
        return Err(Error::last());
    }
    Ok(())
}

pub fn remove(path: &str) -> Result<()> {
    let cpath = CString::new(path).map_err(|_| Error { errno: 22 })?; // EINVAL
    let rc = unsafe { sys::remove(cpath.as_ptr()) };
    if rc != 0 {
        return Err(Error::last());
    }
    Ok(())
}

pub fn is_dir(path: &str) -> Result<bool> {
    match crate::dir::Dir::open(path) {
        Ok(_) => Ok(true),
        Err(err) => {
            if err.errno == 2 || err.errno == 20 {
                Ok(false)
            } else {
                Err(err)
            }
        }
    }
}

fn copy_file(src_path: &str, dst_path: &str) -> Result<()> {
    let src = File::open(src_path, sys::O_RDONLY, 0)?;
    let dst = File::open(
        dst_path,
        sys::O_WRONLY | sys::O_CREAT | sys::O_TRUNC,
        0o666 as sys::mode_t,
    )?;
    let mut buf = [0u8; 512];
    loop {
        let n = src.read(&mut buf)?;
        if n == 0 {
            break;
        }
        dst.write_all(&buf[..n])?;
    }
    Ok(())
}

pub fn rename(old_path: &str, new_path: &str) -> Result<()> {
    if old_path == new_path {
        return Ok(());
    }
    copy_file(old_path, new_path)?;
    unlink(old_path)?;
    Ok(())
}

pub fn exists(path: &str) -> bool {
    let Ok(cpath) = CString::new(path) else {
        return false;
    };
    unsafe { sys::access(cpath.as_ptr(), sys::F_OK) == 0 }
}

pub fn access(path: &str, mode: i32) -> Result<()> {
    let cpath = CString::new(path).map_err(|_| Error { errno: 22 })?; // EINVAL
    let rc = unsafe { sys::access(cpath.as_ptr(), mode) };
    if rc != 0 {
        return Err(Error::last());
    }
    Ok(())
}
