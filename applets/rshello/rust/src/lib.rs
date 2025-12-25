#![no_std]

#[no_mangle]
pub extern "C" fn rshello_fill(buf: *mut u8, len: usize) {
    if buf.is_null() || len < 13 {
        return;
    }

    unsafe {
        *buf.add(0) = b'H';
        *buf.add(1) = b'e';
        *buf.add(2) = b'l';
        *buf.add(3) = b'l';
        *buf.add(4) = b'o';
        *buf.add(5) = b' ';
        *buf.add(6) = b'w';
        *buf.add(7) = b'o';
        *buf.add(8) = b'r';
        *buf.add(9) = b'l';
        *buf.add(10) = b'd';
        *buf.add(11) = b'!';
        *buf.add(12) = b'\n';
    }
}

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}
