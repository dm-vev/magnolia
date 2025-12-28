/*
 * Magnolia OS - Edit Applet (TTY Guard)
 * Copyright (c) 2025 Magnolia Project
 * All rights reserved.
 */
use magnolia_applet::sys;

const DEVFS_IOCTL_TTY_SET_MODE: sys::c_ulong = 0x30;
const DEVFS_IOCTL_TTY_GET_MODE: sys::c_ulong = 0x31;

#[repr(C)]
#[derive(Copy, Clone)]
struct DevfsTtyMode {
    echo: bool,
    canonical: bool,
}

pub struct TtyGuard {
    saved: Option<DevfsTtyMode>,
}

impl TtyGuard {
    pub fn new() -> Self {
        let mut saved = DevfsTtyMode {
            echo: true,
            canonical: true,
        };
        let rc = unsafe { sys::ioctl(sys::STDIN_FILENO, DEVFS_IOCTL_TTY_GET_MODE, &mut saved) };
        if rc == 0 {
            let raw = DevfsTtyMode {
                echo: false,
                canonical: false,
            };
            let _ = unsafe { sys::ioctl(sys::STDIN_FILENO, DEVFS_IOCTL_TTY_SET_MODE, &raw) };
            Self { saved: Some(saved) }
        } else {
            Self { saved: None }
        }
    }
}

impl Drop for TtyGuard {
    fn drop(&mut self) {
        if let Some(saved) = self.saved {
            let _ = unsafe { sys::ioctl(sys::STDIN_FILENO, DEVFS_IOCTL_TTY_SET_MODE, &saved) };
        }
    }
}
