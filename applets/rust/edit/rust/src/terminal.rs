/*
 * Magnolia OS - Edit Applet (Terminal Renderer)
 * Copyright (c) 2025 Magnolia Project
 * All rights reserved.
 */
use alloc::vec::Vec;
use core::cmp::min;

use magnolia_applet::io;
use magnolia_applet::sys;

use crate::input::Input;
use crate::util::push_usize_bytes;

pub(crate) struct ScreenLine {
    pub flag: u8,
    pub text: Vec<u8>,
}

pub struct Terminal {
    pub(crate) width: usize,
    pub(crate) height: usize,
    pub(crate) scrbuf: Vec<ScreenLine>,
}

impl Terminal {
    pub fn new(input: &mut Input) -> Self {
        let (height, width) = Terminal::get_screen_size(input);
        let height = height.saturating_sub(1);
        let mut scrbuf = Vec::new();
        for _ in 0..height {
            scrbuf.push(ScreenLine {
                flag: 0xff,
                text: Vec::new(),
            });
        }
        Self {
            width,
            height,
            scrbuf,
        }
    }

    pub fn write_bytes(&self, bytes: &[u8]) {
        let _ = io::write_all(sys::STDOUT_FILENO, bytes);
    }

    pub fn write_str(&self, s: &str) {
        self.write_bytes(s.as_bytes());
    }

    pub fn goto(&self, row: usize, col: usize) {
        let mut buf = Vec::with_capacity(16);
        buf.extend_from_slice(b"\x1b[");
        push_usize_bytes(&mut buf, row + 1);
        buf.push(b';');
        push_usize_bytes(&mut buf, col + 1);
        buf.push(b'H');
        self.write_bytes(&buf);
    }

    pub fn clear_to_eol(&self) {
        self.write_str("\x1b[0K");
    }

    pub fn clear_screen(&self) {
        self.write_str("\x1b[2J\x1b[H");
    }

    pub fn cursor(&self, on: bool) {
        if on {
            self.write_str("\x1b[?25h");
        } else {
            self.write_str("\x1b[?25l");
        }
    }

    pub fn hilite(&self, mode: u8) {
        match mode {
            1 => self.write_str("\x1b[1;37;44m"),
            2 => self.write_str("\x1b[43m"),
            _ => self.write_str("\x1b[0m"),
        }
    }

    pub fn mouse_reporting(&self, on: bool) {
        if on {
            self.write_str("\x1b[?9h");
        } else {
            self.write_str("\x1b[?9l");
        }
    }

    pub fn scroll_region(&self, stop: usize) {
        if stop > 0 {
            let mut buf = Vec::with_capacity(12);
            buf.extend_from_slice(b"\x1b[1;");
            push_usize_bytes(&mut buf, stop);
            buf.push(b'r');
            self.write_bytes(&buf);
        } else {
            self.write_str("\x1b[r");
        }
    }

    pub fn scroll_up(&mut self, scrolling: usize) {
        if scrolling == 0 || self.height == 0 {
            return;
        }
        let scrolling = min(scrolling, self.scrbuf.len());
        self.scrbuf.rotate_left(scrolling);
        for i in (self.scrbuf.len() - scrolling)..self.scrbuf.len() {
            self.scrbuf[i].flag = 0xff;
            self.scrbuf[i].text.clear();
        }
        self.goto(0, 0);
        for _ in 0..scrolling {
            self.write_str("\x1bM");
        }
    }

    pub fn scroll_down(&mut self, scrolling: usize) {
        if scrolling == 0 || self.height == 0 {
            return;
        }
        let scrolling = min(scrolling, self.scrbuf.len());
        self.scrbuf.rotate_right(scrolling);
        for i in 0..scrolling {
            self.scrbuf[i].flag = 0xff;
            self.scrbuf[i].text.clear();
        }
        self.goto(self.height.saturating_sub(1), 0);
        for _ in 0..scrolling {
            self.write_str("\n");
        }
    }

    pub fn redraw(&mut self, input: &mut Input) {
        self.cursor(false);
        let (height, width) = Terminal::get_screen_size(input);
        self.width = width;
        self.height = height.saturating_sub(1);
        self.scrbuf.clear();
        for _ in 0..self.height {
            self.scrbuf.push(ScreenLine {
                flag: 0xff,
                text: Vec::new(),
            });
        }
        self.scroll_region(self.height);
        self.mouse_reporting(false);
    }

    fn get_screen_size(input: &mut Input) -> (usize, usize) {
        let _ = io::write_all(sys::STDOUT_FILENO, b"\x1b[999;999H\x1b[6n");
        let mut pos = Vec::new();
        loop {
            let b = input.read_byte();
            if b == b'R' {
                break;
            }
            pos.push(b);
            if pos.len() > 32 {
                break;
            }
        }
        let mut row = 24usize;
        let mut col = 80usize;
        if pos.starts_with(b"\x1b[") {
            let mut nums = Vec::new();
            let mut current = 0usize;
            let mut has = false;
            for &b in &pos[2..] {
                if b.is_ascii_digit() {
                    current = current * 10 + (b - b'0') as usize;
                    has = true;
                } else if b == b';' {
                    nums.push(current);
                    current = 0;
                    has = false;
                }
            }
            if has {
                nums.push(current);
            }
            if nums.len() >= 2 {
                row = nums[0];
                col = nums[1];
            }
        }
        (row, col)
    }
}
