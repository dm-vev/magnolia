use alloc::ffi::CString;
use alloc::string::String;
use alloc::vec;
use alloc::vec::Vec;
use core::ffi::CStr;

use crate::errno::{Error, Result};
use crate::sys;

fn cstring_from_str(value: &str) -> Result<CString> {
    CString::new(value).map_err(|_| Error { errno: 22 })
}

fn cstr_to_string(ptr: *const sys::c_char) -> Result<String> {
    if ptr.is_null() {
        return Err(Error { errno: 22 });
    }
    let s = unsafe { CStr::from_ptr(ptr) };
    let s = s.to_str().map_err(|_| Error { errno: 22 })?;
    Ok(String::from(s))
}

fn rc_to_result(rc: i32) -> Result<()> {
    if rc < 0 {
        return Err(Error { errno: -rc });
    }
    Ok(())
}

pub fn get(key: &str) -> Result<String> {
    let ckey = cstring_from_str(key)?;
    let mut buf: Vec<sys::c_char> = vec![0; 256];
    let rc = unsafe { sys::m_sysctl_get(ckey.as_ptr(), buf.as_mut_ptr(), buf.len()) };
    rc_to_result(rc)?;
    let value = unsafe { CStr::from_ptr(buf.as_ptr()) };
    let value = value.to_str().map_err(|_| Error { errno: 22 })?;
    Ok(String::from(value))
}

pub fn set(key: &str, value: &str) -> Result<()> {
    let ckey = cstring_from_str(key)?;
    let cvalue = cstring_from_str(value)?;
    let rc = unsafe { sys::m_sysctl_set(ckey.as_ptr(), cvalue.as_ptr()) };
    rc_to_result(rc)
}

pub fn list(prefix: Option<&str>) -> Result<Vec<(String, String)>> {
    let mut items: Vec<(String, String)> = Vec::new();
    let mut context = SysctlListContext {
        items: &mut items,
        error: None,
    };

    let cprefix = if let Some(p) = prefix {
        Some(cstring_from_str(p)?)
    } else {
        None
    };

    let rc = unsafe {
        sys::m_sysctl_list(
            cprefix.as_ref().map(|s| s.as_ptr()).unwrap_or(core::ptr::null()),
            Some(sysctl_list_cb),
            &mut context as *mut _ as *mut sys::c_void,
        )
    };
    rc_to_result(rc)?;

    if let Some(err) = context.error.take() {
        return Err(err);
    }
    Ok(items)
}

struct SysctlListContext<'a> {
    items: &'a mut Vec<(String, String)>,
    error: Option<Error>,
}

unsafe extern "C" fn sysctl_list_cb(
    key: *const sys::c_char,
    value: *const sys::c_char,
    ctx: *mut sys::c_void,
) -> sys::c_int {
    if ctx.is_null() {
        return 1;
    }
    let context = &mut *(ctx as *mut SysctlListContext);
    let key = match cstr_to_string(key) {
        Ok(k) => k,
        Err(err) => {
            context.error = Some(err);
            return 1;
        }
    };
    let value = match cstr_to_string(value) {
        Ok(v) => v,
        Err(err) => {
            context.error = Some(err);
            return 1;
        }
    };
    context.items.push((key, value));
    0
}
