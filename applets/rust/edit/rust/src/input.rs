/*
 * Magnolia OS - Edit Applet (Input Layer)
 * Copyright (c) 2025 Magnolia Project
 * All rights reserved.
 */
use alloc::vec::Vec;

use magnolia_applet::io;
use magnolia_applet::sys;

use crate::key::*;

struct KeyMapEntry {
    seq: &'static [u8],
    key: Key,
}

const KEYMAP: &[KeyMapEntry] = &[
    KeyMapEntry { seq: b"\x1b[A", key: KEY_UP },
    KeyMapEntry { seq: b"\x1b[1;2A", key: KEY_SHIFT_UP },
    KeyMapEntry { seq: b"\x1b[1;3A", key: KEY_ALT_UP },
    KeyMapEntry { seq: b"\x1b[B", key: KEY_DOWN },
    KeyMapEntry { seq: b"\x1b[1;2B", key: KEY_SHIFT_DOWN },
    KeyMapEntry { seq: b"\x1b[1;3B", key: KEY_ALT_DOWN },
    KeyMapEntry { seq: b"\x1b[D", key: KEY_LEFT },
    KeyMapEntry { seq: b"\x1b[1;2D", key: KEY_SHIFT_LEFT },
    KeyMapEntry { seq: b"\x1b[1;6D", key: KEY_SHIFT_CTRL_LEFT },
    KeyMapEntry { seq: b"\x1b[1;3D", key: KEY_ALT_LEFT },
    KeyMapEntry { seq: b"\x1b[C", key: KEY_RIGHT },
    KeyMapEntry { seq: b"\x1b[1;2C", key: KEY_SHIFT_RIGHT },
    KeyMapEntry { seq: b"\x1b[1;6C", key: KEY_SHIFT_CTRL_RIGHT },
    KeyMapEntry { seq: b"\x1b[1;3C", key: KEY_ALT_RIGHT },
    KeyMapEntry { seq: b"\x1b[H", key: KEY_HOME },
    KeyMapEntry { seq: b"\x1bOH", key: KEY_HOME },
    KeyMapEntry { seq: b"\x1b[1~", key: KEY_HOME },
    KeyMapEntry { seq: b"\x1b[F", key: KEY_END },
    KeyMapEntry { seq: b"\x1bOF", key: KEY_END },
    KeyMapEntry { seq: b"\x1b[4~", key: KEY_END },
    KeyMapEntry { seq: b"\x1b[5~", key: KEY_PGUP },
    KeyMapEntry { seq: b"\x1b[6~", key: KEY_PGDN },
    KeyMapEntry { seq: b"\x1b[5;5~", key: KEY_PREV },
    KeyMapEntry { seq: b"\x1b[6;5~", key: KEY_NEXT },
    KeyMapEntry { seq: b"\x1b[1;5D", key: KEY_WORD_LEFT },
    KeyMapEntry { seq: b"\x1b[1;5C", key: KEY_WORD_RIGHT },
    KeyMapEntry { seq: b"\x03", key: KEY_COPY },
    KeyMapEntry { seq: b"\r", key: KEY_ENTER },
    KeyMapEntry { seq: b"\x7f", key: KEY_BACKSPACE },
    KeyMapEntry { seq: b"\x1b[3~", key: KEY_DELETE },
    KeyMapEntry { seq: b"\x1b[Z", key: KEY_BACKTAB },
    KeyMapEntry { seq: b"\x19", key: KEY_REDO },
    KeyMapEntry { seq: b"\x08", key: KEY_BACKSPACE },
    KeyMapEntry { seq: b"\x12", key: KEY_REPLC },
    KeyMapEntry { seq: b"\x11", key: KEY_QUIT },
    KeyMapEntry { seq: b"\n", key: KEY_ENTER },
    KeyMapEntry { seq: b"\x13", key: KEY_WRITE },
    KeyMapEntry { seq: b"\x1bOQ", key: KEY_WRITE },
    KeyMapEntry { seq: b"\x1b[12~", key: KEY_WRITE },
    KeyMapEntry { seq: b"\x1bs", key: KEY_WRITE },
    KeyMapEntry { seq: b"\x1bS", key: KEY_WRITE },
    KeyMapEntry { seq: b"\x06", key: KEY_FIND },
    KeyMapEntry { seq: b"\x0e", key: KEY_FIND_AGAIN },
    KeyMapEntry { seq: b"\x07", key: KEY_GOTO },
    KeyMapEntry { seq: b"\x05", key: KEY_REDRAW },
    KeyMapEntry { seq: b"\x1a", key: KEY_UNDO },
    KeyMapEntry { seq: b"\x09", key: KEY_TAB },
    KeyMapEntry { seq: b"\x15", key: KEY_BACKTAB },
    KeyMapEntry { seq: b"\x18", key: KEY_CUT },
    KeyMapEntry { seq: b"\x16", key: KEY_PASTE },
    KeyMapEntry { seq: b"\x04", key: KEY_UNDO_YANK },
    KeyMapEntry { seq: b"\x0c", key: KEY_MARK },
    KeyMapEntry { seq: b"\x00", key: KEY_MARK },
    KeyMapEntry { seq: b"\x14", key: KEY_FIRST },
    KeyMapEntry { seq: b"\x02", key: KEY_LAST },
    KeyMapEntry { seq: b"\x01", key: KEY_TOGGLE },
    KeyMapEntry { seq: b"\x17", key: KEY_NEXT },
    KeyMapEntry { seq: b"\x0f", key: KEY_GET },
    KeyMapEntry { seq: b"\x10", key: KEY_COMMENT },
    KeyMapEntry { seq: b"\x1f", key: KEY_COMMENT },
    KeyMapEntry { seq: b"\x1b[1;5A", key: KEY_SCRLUP },
    KeyMapEntry { seq: b"\x1b[1;5B", key: KEY_SCRLDN },
    KeyMapEntry { seq: b"\x1b[1;5H", key: KEY_FIRST },
    KeyMapEntry { seq: b"\x1b[1;5F", key: KEY_LAST },
    KeyMapEntry { seq: b"\x1b[3;5~", key: KEY_DEL_WORD },
    KeyMapEntry { seq: b"\x1b[3;2~", key: KEY_DEL_LINE },
    KeyMapEntry { seq: b"\x0b", key: KEY_MATCH },
    KeyMapEntry { seq: b"\x1b[M", key: KEY_MOUSE },
    KeyMapEntry { seq: b"\x1b[2;3~", key: KEY_PLACE },
    KeyMapEntry { seq: b"\x1b[5;3~", key: KEY_PREV_PLACE },
    KeyMapEntry { seq: b"\x1b[6;3~", key: KEY_NEXT_PLACE },
    KeyMapEntry { seq: b"\x1b[1;3H", key: KEY_UNDO_PREV },
    KeyMapEntry { seq: b"\x1b[1;3F", key: KEY_UNDO_NEXT },
];

