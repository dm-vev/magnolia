use crate::sys;

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct Error {
    pub errno: i32,
}

pub type Result<T> = core::result::Result<T, Error>;

impl Error {
    pub fn last() -> Self {
        Self { errno: errno() }
    }
}

pub fn errno() -> i32 {
    unsafe {
        let ptr = sys::__errno();
        if ptr.is_null() {
            0
        } else {
            *ptr
        }
    }
}

pub const EINVAL: i32 = 22;
pub const ENOENT: i32 = 2;
pub const ENOSPC: i32 = 28;
pub const EIO: i32 = 5;
pub const EINTR: i32 = 4;
pub const EAGAIN: i32 = 11;
pub const ENOTSUP: i32 = 95;
pub const ETIMEDOUT: i32 = 110;
pub const ECONNREFUSED: i32 = 111;
