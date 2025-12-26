#![no_std]
#![cfg_attr(target_os = "none", feature(alloc_error_handler))]

extern crate alloc;

pub use magnolia_applet_sys as sys;

pub mod args;
pub mod errno;
pub mod fs;
pub mod io;
pub mod rt;
pub mod time;

pub use args::Args;
pub use errno::{errno, Error, Result};

#[macro_export]
macro_rules! entry {
    ($main:path) => {
        #[no_mangle]
        pub extern "C" fn app_main(argc: i32, argv: *const *const $crate::sys::c_char) -> i32 {
            $main(unsafe { $crate::Args::from_raw(argc, argv) })
        }
    };
}