#[derive(Clone, Debug)]
pub enum InputData {
    Char(Vec<u8>),
    Mouse { x: usize, y: usize, code: u8 },
}

#[derive(Clone, Debug)]
pub struct InputEvent {
    pub key: Key,
    pub data: Option<InputData>,
}

pub struct Input {
    buf: Vec<u8>,
}

impl Input {
    pub fn new() -> Self {
        Self { buf: Vec::new() }
    }

    fn fill(&mut self) {
        let mut tmp = [0u8; 64];
        if let Ok(n) = io::read(sys::STDIN_FILENO, &mut tmp) {
            if n > 0 {
                self.buf.extend_from_slice(&tmp[..n]);
            }
        }
    }

    fn ensure(&mut self) {
        if self.buf.is_empty() {
            self.fill();
        }
    }

    pub(crate) fn read_byte(&mut self) -> u8 {
        self.ensure();
        if self.buf.is_empty() {
            0
        } else {
            self.buf.remove(0)
        }
    }

    fn read_bytes(&mut self, count: usize) -> Vec<u8> {
        let mut out = Vec::with_capacity(count);
        while out.len() < count {
            self.ensure();
            if self.buf.is_empty() {
                break;
            }
            out.push(self.buf.remove(0));
        }
        out
    }

    fn key_for_seq(seq: &[u8]) -> Option<Key> {
        for entry in KEYMAP {
            if entry.seq == seq {
                return Some(entry.key);
            }
        }
        None
    }

