use core::ffi::CStr;

use crate::sys::c_char;

#[derive(Copy, Clone)]
pub struct Args {
    argc: usize,
    argv: *const *const c_char,
}

impl Args {
    pub unsafe fn from_raw(argc: i32, argv: *const *const c_char) -> Self {
        let argc = if argc > 0 { argc as usize } else { 0 };
        Self { argc, argv }
    }

    pub fn len(&self) -> usize {
        self.argc
    }

    pub fn is_empty(&self) -> bool {
        self.argc == 0
    }

    pub fn get(&self, index: usize) -> Option<&CStr> {
        if index >= self.argc {
            return None;
        }
        if self.argv.is_null() {
            return None;
        }
        unsafe {
            let ptr = *self.argv.add(index);
            if ptr.is_null() {
                None
            } else {
                Some(CStr::from_ptr(ptr))
            }
        }
    }

    pub fn iter(&self) -> ArgsIter<'_> {
        ArgsIter { args: self, index: 0 }
    }
}

pub struct ArgsIter<'a> {
    args: &'a Args,
    index: usize,
}

impl<'a> Iterator for ArgsIter<'a> {
    type Item = &'a CStr;

    fn next(&mut self) -> Option<Self::Item> {
        let item = self.args.get(self.index)?;
        self.index += 1;
        Some(item)
    }
}

