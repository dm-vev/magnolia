#![no_std]
#![cfg_attr(target_os = "none", feature(alloc_error_handler))]

extern crate alloc;

pub use magnolia_applet_sys as sys;

pub mod args;
pub mod cli;
pub mod dir;
pub mod errno;
pub mod elf;
pub mod fs;
pub mod gpio;
pub mod io;
pub mod net;
pub mod rt;
pub mod sysctl;
pub mod time;

pub use args::Args;
pub use errno::{errno, Error, Result};
pub use gpio::Gpio;

#[macro_export]
macro_rules! entry {
    ($main:path) => {
        #[no_mangle]
        pub extern "C" fn app_main(argc: i32, argv: *const *const $crate::sys::c_char) -> i32 {
            $main(unsafe { $crate::Args::from_raw(argc, argv) })
        }
    };
}