    fn is_prefix(seq: &[u8]) -> bool {
        for entry in KEYMAP {
            if entry.seq.starts_with(seq) {
                return true;
            }
        }
        false
    }

    pub fn key_max() -> usize {
        let mut max_len = 0;
        for entry in KEYMAP {
            if entry.seq.len() > max_len {
                max_len = entry.seq.len();
            }
        }
        max_len
    }

    fn read_utf8_char(&mut self, first: u8) -> Vec<u8> {
        let mut out = vec![first];
        let needed = if first & 0x80 == 0 {
            0
        } else if first & 0xE0 == 0xC0 {
            1
        } else if first & 0xF0 == 0xE0 {
            2
        } else if first & 0xF8 == 0xF0 {
            3
        } else {
            0
        };
        if needed > 0 {
            out.extend_from_slice(&self.read_bytes(needed));
        }
        out
    }

    pub fn get_input(&mut self, key_max: usize) -> InputEvent {
        loop {
            self.ensure();
            if self.buf.is_empty() {
                continue;
            }
            let first = self.read_byte();
            if first == 0x1b {
                if self.buf.is_empty() {
                    return InputEvent { key: KEY_QUIT, data: None };
                }
                let mut seq = vec![first];
                loop {
                    if seq == b"\x1b\x1b" {
                        seq = vec![0x1b];
                        break;
                    }

                    if let Some(key) = Input::key_for_seq(&seq) {
                        if key == KEY_MOUSE {
                            let raw = self.read_bytes(3);
                            if raw.len() == 3 {
                                let mouse_fct = raw[0];
                                let mouse_x = raw[1].saturating_sub(33) as usize;
                                let mouse_y = raw[2].saturating_sub(33) as usize;
                                if mouse_fct == 0x61 {
                                    return InputEvent {
                                        key: KEY_SCRLDN,
                                        data: Some(InputData::Char(vec![3])),
                                    };
                                }
                                if mouse_fct == 0x60 {
                                    return InputEvent {
                                        key: KEY_SCRLUP,
                                        data: Some(InputData::Char(vec![3])),
                                    };
                                }
                                return InputEvent {
                                    key: KEY_MOUSE,
                                    data: Some(InputData::Mouse {
                                        x: mouse_x,
                                        y: mouse_y,
                                        code: mouse_fct,
                                    }),
                                };
                            }
                        } else {
                            return InputEvent { key, data: None };
                        }
                    }

                    if seq.len() >= key_max {
                        break;
                    }

                    if !Input::is_prefix(&seq) {
                        break;
                    }

                    if self.buf.is_empty() {
                        self.fill();
                        if self.buf.is_empty() {
                            break;
                        }
                    }

                    let next = self.read_byte();
                    if seq.len() == 1 && next.is_ascii_alphabetic() && next != b'O' {
                        if Input::key_for_seq(&[0x1b, next]).is_some() {
                            seq.push(next);
                        } else {
                            let mapped = next & 0x1f;
                            seq = vec![mapped];
                        }
                        break;
                    }
                    seq.push(next);
                    let last = *seq.last().unwrap_or(&0);
                    if last == b'~' || (last.is_ascii_alphabetic() && seq.len() > 2) {
                        break;
                    }
                }

                if let Some(key) = Input::key_for_seq(&seq) {
                    return InputEvent { key, data: None };
                }
            } else if first >= 0x20 {
                let ch = self.read_utf8_char(first);
                return InputEvent {
                    key: KEY_NONE,
                    data: Some(InputData::Char(ch)),
                };
            } else if let Some(key) = Input::key_for_seq(&[first]) {
                return InputEvent { key, data: None };
            }
        }
    }
}
