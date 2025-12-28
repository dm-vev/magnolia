use alloc::ffi::CString;
use alloc::vec::Vec;
use core::ffi::{c_char, CStr};

use crate::errno::{Error, Result};
use crate::sys;

#[derive(Debug)]
pub struct Dir {
    handle: *mut sys::DIR,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DirEntry {
    pub d_type: u8,
    pub name: Vec<u8>,
}

impl Dir {
    pub fn open_cstr(path: &CStr) -> Result<Self> {
        let handle = unsafe { sys::opendir(path.as_ptr()) };
        if handle.is_null() {
            return Err(Error::last());
        }
        Ok(Self { handle })
    }

    pub fn open(path: &str) -> Result<Self> {
        let cpath = CString::new(path).map_err(|_| Error { errno: 22 })?; // EINVAL
        Self::open_cstr(&cpath)
    }

    pub fn rewind(&mut self) {
        unsafe { sys::rewinddir(self.handle) }
    }

    pub fn next(&mut self) -> Result<Option<DirEntry>> {
        unsafe {
            let ep = sys::__errno();
            if !ep.is_null() {
                *ep = 0;
            }

            let ent = sys::readdir(self.handle);
            if ent.is_null() {
                let e = if ep.is_null() { 0 } else { *ep };
                if e == 0 {
                    return Ok(None);
                }
                return Err(Error { errno: e });
            }

            let name_ptr = (*ent).d_name.as_ptr() as *const c_char;
            let name = CStr::from_ptr(name_ptr).to_bytes().to_vec();
            Ok(Some(DirEntry {
                d_type: (*ent).d_type,
                name,
            }))
        }
    }
}

impl Drop for Dir {
    fn drop(&mut self) {
        if !self.handle.is_null() {
            unsafe {
                let _ = sys::closedir(self.handle);
            }
            self.handle = core::ptr::null_mut();
        }
    }
}

