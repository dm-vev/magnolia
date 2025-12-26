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

