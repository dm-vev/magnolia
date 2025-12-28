/*
 * Magnolia OS - Edit Applet (Utilities)
 * Copyright (c) 2025 Magnolia Project
 * All rights reserved.
 */
use alloc::string::String;
use alloc::vec::Vec;

pub fn find_subslice(hay: &[u8], needle: &[u8]) -> Option<usize> {
    if needle.is_empty() {
        return Some(0);
    }
    if needle.len() > hay.len() {
        return None;
    }
    for i in 0..=hay.len() - needle.len() {
        if &hay[i..i + needle.len()] == needle {
            return Some(i);
        }
    }
    None
}

pub fn trim_ascii_space(bytes: &[u8]) -> &[u8] {
    let mut start = 0;
    let mut end = bytes.len();
    while start < end {
        match bytes[start] {
            b' ' | b'\t' | b'\r' | b'\n' => start += 1,
            _ => break,
        }
    }
    while end > start {
        match bytes[end - 1] {
            b' ' | b'\t' | b'\r' | b'\n' => end -= 1,
            _ => break,
        }
    }
    &bytes[start..end]
}

pub fn push_usize_str(out: &mut String, mut value: usize) {
    let mut buf = [0u8; 20];
    let mut i = buf.len();
    if value == 0 {
        out.push('0');
        return;
    }
    while value > 0 {
        i -= 1;
        buf[i] = b'0' + (value % 10) as u8;
        value /= 10;
    }
    // SAFETY: buf contains only ASCII digits.
    let s = unsafe { core::str::from_utf8_unchecked(&buf[i..]) };
    out.push_str(s);
}

pub fn push_usize_bytes(out: &mut Vec<u8>, mut value: usize) {
    let mut buf = [0u8; 20];
    let mut i = buf.len();
    if value == 0 {
        out.push(b'0');
        return;
    }
    while value > 0 {
        i -= 1;
        buf[i] = b'0' + (value % 10) as u8;
        value /= 10;
    }
    out.extend_from_slice(&buf[i..]);
}

pub fn abs_isize_to_usize(value: isize) -> usize {
    if value < 0 {
        value.wrapping_neg() as usize
    } else {
        value as usize
    }
}
