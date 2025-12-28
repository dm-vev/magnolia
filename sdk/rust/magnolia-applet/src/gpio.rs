use core::mem::size_of;

use crate::errno::{Error, Result};
use crate::fs::File;
use crate::sys;

pub struct Gpio {
    file: File,
}

impl Gpio {
    pub fn open() -> Result<Self> {
        let file = File::open("/dev/gpio", sys::O_RDWR, 0)?;
        Ok(Self { file })
    }

    pub fn configure(&self, cfg: &sys::devfs_gpio_config_t) -> Result<()> {
        let rc = unsafe { sys::ioctl(self.file.fd(), sys::DEVFS_IOCTL_GPIO_CONFIG, cfg) };
        if rc != 0 {
            return Err(Error::last());
        }
        Ok(())
    }

    pub fn read_values(&self, pin_mask: u64) -> Result<u64> {
        let mut req = sys::devfs_gpio_values_t {
            pin_mask,
            values: 0,
        };
        let rc = unsafe { sys::ioctl(self.file.fd(), sys::DEVFS_IOCTL_GPIO_READ, &mut req) };
        if rc != 0 {
            return Err(Error::last());
        }
        Ok(req.values)
    }

    pub fn write_values(&self, pin_mask: u64, values: u64) -> Result<()> {
        let mut req = sys::devfs_gpio_values_t { pin_mask, values };
        let rc = unsafe { sys::ioctl(self.file.fd(), sys::DEVFS_IOCTL_GPIO_WRITE, &mut req) };
        if rc != 0 {
            return Err(Error::last());
        }
        Ok(())
    }

    pub fn subscribe_edges(&self, pin_mask: u64, edge: u8, debounce_us: u32) -> Result<()> {
        let req = sys::devfs_gpio_edge_config_t {
            pin_mask,
            edge,
            reserved: [0; 3],
            debounce_us,
        };
        let rc = unsafe {
            sys::ioctl(self.file.fd(), sys::DEVFS_IOCTL_GPIO_EDGE_SUBSCRIBE, &req)
        };
        if rc != 0 {
            return Err(Error::last());
        }
        Ok(())
    }

    pub fn unsubscribe_edges(&self, pin_mask: u64) -> Result<()> {
        let req = sys::devfs_gpio_edge_config_t {
            pin_mask,
            edge: sys::DEVFS_GPIO_EDGE_NONE,
            reserved: [0; 3],
            debounce_us: 0,
        };
        let rc = unsafe {
            sys::ioctl(self.file.fd(), sys::DEVFS_IOCTL_GPIO_EDGE_UNSUBSCRIBE, &req)
        };
        if rc != 0 {
            return Err(Error::last());
        }
        Ok(())
    }

    pub fn read_events(&self, out: &mut [sys::devfs_gpio_event_t]) -> Result<usize> {
        if out.is_empty() {
            return Ok(0);
        }
        let byte_len = out.len() * size_of::<sys::devfs_gpio_event_t>();
        let buf = unsafe {
            core::slice::from_raw_parts_mut(
                out.as_mut_ptr().cast::<u8>(),
                byte_len,
            )
        };
        let n = self.file.read(buf)?;
        if n % size_of::<sys::devfs_gpio_event_t>() != 0 {
            return Err(Error { errno: 5 }); // EIO
        }
        Ok(n / size_of::<sys::devfs_gpio_event_t>())
    }
}
