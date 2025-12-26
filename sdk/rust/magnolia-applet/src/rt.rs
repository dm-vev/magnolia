use core::alloc::{GlobalAlloc, Layout};
use core::ptr;

use crate::sys;

const HEADER_SIZE: usize = core::mem::size_of::<usize>();

pub struct MagnoliaAlloc;

unsafe impl GlobalAlloc for MagnoliaAlloc {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        if layout.size() == 0 {
            return layout.align() as *mut u8;
        }

        let align = layout.align().max(HEADER_SIZE);
        let size = layout.size();
        let total = match size
            .checked_add(align)
            .and_then(|v| v.checked_add(HEADER_SIZE))
        {
            Some(v) => v,
            None => return ptr::null_mut(),
        };

        let base = sys::malloc(total) as *mut u8;
        if base.is_null() {
            return ptr::null_mut();
        }

        let start = base.add(HEADER_SIZE);
        let aligned = ((start as usize + (align - 1)) & !(align - 1)) as *mut u8;
        let header = aligned.sub(HEADER_SIZE);
        ptr::write_unaligned(header as *mut usize, base as usize);
        aligned
    }

    unsafe fn dealloc(&self, ptr: *mut u8, _layout: Layout) {
        if _layout.size() == 0 {
            return;
        }
        if ptr.is_null() {
            return;
        }
        let header = ptr.sub(HEADER_SIZE);
        let base = ptr::read_unaligned(header as *const usize) as *mut sys::c_void;
        sys::free(base);
    }
}

#[cfg(target_os = "none")]
#[global_allocator]
static GLOBAL: MagnoliaAlloc = MagnoliaAlloc;

#[cfg(target_os = "none")]
#[alloc_error_handler]
fn alloc_error(_layout: Layout) -> ! {
    unsafe { sys::abort() }
}

#[cfg(target_os = "none")]
#[panic_handler]
fn panic(info: &core::panic::PanicInfo) -> ! {
    crate::eprintln!("panic: {:?}", info);
    unsafe { sys::abort() }
}
