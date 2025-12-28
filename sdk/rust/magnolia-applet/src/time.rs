use core::time::Duration;

use crate::errno::{Error, Result};
use crate::sys;

pub fn sleep(seconds: u32) -> u32 {
    unsafe { sys::sleep(seconds) }
}

pub fn usleep(usec: u32) -> i32 {
    unsafe { sys::usleep(usec) }
}

pub fn nanosleep(dur: Duration) -> Result<()> {
    let req = sys::timespec {
        tv_sec: dur.as_secs() as sys::time_t,
        tv_nsec: dur.subsec_nanos() as sys::c_long,
    };
    let rc = unsafe { sys::nanosleep(&req, core::ptr::null_mut()) };
    if rc != 0 {
        return Err(Error::last());
    }
    Ok(())
}

pub fn clock_gettime(clock_id: i32) -> Result<sys::timespec> {
    let mut ts = sys::timespec { tv_sec: 0, tv_nsec: 0 };
    let rc = unsafe { sys::clock_gettime(clock_id, &mut ts) };
    if rc != 0 {
        return Err(Error::last());
    }
    Ok(ts)
}

pub fn monotonic_time() -> Result<sys::timespec> {
    clock_gettime(sys::CLOCK_MONOTONIC)
}

pub fn realtime_time() -> Result<sys::timespec> {
    clock_gettime(sys::CLOCK_REALTIME)
}

pub fn monotonic_duration() -> Result<Duration> {
    let ts = monotonic_time()?;
    Ok(Duration::new(ts.tv_sec as u64, ts.tv_nsec as u32))
}

pub fn gettimeofday() -> Result<sys::timeval> {
    let mut tv = sys::timeval { tv_sec: 0, tv_usec: 0 };
    let rc = unsafe { sys::gettimeofday(&mut tv, core::ptr::null_mut()) };
    if rc != 0 {
        return Err(Error::last());
    }
    Ok(tv)
}

pub fn time() -> sys::time_t {
    unsafe { sys::time(core::ptr::null_mut()) }
}
