use alloc::ffi::CString;
use alloc::vec::Vec;

use crate::errno::{Error, Result};
use crate::sys;

fn cstring_from_str(value: &str) -> Result<CString> {
    CString::new(value).map_err(|_| Error { errno: 22 })
}

pub fn run_file(path: &str, args: &[&str]) -> Result<i32> {
    let mut cstrings: Vec<CString> = Vec::with_capacity(args.len() + 1);
    cstrings.push(cstring_from_str(path)?);
    for arg in args {
        cstrings.push(cstring_from_str(arg)?);
    }

    let mut argv: Vec<*mut sys::c_char> = cstrings
        .iter()
        .map(|s| s.as_ptr() as *mut sys::c_char)
        .collect();

    let mut rc: sys::c_int = 0;
    let ret = unsafe {
        sys::m_elf_run_file(
            cstrings[0].as_ptr(),
            argv.len() as sys::c_int,
            argv.as_mut_ptr(),
            &mut rc,
        )
    };
    if ret < 0 {
        return Err(Error { errno: -ret });
    }
    Ok(rc)
}
