use crate::sys;

pub fn sleep(seconds: u32) -> u32 {
    unsafe { sys::sleep(seconds) }
}

pub fn usleep(usec: u32) -> i32 {
    unsafe { sys::usleep(usec) }
}

