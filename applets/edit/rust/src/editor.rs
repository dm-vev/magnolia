/*
 * Magnolia OS - Edit Applet (Editor Core)
 * Copyright (c) 2025 Magnolia Project
 * All rights reserved.
 */
use alloc::string::{String, ToString};
use alloc::vec;
use alloc::vec::Vec;
use core::cmp::{max, min};

use magnolia_applet::dir::Dir;
use magnolia_applet::fs;
use magnolia_applet::sys;

use crate::input::{Input, InputData, InputEvent};
use crate::key::*;
use crate::terminal::Terminal;
use crate::util::{
    abs_isize_to_usize,
    find_subslice,
    push_usize_str,
    trim_ascii_space,
};

const EDIT_VERSION: &str = " V1.0 ";

#[derive(Debug, Clone, Copy)]
pub enum EditAction {
    Quit,
    ForceQuit,
    OpenFile,
    Next,
    Prev,
    NextPlace,
}

struct UndoItem {
    line: usize,
    span: isize,
    text: Vec<Vec<u8>>,
    key: Key,
    col: usize,
    chain: bool,
}

#[derive(Clone, Debug)]
struct Place {
    editor_id: u32,
    line: usize,
}

pub struct Shared {
    yank_buffer: Vec<Vec<u8>>,
    find_pattern: String,
    case_sensitive: bool,
    autoindent: bool,
    replc_pattern: String,
    comment_char: Vec<u8>,
    word_char: Vec<u8>,
    file_char: Vec<u8>,
    match_span: usize,
    place_list: Vec<Place>,
    place_index: usize,
    max_places: usize,
}

impl Shared {
    pub fn new() -> Self {
        Self {
            yank_buffer: Vec::new(),
            find_pattern: String::new(),
            case_sensitive: false,
            autoindent: true,
            replc_pattern: String::new(),
            comment_char: b"# ".to_vec(),
            word_char: b"_\\".to_vec(),
            file_char: b"_.-".to_vec(),
            match_span: 50,
            place_list: Vec::new(),
            place_index: 0,
            max_places: 20,
        }
    }

    pub fn current_place_editor(&self) -> Option<u32> {
        self.place_list.get(self.place_index).map(|p| p.editor_id)
    }

    pub fn file_char(&self) -> &[u8] {
        &self.file_char
    }
}

pub struct Context {
    term: Terminal,
    input: Input,
}

impl Context {
    pub fn new() -> Self {
        let mut input = Input::new();
        let term = Terminal::new(&mut input);
        Self { term, input }
    }

    pub fn reset_screen(&mut self) {
        self.term.scroll_region(0);
        self.term.hilite(0);
        self.term.cursor(true);
        self.term.clear_screen();
    }
}

pub struct Editor {
    id: u32,
    top_line: usize,
    cur_line: usize,
    row: usize,
    vcol: usize,
    col: usize,
    margin: usize,
    tab_size: usize,
    changed: bool,
    hash: u32,
    message: String,
    fname: String,
    content: Vec<Vec<u8>>,
    undo: Vec<UndoItem>,
    undo_limit: usize,
    undo_index: usize,
    redo: Vec<UndoItem>,
    mark: Option<(usize, usize)>,
    mark_flag: i32,
    write_tabs: bool,
    work_dir: String,
    is_dir: bool,
    key_max: usize,
}

impl Editor {
    pub fn new(id: u32, tab_size: usize, undo_limit: usize) -> Self {
        let work_dir = fs::getcwd()
            .ok()
            .and_then(|v| String::from_utf8(v).ok())
            .unwrap_or_else(|| "/".to_string());
        let key_max = Input::key_max();
        Self {
            id,
            top_line: 0,
            cur_line: 0,
            row: 0,
            vcol: 0,
            col: 0,
            margin: 0,
            tab_size,
            changed: false,
            hash: 0,
            message: String::new(),
            fname: String::new(),
            content: vec![Vec::new()],
            undo: Vec::new(),
            undo_limit,
            undo_index: 0,
            redo: Vec::new(),
            mark: None,
            mark_flag: 0,
            write_tabs: false,
            work_dir,
            is_dir: false,
            key_max,
        }
    }

    pub fn id(&self) -> u32 {
        self.id
    }

    fn goto(&self, ctx: &mut Context, row: usize, col: usize) {
        ctx.term.goto(row, col);
    }

    fn clear_to_eol(&self, ctx: &mut Context) {
        ctx.term.clear_to_eol();
    }

    fn cursor(&self, ctx: &mut Context, on: bool) {
        ctx.term.cursor(on);
    }

    fn hilite(&self, ctx: &mut Context, mode: u8) {
        ctx.term.hilite(mode);
    }

    fn scroll_up(&self, ctx: &mut Context, scrolling: usize) {
        ctx.term.scroll_up(scrolling);
    }

    fn scroll_down(&self, ctx: &mut Context, scrolling: usize) {
        ctx.term.scroll_down(scrolling);
    }

    fn redraw(&mut self, ctx: &mut Context, shared: &Shared, show_version: bool) {
        ctx.term.redraw(&mut ctx.input);
        if show_version {
            self.message = EDIT_VERSION.to_string();
        }
        self.changed = self.hash != self.hash_buffer();
        if self.row >= ctx.term.height {
            self.row = ctx.term.height.saturating_sub(1);
        }
        let _ = shared;
    }

    fn status_line(&self, ctx: &mut Context) {
        let mut msg = String::new();
        if self.changed {
            msg.push('*');
        }
        msg.push_str(&self.fname);
        if ctx.term.width > 40 {
            msg.push_str(" Row: ");
            push_usize_str(&mut msg, self.cur_line + 1);
            msg.push('/');
            push_usize_str(&mut msg, self.content.len());
            msg.push_str(" Col: ");
            push_usize_str(&mut msg, self.vcol + 1);
            msg.push_str("  ");
            msg.push_str(&self.message);
        } else {
            msg.push(' ');
            push_usize_str(&mut msg, self.cur_line + 1);
            msg.push(':');
            push_usize_str(&mut msg, self.vcol + 1);
            msg.push_str("  ");
            msg.push_str(&self.message);
        }
        self.goto(ctx, ctx.term.height, 0);
        self.hilite(ctx, 1);
        let bytes = msg.as_bytes();
        let cut = if ctx.term.width > 0 {
            min(bytes.len(), ctx.term.width.saturating_sub(1))
        } else {
            0
        };
        ctx.term.write_bytes(&bytes[..cut]);
        self.clear_to_eol(ctx);
        self.hilite(ctx, 0);
    }

    fn display_window(&mut self, ctx: &mut Context, _shared: &Shared) {
        if self.content.is_empty() {
            self.content.push(Vec::new());
        }
        let total_lines = self.content.len();
        self.cur_line = min(total_lines.saturating_sub(1), self.cur_line);
        let line_len = self.content[self.cur_line].len();
        self.vcol = min(self.col, line_len);

        if self.vcol >= ctx.term.width + self.margin {
            self.margin = self.vcol.saturating_sub(ctx.term.width) + (ctx.term.width >> 2);
        } else if self.vcol < self.margin {
            self.margin = self.vcol.saturating_sub(ctx.term.width >> 2);
        }

        if !(self.top_line <= self.cur_line && self.cur_line < self.top_line + ctx.term.height) {
            self.top_line = self.cur_line.saturating_sub(self.row);
        }

        self.row = self.cur_line.saturating_sub(self.top_line);

        self.cursor(ctx, false);

        let mut line_index = self.top_line;
        let (mut start_line, mut start_col, mut end_line, mut end_col) = (0, 0, 0, 0);
        let mut has_mark = false;
        if self.mark.is_some() {
            let (sl, sc, el, ec) = self.mark_range();
            start_line = sl;
            start_col = sc;
            end_line = el;
            end_col = ec;
            has_mark = true;
        }

        for row in 0..ctx.term.height {
            if line_index >= total_lines {
                if row < ctx.term.scrbuf.len() && ctx.term.scrbuf[row].flag != 0xfe {
                    self.goto(ctx, row, 0);
                    self.clear_to_eol(ctx);
                    if row < ctx.term.scrbuf.len() {
                        ctx.term.scrbuf[row].flag = 0xfe;
                        ctx.term.scrbuf[row].text.clear();
                    }
                }
            } else {
                let mut flag = 0u8;
                if has_mark {
                    flag = (start_line <= line_index && line_index < end_line) as u8;
                    flag |= ((start_line == line_index) as u8) << 1;
                    flag |= (((end_line.saturating_sub(1)) == line_index) as u8) << 2;
                }
                let line = &self.content[line_index];
                let start = min(self.margin, line.len());
                let end = min(start + ctx.term.width, line.len());
                let slice = line[start..end].to_vec();
                let changed = row >= ctx.term.scrbuf.len()
                    || ctx.term.scrbuf[row].flag != flag
                    || ctx.term.scrbuf[row].text != slice
                    || (flag != 0 && line_index == self.cur_line);

                if changed {
                    self.goto(ctx, row, 0);
                    if !has_mark || flag == 0 {
                        ctx.term.write_bytes(&slice);
                    } else if flag == 7 {
                        let rel_start = start_col.saturating_sub(self.margin);
                        let rel_end = end_col.saturating_sub(self.margin);
                        let rel_start = min(rel_start, slice.len());
                        let rel_end = min(rel_end, slice.len());
                        ctx.term.write_bytes(&slice[..rel_start]);
                        self.hilite(ctx, 2);
                        ctx.term.write_bytes(&slice[rel_start..rel_end]);
                        self.hilite(ctx, 0);
                        ctx.term.write_bytes(&slice[rel_end..]);
                    } else if flag == 3 {
                        let rel_start = start_col.saturating_sub(self.margin);
                        let rel_start = min(rel_start, slice.len());
                        ctx.term.write_bytes(&slice[..rel_start]);
                        self.hilite(ctx, 2);
                        ctx.term.write_bytes(&slice[rel_start..]);
                        ctx.term.write_bytes(b" ");
                        self.hilite(ctx, 0);
                    } else if flag == 5 {
                        let rel_end = end_col.saturating_sub(self.margin);
                        let rel_end = min(rel_end, slice.len());
                        self.hilite(ctx, 2);
                        ctx.term.write_bytes(&slice[..rel_end]);
                        self.hilite(ctx, 0);
                        ctx.term.write_bytes(&slice[rel_end..]);
                    } else {
                        self.hilite(ctx, 2);
                        ctx.term.write_bytes(&slice);
                        ctx.term.write_bytes(b" ");
                        self.hilite(ctx, 0);
                    }
                    if slice.len() < ctx.term.width {
                        self.clear_to_eol(ctx);
                    }
                    if row < ctx.term.scrbuf.len() {
                        ctx.term.scrbuf[row].flag = flag;
                        ctx.term.scrbuf[row].text = slice;
                    }
                }
                line_index += 1;
            }
        }
        self.status_line(ctx);
        self.goto(ctx, self.row, self.vcol.saturating_sub(self.margin));
        self.cursor(ctx, true);
    }

    fn spaces(&self, line: &[u8], pos: Option<usize>) -> usize {
        match pos {
            None => line.iter().take_while(|&&c| c == b' ').count(),
            Some(pos) => {
                let pos = min(pos, line.len());
                let mut i = pos;
                while i > 0 && line[i - 1] == b' ' {
                    i -= 1;
                }
                pos - i
            }
        }
    }

    fn mark_range(&self) -> (usize, usize, usize, usize) {
        let mark = self.mark.unwrap_or((self.cur_line, self.col));
        if self.mark_order(self.cur_line, self.col) >= 0 {
            (mark.0, mark.1, self.cur_line + 1, self.col)
        } else {
            (self.cur_line, self.col, mark.0 + 1, mark.1)
        }
    }

    fn mark_order(&self, line: usize, col: usize) -> isize {
        let mark = self.mark.unwrap_or((line, col));
        if mark.0 == line {
            col as isize - mark.1 as isize
        } else {
            line as isize - mark.0 as isize
        }
    }

    fn line_range(&self) -> (usize, usize) {
        let (sline, _scol, eline, ecol) = self.mark_range();
        if ecol > 0 {
            (sline, eline)
        } else {
            (sline, eline.saturating_sub(1))
        }
    }

    fn line_edit(
        &mut self,
        ctx: &mut Context,
        prompt: &str,
        default: &str,
        zap: Option<&[u8]>,
    ) -> Option<String> {
        self.goto(ctx, ctx.term.height, 0);
        self.hilite(ctx, 1);
        ctx.term.write_str(prompt);
        ctx.term.write_str(default);
        self.clear_to_eol(ctx);

        let mut res = default.as_bytes().to_vec();
        let mut pos = res.len();
        let mut del_all = true;

        loop {
            let evt = ctx.input.get_input(self.key_max);
            match evt.key {
                KEY_NONE => {
                    if let Some(InputData::Char(ch)) = evt.data {
                        if del_all {
                            ctx.term.write_str("\x08".repeat(pos).as_str());
                            ctx.term.write_str(&" ".repeat(res.len()));
                            ctx.term.write_str("\x08".repeat(res.len()).as_str());
                            res.clear();
                            pos = 0;
                            del_all = false;
                        }
                        if prompt.len() + res.len() < ctx.term.width.saturating_sub(2) {
                            res.splice(pos..pos, ch.clone());
                            ctx.term.write_bytes(&ch);
                            pos += ch.len();
                            let tail = &res[pos..];
                            ctx.term.write_bytes(tail);
                            ctx.term.write_str("\x08".repeat(tail.len()).as_str());
                        }
                    }
                }
                KEY_ENTER | KEY_TAB => {
                    self.hilite(ctx, 0);
                    return String::from_utf8(res).ok();
                }
                KEY_QUIT | KEY_COPY => {
                    self.hilite(ctx, 0);
                    return None;
                }
                KEY_LEFT => {
                    if pos > 0 {
                        ctx.term.write_str("\x08");
                        pos -= 1;
                    }
                }
                KEY_RIGHT => {
                    if pos < res.len() {
                        ctx.term.write_bytes(&[res[pos]]);
                        pos += 1;
                    }
                }
                KEY_HOME => {
                    if pos > 0 {
                        ctx.term.write_str("\x08".repeat(pos).as_str());
                        pos = 0;
                    }
                }
                KEY_END => {
                    if pos < res.len() {
                        ctx.term.write_bytes(&res[pos..]);
                        pos = res.len();
                    }
                }
                KEY_DELETE => {
                    if del_all {
                        ctx.term.write_str("\x08".repeat(pos).as_str());
                        ctx.term.write_str(&" ".repeat(res.len()));
                        ctx.term.write_str("\x08".repeat(res.len()).as_str());
                        res.clear();
                        pos = 0;
                    } else if pos < res.len() {
                        res.remove(pos);
                        let tail = &res[pos..];
                        ctx.term.write_bytes(tail);
                        ctx.term.write_bytes(b" ");
                        ctx.term.write_str("\x08".repeat(tail.len() + 1).as_str());
                    }
                }
                KEY_BACKSPACE => {
                    if del_all {
                        ctx.term.write_str("\x08".repeat(pos).as_str());
                        ctx.term.write_str(&" ".repeat(res.len()));
                        ctx.term.write_str("\x08".repeat(res.len()).as_str());
                        res.clear();
                        pos = 0;
                        del_all = false;
                        continue;
                    }
                    if pos > 0 {
                        res.remove(pos - 1);
                        ctx.term.write_str("\x08");
                        pos -= 1;
                        let tail = &res[pos..];
                        ctx.term.write_bytes(tail);
                        ctx.term.write_bytes(b" ");
                        ctx.term.write_str("\x08".repeat(tail.len() + 1).as_str());
                    }
                }
                KEY_PASTE => {
                    if let Some(zap) = zap {
                        let s = self.getsymbol(&self.content[self.cur_line], self.col, zap);
                        let max_len = ctx.term.width.saturating_sub(pos + prompt.len() + 1);
                        let s = if s.len() > max_len {
                            &s[..max_len]
                        } else {
                            &s
                        };
                        res.extend_from_slice(s);
                        ctx.term.write_bytes(&res[pos..]);
                        pos = res.len();
                    }
                }
                _ => {}
            }
            del_all = false;
        }
    }

    fn getsymbol(&self, s: &[u8], pos: usize, zap: &[u8]) -> Vec<u8> {
        if pos < s.len() {
            let start = self.skip_while(s, pos, zap, -1);
            let stop = self.skip_while(s, pos, zap, 1);
            s[start + 1..stop].to_vec()
        } else {
            Vec::new()
        }
    }

    fn issymbol(&self, c: u8, zap: &[u8]) -> bool {
        c.is_ascii_alphanumeric() || zap.contains(&c)
    }

    fn skip_until(&self, s: &[u8], mut pos: isize, zap: &[u8], way: isize) -> isize {
        let stop = if way < 0 { -1 } else { s.len() as isize };
        while pos != stop {
            let idx = pos as usize;
            if idx < s.len() && self.issymbol(s[idx], zap) {
                break;
            }
            pos += way;
        }
        pos
    }

    fn skip_while(&self, s: &[u8], mut pos: usize, zap: &[u8], way: isize) -> usize {
        let stop = if way < 0 { 0usize } else { s.len() };
        if way < 0 {
            while pos > stop && self.issymbol(s[pos - 1], zap) {
                pos = pos.saturating_sub(1);
            }
            pos
        } else {
            while pos < stop && self.issymbol(s[pos], zap) {
                pos += 1;
            }
            pos
        }
    }

    fn move_up(&mut self, ctx: &mut Context) {
        if self.cur_line > 0 {
            self.cur_line -= 1;
            if self.cur_line < self.top_line {
                self.scroll_up(ctx, 1);
            }
        }
    }

    fn skip_up(&mut self) -> bool {
        if self.col == 0 && self.cur_line > 0 {
            self.col = self.content[self.cur_line - 1].len();
            self.cur_line -= 1;
            true
        } else {
            false
        }
    }

    fn move_left(&mut self) {
        self.col = self.vcol;
        if !self.skip_up() {
            self.col = self.col.saturating_sub(1);
        }
    }

    fn move_down(&mut self, ctx: &mut Context) {
        if self.cur_line + 1 < self.content.len() {
            self.cur_line += 1;
            if self.cur_line >= self.top_line + ctx.term.height {
                self.scroll_down(ctx, 1);
            }
        }
    }

    fn skip_down(&mut self, line_len: usize) -> bool {
        if self.col >= line_len && self.cur_line + 1 < self.content.len() {
            self.col = 0;
            self.cur_line += 1;
            true
        } else {
            false
        }
    }

    fn move_right(&mut self, line_len: usize) {
        self.col = self.vcol;
        if !self.skip_down(line_len) {
            self.col = min(self.col + 1, line_len);
        }
    }

    fn find_in_file(&mut self, pattern: &str, col: usize, end: usize, shared: &mut Shared) -> Option<usize> {
        shared.find_pattern = pattern.to_string();
        let pat = pattern.as_bytes();
        if pat.is_empty() {
            return None;
        }
        let mut start_line = self.cur_line;
        let mut col = col;
        if col > self.content[start_line].len() {
            start_line += 1;
            col = 0;
        }
        for line_idx in start_line..min(end, self.content.len()) {
            let line = &self.content[line_idx];
            let slice = if line_idx == start_line { &line[col..] } else { &line[..] };
            let mut hay = slice.to_vec();
            let mut needle = pat.to_vec();
            if !shared.case_sensitive {
                for b in &mut hay {
                    *b = b.to_ascii_lowercase();
                }
                for b in &mut needle {
                    *b = b.to_ascii_lowercase();
                }
            }
            if let Some(pos) = find_subslice(&hay, &needle) {
                self.cur_line = line_idx;
                self.col = col + pos;
                return Some(needle.len());
            }
            col = 0;
        }
                    self.message.clear();
                    self.message.push_str(&shared.find_pattern);
                    self.message.push_str(" not found (again)");
        None
    }

    fn undo_add(&mut self, lnum: usize, text: Vec<Vec<u8>>, key: Key, span: isize, chain: bool) {
        let need_new = self.undo.is_empty()
            || key == KEY_NONE
            || self.undo.last().map(|u| u.key != key || u.line != lnum).unwrap_or(true);
        if need_new {
            self.changed = true;
            if self.undo.len() >= self.undo_limit {
                self.undo.remove(0);
            }
            self.undo.push(UndoItem {
                line: lnum,
                span,
                text,
                key,
                col: self.col,
                chain,
            });
            self.redo.clear();
            self.undo_index = self.undo.len().saturating_sub(1);
        }
    }

    fn undo_redo(&mut self, undo_is_undo: bool) {
        let mut undo = if undo_is_undo {
            core::mem::take(&mut self.undo)
        } else {
            core::mem::take(&mut self.redo)
        };
        let mut redo = if undo_is_undo {
            core::mem::take(&mut self.redo)
        } else {
            core::mem::take(&mut self.undo)
        };

        let mut chain = true;
        let redo_start = redo.len();
        while !undo.is_empty() && chain {
            let action = undo.pop().unwrap();
            if action.key != KEY_INDENT && action.key != KEY_DEDENT && action.key != KEY_COMMENT {
                self.cur_line = action.line;
            }
            self.col = action.col;
            if redo.len() >= self.undo_limit {
                redo.remove(0);
            }
            if action.span >= 0 {
                let start = action.line;
                let end = min(self.content.len(), action.line + action.span as usize);
                let saved = self.content[start..end].to_vec();
                redo.push(UndoItem {
                    line: action.line,
                    span: action.text.len() as isize,
                    text: saved,
                    key: action.key,
                    col: action.col,
                    chain: action.chain,
                });
                if action.line < self.content.len() {
                    self.content.splice(action.line..end, action.text.clone());
                } else {
                    self.content.extend(action.text.clone());
                }
            } else {
                let start = action.line;
                let end = min(self.content.len(), action.line + (-action.span) as usize);
                let saved = self.content[start..=start].to_vec();
                redo.push(UndoItem {
                    line: action.line,
                    span: 1,
                    text: saved,
                    key: action.key,
                    col: action.col,
                    chain: action.chain,
                });
                if end > start {
                    self.content.drain(start..end);
                }
                if start < self.content.len() {
                    self.content[start] = action.text[0].clone();
                }
            }
            chain = action.chain;
        }
        if redo.len() > redo_start {
            if let Some(last) = redo.last_mut() {
                last.chain = true;
            }
            if let Some(first) = redo.get_mut(redo_start) {
                first.chain = false;
            }
            self.changed = self.hash != self.hash_buffer();
            self.clear_mark();
        }

        if undo_is_undo {
            self.undo = undo;
            self.redo = redo;
        } else {
            self.redo = undo;
            self.undo = redo;
        }
    }

    fn set_mark(&mut self, flag: i32) {
        if self.mark.is_none() {
            self.mark = Some((self.cur_line, self.col));
        }
        if self.mark_flag < flag {
            self.mark_flag = flag;
        }
    }

    fn check_mark(&mut self) {
        if self.mark.is_some() {
            self.mark_flag -= 1;
            if self.mark_flag <= 0 {
                self.clear_mark();
            }
        }
    }

    fn clear_mark(&mut self) {
        self.mark = None;
        self.mark_flag = 0;
    }

    fn yank_mark(&mut self, shared: &mut Shared) {
        let (start_row, start_col, end_row, end_col) = self.mark_range();
        shared.yank_buffer = self.content[start_row..end_row].to_vec();
        if let Some(last) = shared.yank_buffer.last_mut() {
            last.truncate(end_col);
        }
        if let Some(first) = shared.yank_buffer.first_mut() {
            if start_col < first.len() {
                first.drain(0..start_col);
            } else {
                first.clear();
            }
        }
    }

    fn delete_mark(&mut self, shared: &mut Shared, yank: bool) {
        if yank {
            self.yank_mark(shared);
        }
        let (start_row, start_col, end_row, end_col) = self.mark_range();
        let slice = self.content[start_row..end_row].to_vec();
        self.undo_add(start_row, slice, KEY_NONE, 1, false);
        let tail = if end_row > 0 && end_row - 1 < self.content.len() {
            let line = &self.content[end_row - 1];
            if end_col < line.len() {
                line[end_col..].to_vec()
            } else {
                Vec::new()
            }
        } else {
            Vec::new()
        };
        if start_row < self.content.len() {
            let mut new_line = self.content[start_row][..start_col].to_vec();
            new_line.extend_from_slice(&tail);
            self.content[start_row] = new_line;
        }
        if start_row + 1 < end_row {
            self.content.drain(start_row + 1..end_row);
        }
        if self.content.is_empty() {
            self.content.push(Vec::new());
        }
        self.col = start_col;
        self.cur_line = start_row;
        self.clear_mark();
    }

    fn handle_edit_keys(&mut self, ctx: &mut Context, shared: &mut Shared, evt: InputEvent) -> Option<Key> {
        let mut key = evt.key;
        let mut char_data = evt.data.clone();
        let line = self.content[self.cur_line].clone();

        if key == KEY_NONE {
            self.col = self.vcol;
            let ch = if let Some(InputData::Char(ch)) = char_data.take() {
                ch
            } else {
                Vec::new()
            };
            if self.mark.is_some() {
                self.delete_mark(shared, false);
            }
            let chain = false;
            self.undo_add(self.cur_line, vec![line.clone()], 0x20, 1, chain);
            let mut new_line = line;
            new_line.splice(self.col..self.col, ch.clone());
            self.content[self.cur_line] = new_line;
            self.col += ch.len();
            return Some(key);
        }

        match key {
            KEY_SHIFT_CTRL_LEFT => {
                self.set_mark(999999999);
                key = KEY_WORD_LEFT;
            }
            KEY_SHIFT_CTRL_RIGHT => {
                self.set_mark(999999999);
                key = KEY_WORD_RIGHT;
            }
            KEY_WORD_LEFT => {
                self.col = self.vcol;
                if self.col > 0 {
                    let pos = self.skip_until(
                        &line,
                        self.col.saturating_sub(1) as isize,
                        &shared.word_char,
                        -1,
                    );
                    let pos = if pos < 0 { 0 } else { pos as usize };
                    let pos = self.skip_while(&line, pos, &shared.word_char, -1);
                    self.col = min(self.col, pos + 1);
                }
            }
            KEY_WORD_RIGHT => {
                self.col = self.vcol;
                if self.col < line.len() {
                    let pos = self.skip_until(&line, self.col as isize, &shared.word_char, 1);
                    let pos = if pos < 0 { 0 } else { pos as usize };
                    let pos = self.skip_while(&line, pos, &shared.word_char, 1);
                    self.col = min(pos, line.len());
                }
            }
            KEY_LEFT => self.move_left(),
            KEY_RIGHT => self.move_right(line.len()),
            KEY_UP => self.move_up(ctx),
            KEY_DOWN => self.move_down(ctx),
            KEY_BACKSPACE => {
                if self.mark.is_some() {
                    self.delete_mark(shared, false);
                } else if self.col == 0 {
                    if self.cur_line > 0 {
                        let prev = self.content[self.cur_line - 1].clone();
                        let current = self.content[self.cur_line].clone();
                        self.undo_add(self.cur_line - 1, vec![prev, current.clone()], KEY_NONE, 1, false);
                        self.content[self.cur_line - 1].extend_from_slice(&current);
                        self.content.remove(self.cur_line);
                        self.cur_line -= 1;
                        self.col = self.content[self.cur_line].len();
                    }
                } else {
                    self.undo_add(self.cur_line, vec![line.clone()], KEY_BACKSPACE, 1, false);
                    let mut new_line = line;
                    new_line.remove(self.col - 1);
                    self.content[self.cur_line] = new_line;
                    self.col -= 1;
                }
            }
            KEY_DELETE => {
                if self.mark.is_some() {
                    self.delete_mark(shared, false);
                } else if self.col >= line.len() {
                    if self.cur_line + 1 < self.content.len() {
                        let mut next = self.content.remove(self.cur_line + 1);
                        self.undo_add(self.cur_line, vec![line.clone(), next.clone()], KEY_NONE, 1, false);
                        if shared.autoindent && self.col > 0 {
                            let ns = self.spaces(&next, None);
                            if ns > 0 {
                                next.drain(0..ns);
                            }
                        }
                        self.content[self.cur_line].extend_from_slice(&next);
                    }
                } else {
                    self.undo_add(self.cur_line, vec![line.clone()], KEY_DELETE, 1, false);
                    let mut new_line = line;
                    new_line.remove(self.col);
                    self.content[self.cur_line] = new_line;
                }
            }
            KEY_DEL_WORD => {
                if self.col < line.len() {
                    let mut pos = self.skip_while(&line, self.col, &shared.word_char, 1);
                    pos += self.spaces(&line[pos..], None);
                    if self.col < pos {
                        self.undo_add(self.cur_line, vec![line.clone()], KEY_DEL_WORD, 1, false);
                        let mut new_line = line;
                        new_line.drain(self.col..pos);
                        self.content[self.cur_line] = new_line;
                    }
                }
            }
            KEY_DEL_LINE => {
                if self.cur_line + 1 < self.content.len() {
                    let next = self.content[self.cur_line + 1].clone();
                    self.undo_add(self.cur_line, vec![line.clone(), next], KEY_NONE, 1, false);
                } else {
                    self.undo_add(self.cur_line, vec![line.clone()], KEY_NONE, 1, false);
                }
                self.content.remove(self.cur_line);
                if self.content.is_empty() {
                    self.content.push(Vec::new());
                }
            }
            KEY_HOME => {
                let indent = self.spaces(&line, None);
                self.col = if self.col == 0 { indent } else { 0 };
            }
            KEY_END => {
                let comment = trim_ascii_space(&shared.comment_char).to_vec();
                let mut split = line.clone();
                if !comment.is_empty() {
                    if let Some(pos) = find_subslice(&line, &comment) {
                        split = line[..pos].to_vec();
                    }
                }
                let ni = split.iter().rposition(|&b| b != b' ').map(|p| p + 1).unwrap_or(0);
                let ns = self.spaces(&line, None);
                self.col = if self.col >= line.len() && ni > ns { ni } else { line.len() };
            }
            KEY_PGUP => self.cur_line = self.cur_line.saturating_sub(ctx.term.height),
            KEY_PGDN => self.cur_line = min(self.cur_line + ctx.term.height, self.content.len().saturating_sub(1)),
            KEY_FIND => {
                let find_default = shared.find_pattern.clone();
                let word_char = shared.word_char.clone();
                if let Some(pat) = self.line_edit(ctx, "Find: ", &find_default, Some(&word_char)) {
                    self.clear_mark();
                    let _ = self.find_in_file(&pat, self.col + 1, self.content.len(), shared);
                    self.row = ctx.term.height >> 1;
                }
            }
            KEY_FIND_AGAIN => {
                if !shared.find_pattern.is_empty() {
                    let pat = shared.find_pattern.clone();
                    let _ = self.find_in_file(&pat, self.col + 1, self.content.len(), shared);
                    self.row = ctx.term.height >> 1;
                }
            }
            KEY_GOTO => {
                if let Some(line_str) = self.line_edit(ctx, "Goto Line: ", "", None) {
                    if let Ok(line_num) = line_str.trim().parse::<usize>() {
                        self.cur_line = line_num.saturating_sub(1);
                        self.row = ctx.term.height >> 1;
                    }
                }
            }
            KEY_FIRST => {
                self.check_mark();
                self.cur_line = 0;
            }
            KEY_LAST => {
                self.check_mark();
                self.cur_line = self.content.len().saturating_sub(1);
                self.row = ctx.term.height.saturating_sub(1);
            }
            KEY_TOGGLE => {
                let comment_str = core::str::from_utf8(&shared.comment_char).unwrap_or("?");
                let mut prompt = String::new();
                prompt.push_str("Autoindent ");
                prompt.push(if shared.autoindent { 'y' } else { 'n' });
                prompt.push_str(", Search Case ");
                prompt.push(if shared.case_sensitive { 'y' } else { 'n' });
                prompt.push_str(", Tabsize ");
                push_usize_str(&mut prompt, self.tab_size);
                prompt.push_str(", Comment ");
                prompt.push_str(comment_str);
                prompt.push_str(", Tabwrite ");
                prompt.push(if self.write_tabs { 'y' } else { 'n' });
                prompt.push_str(": ");
                if let Some(pat) = self.line_edit(ctx, &prompt, "", None) {
                    let parts: Vec<&str> = pat.split(',').map(|s| s.trim()).collect();
                    if let Some(p) = parts.get(0) {
                        if !p.is_empty() {
                            shared.autoindent = p.as_bytes()[0].to_ascii_lowercase() == b'y';
                        }
                    }
                    if let Some(p) = parts.get(1) {
                        if !p.is_empty() {
                            shared.case_sensitive = p.as_bytes()[0].to_ascii_lowercase() == b'y';
                        }
                    }
                    if let Some(p) = parts.get(2) {
                        if let Ok(ts) = p.parse::<usize>() {
                            self.tab_size = ts.max(1);
                        }
                    }
                    if let Some(p) = parts.get(3) {
                        if !p.is_empty() {
                            shared.comment_char = p.as_bytes().to_vec();
                        }
                    }
                    if let Some(p) = parts.get(4) {
                        if !p.is_empty() {
                            self.write_tabs = p.as_bytes()[0].to_ascii_lowercase() == b'y';
                        }
                    }
                }
            }
            KEY_SCRLUP => {
                let ni = if let Some(InputData::Char(ch)) = char_data { if !ch.is_empty() { ch[0] as usize } else { 1 } } else { 1 };
                if self.top_line > 0 {
                    self.top_line = self.top_line.saturating_sub(ni);
                    self.cur_line = min(self.cur_line, self.top_line + ctx.term.height - 1);
                    self.scroll_up(ctx, ni);
                }
            }
            KEY_SCRLDN => {
                let ni = if let Some(InputData::Char(ch)) = char_data { if !ch.is_empty() { ch[0] as usize } else { 1 } } else { 1 };
                if self.top_line + ctx.term.height < self.content.len() {
                    self.top_line = min(self.top_line + ni, self.content.len().saturating_sub(1));
                    self.cur_line = max(self.cur_line, self.top_line);
                    self.scroll_down(ctx, ni);
                }
            }
            KEY_MATCH => {
                if self.col < line.len() {
                    let brackets = b"<{[()]}>";
                    let srch = line[self.col];
                    if let Some(idx) = brackets.iter().position(|&b| b == srch) {
                        let match_ch = brackets[7 - idx];
                        let mut level = 0isize;
                        let way: isize = if idx < 4 { 1 } else { -1 };
                        let mut i = self.cur_line as isize;
                        let mut c = self.col as isize + way;
                        let lstop = if way > 0 {
                            min(self.content.len(), self.cur_line + shared.match_span) as isize
                        } else {
                            max(-1, self.cur_line as isize - shared.match_span as isize)
                        };
                        while i != lstop {
                            let l = &self.content[i as usize];
                            let cstop = if way > 0 { l.len() as isize } else { -1 };
                            if l.contains(&srch) || l.contains(&match_ch) {
                                while c != cstop {
                                    let idx = c as usize;
                                    if idx < l.len() {
                                        if l[idx] == match_ch {
                                            if level == 0 {
                                                self.cur_line = i as usize;
                                                self.col = c as usize;
                                                return Some(key);
                                            }
                                            level -= 1;
                                        } else if l[idx] == srch {
                                            level += 1;
                                        }
                                    }
                                    c += way;
                                }
                            }
                            i += way;
                            if i >= 0 && (i as usize) < self.content.len() {
                                c = if way > 0 { 0 } else { self.content[i as usize].len() as isize - 1 };
                            }
                        }
                        let delta = abs_isize_to_usize(lstop - self.cur_line as isize);
                        self.message.clear();
                        self.message.push_str("No match in ");
                        push_usize_str(&mut self.message, delta);
                        self.message.push_str(" lines");
                    }
                }
            }
            KEY_MARK => {
                if self.mark.is_none() {
                    self.set_mark(999999999);
                    self.move_right(line.len());
                } else {
                    self.clear_mark();
                }
            }
            KEY_SHIFT_DOWN => {
                self.set_mark(999999999);
                self.move_down(ctx);
            }
            KEY_SHIFT_UP => {
                self.set_mark(999999999);
                self.move_up(ctx);
            }
            KEY_SHIFT_LEFT => {
                self.set_mark(999999999);
                self.move_left();
            }
            KEY_SHIFT_RIGHT => {
                self.set_mark(999999999);
                self.move_right(line.len());
            }
            KEY_ALT_LEFT => {
                if self.col > 0 && self.col < line.len() {
                    self.undo_add(self.cur_line, vec![line.clone()], KEY_ALT_LEFT, 1, false);
                    let i = self.col;
                    let mut new_line = line.clone();
                    new_line.swap(i - 1, i);
                    self.content[self.cur_line] = new_line;
                }
            }
            KEY_ALT_RIGHT => {
                if self.col + 1 < line.len() {
                    self.undo_add(self.cur_line, vec![line.clone()], KEY_ALT_RIGHT, 1, false);
                    let i = self.col;
                    let mut new_line = line.clone();
                    new_line.swap(i, i + 1);
                    self.content[self.cur_line] = new_line;
                    self.col += 1;
                }
            }
            KEY_ALT_UP => {
                let (start_line, end_line) = if self.mark.is_none() {
                    (self.cur_line, self.cur_line + 1)
                } else {
                    self.line_range()
                };
                if start_line > 0 {
                    self.undo_add(
                        start_line - 1,
                        self.content[start_line - 1..end_line].to_vec(),
                        KEY_NONE,
                        (end_line - start_line + 1) as isize,
                        false,
                    );
                    let prev = self.content[start_line - 1].clone();
                    for i in start_line..end_line {
                        self.content[i - 1] = self.content[i].clone();
                    }
                    self.content[end_line - 1] = prev;
                    self.move_up(ctx);
                }
            }
            KEY_ALT_DOWN => {
                let (start_line, end_line) = if self.mark.is_none() {
                    (self.cur_line, self.cur_line + 1)
                } else {
                    self.line_range()
                };
                if end_line < self.content.len() {
                    self.undo_add(
                        start_line,
                        self.content[start_line..end_line + 1].to_vec(),
                        KEY_NONE,
                        (end_line - start_line + 1) as isize,
                        false,
                    );
                    let next = self.content[end_line].clone();
                    let mut i = end_line;
                    while i > start_line {
                        self.content[i] = self.content[i - 1].clone();
                        i -= 1;
                    }
                    self.content[start_line] = next;
                    self.move_down(ctx);
                }
            }
            KEY_ENTER => {
                self.col = self.vcol;
                self.clear_mark();
                self.undo_add(self.cur_line, vec![line.clone()], KEY_NONE, 2, false);
                let mut left = line[..self.col].to_vec();
                let mut right = line[self.col..].to_vec();
                let mut ni = 0;
                if shared.autoindent {
                    ni = min(self.spaces(&line, None), self.col);
                }
                let mut new_line = vec![b' '; ni];
                new_line.append(&mut right);
                self.content[self.cur_line] = left;
                self.cur_line += 1;
                self.content.insert(self.cur_line, new_line);
                self.col = ni;
            }
            KEY_TAB => {
                if self.mark.is_none() {
                    self.col = self.vcol;
                    self.undo_add(self.cur_line, vec![line.clone()], KEY_TAB, 1, false);
                    let ni = self.tab_size - (self.col % self.tab_size);
                    let mut new_line = line;
                    new_line.splice(self.col..self.col, vec![b' '; ni]);
                    self.content[self.cur_line] = new_line;
                    self.col += ni;
                } else {
                    let (start, end) = self.line_range();
                    self.undo_add(start, self.content[start..end].to_vec(), KEY_INDENT, (end - start) as isize, false);
                    for i in start..end {
                        if !self.content[i].is_empty() {
                            let ns = self.spaces(&self.content[i], None);
                            let add = self.tab_size - (ns % self.tab_size);
                            let mut new_line = vec![b' '; add];
                            new_line.extend_from_slice(&self.content[i]);
                            self.content[i] = new_line;
                        }
                    }
                }
            }
            KEY_BACKTAB => {
                if self.mark.is_none() {
                    self.col = self.vcol;
                    let ni = min((self.col.saturating_sub(1)) % self.tab_size + 1, self.spaces(&line, Some(self.col)));
                    if ni > 0 {
                        self.undo_add(self.cur_line, vec![line.clone()], KEY_BACKTAB, 1, false);
                        let mut new_line = line;
                        new_line.drain(self.col - ni..self.col);
                        self.content[self.cur_line] = new_line;
                        self.col -= ni;
                    }
                } else {
                    let (start, end) = self.line_range();
                    self.undo_add(start, self.content[start..end].to_vec(), KEY_DEDENT, (end - start) as isize, false);
                    for i in start..end {
                        let ns = self.spaces(&self.content[i], None);
                        if ns > 0 {
                            let drop = (ns - 1) % self.tab_size + 1;
                            self.content[i].drain(0..drop);
                        }
                    }
                }
            }
            KEY_REPLC => {
                let find_default = shared.find_pattern.clone();
                let pat = self.line_edit(ctx, "Replace: ", &find_default, Some(b"_"));
                if let Some(pat) = pat {
                    let replc_default = if shared.replc_pattern.is_empty() {
                        pat.clone()
                    } else {
                        shared.replc_pattern.clone()
                    };
                    let rpat = self.line_edit(ctx, "With: ", &replc_default, None);
                    if let Some(rpat) = rpat {
                        shared.replc_pattern = rpat.clone();
                        let mut q = b'n';
                        let (mut end_line, mut end_col) = (self.content.len(), usize::MAX);
                        let (cur_line, cur_col) = (self.cur_line, self.col);
                        if self.mark.is_some() {
                            let (sline, scol, eline, ecol) = self.mark_range();
                            self.cur_line = sline;
                            self.col = scol;
                            end_line = eline;
                            end_col = ecol;
                        }
                        self.message = "Replace (yes/No/all/quit) ? ".to_string();
                        let mut chain = false;
                        let mut count = 0;
                        loop {
                            let ni = self.find_in_file(&pat, self.col, end_line, shared);
                            if let Some(ni) = ni {
                                if self.cur_line != end_line.saturating_sub(1) || self.col < end_col {
                                    if q != b'a' {
                                        self.display_window(ctx, shared);
                                        let evt = ctx.input.get_input(self.key_max);
                                        if let Some(InputData::Char(ch)) = evt.data {
                                            if let Some(&b) = ch.first() {
                                                q = b.to_ascii_lowercase();
                                            }
                                        }
                                    }
                                    if q == b'q' {
                                        break;
                                    } else if q == b'a' || q == b'y' {
                                        self.undo_add(self.cur_line, vec![self.content[self.cur_line].clone()], KEY_NONE, 1, chain);
                                        let mut new_line = self.content[self.cur_line].clone();
                                        new_line.splice(self.col..self.col + ni, rpat.as_bytes().iter().cloned());
                                        self.content[self.cur_line] = new_line;
                                        self.col += rpat.len();
                                        count += 1;
                                        chain = true;
                                    } else {
                                        self.col += 1;
                                    }
                                    continue;
                                }
                            }
                            break;
                        }
                        self.cur_line = cur_line;
                        self.col = cur_col;
                        self.message.clear();
                        self.message.push('\'');
                        self.message.push_str(&pat);
                        self.message.push_str("' replaced ");
                        push_usize_str(&mut self.message, count);
                        self.message.push_str(" times");
                    }
                }
            }
            KEY_CUT => {
                if self.mark.is_none() {
                    if self.cur_line + 1 < self.content.len() {
                        self.mark = Some((self.cur_line + 1, 0));
                    } else {
                        self.mark = Some((self.cur_line, line.len()));
                    }
                    self.col = 0;
                }
                self.delete_mark(shared, true);
            }
            KEY_COPY => {
                let col = self.col;
                if self.mark.is_none() {
                    if self.cur_line + 1 < self.content.len() {
                        self.mark = Some((self.cur_line + 1, 0));
                    } else {
                        self.mark = Some((self.cur_line, line.len()));
                    }
                    self.col = 0;
                }
                self.yank_mark(shared);
                self.clear_mark();
                self.col = col;
            }
            KEY_PASTE => {
                if !shared.yank_buffer.is_empty() {
                    self.col = self.vcol;
                    if self.mark.is_some() {
                        self.delete_mark(shared, false);
                    }
                    let mut head = shared.yank_buffer[0].clone();
                    let mut tail = shared.yank_buffer.last().cloned().unwrap_or_default();
                    head.splice(0..0, self.content[self.cur_line][..self.col].iter().cloned());
                    tail.extend_from_slice(&self.content[self.cur_line][self.col..]);
                    let mut insert = shared.yank_buffer.clone();
                    insert[0] = head;
                    let last = insert.len().saturating_sub(1);
                    insert[last] = tail;
                    self.undo_add(self.cur_line, vec![self.content[self.cur_line].clone()], KEY_NONE, (1isize - insert.len() as isize), true);
                    self.content.splice(self.cur_line..self.cur_line + 1, insert);
                }
            }
            KEY_WRITE => {
                self.save_current(ctx, shared);
            }
            KEY_UNDO => {
                self.undo_redo(true);
            }
            KEY_REDO => {
                self.undo_redo(false);
            }
            KEY_COMMENT => {
                let (start, end) = if self.mark.is_none() {
                    (self.cur_line, self.cur_line + 1)
                } else {
                    self.line_range()
                };
                self.undo_add(start, self.content[start..end].to_vec(), KEY_COMMENT, (end - start) as isize, false);
                let ni = shared.comment_char.len();
                for i in start..end {
                    if !self.content[i].iter().all(|&b| b == b' ') {
                        let ns = self.spaces(&self.content[i], None);
                        if self.content[i].get(ns..ns + ni) == Some(&shared.comment_char[..]) {
                            self.content[i].drain(ns..ns + ni);
                        } else {
                            let mut new_line = self.content[i][..ns].to_vec();
                            new_line.extend_from_slice(&shared.comment_char);
                            new_line.extend_from_slice(&self.content[i][ns..]);
                            self.content[i] = new_line;
                        }
                    }
                }
            }
            KEY_REDRAW => {
                self.redraw(ctx, shared, true);
            }
            KEY_PLACE => {
                let here = Place { editor_id: self.id, line: self.cur_line };
                if !shared.place_list.iter().any(|p| p.editor_id == here.editor_id && p.line == here.line) {
                    if shared.place_list.len() >= shared.max_places {
                        shared.place_list.remove(0);
                    }
                    shared.place_list.push(here);
                    shared.place_index = shared.place_list.len().saturating_sub(1);
                }
            }
            KEY_NEXT_PLACE | KEY_PREV_PLACE => {
                let count = shared.place_list.len();
                if count > 0 {
                    if key == KEY_NEXT_PLACE {
                        shared.place_index = (shared.place_index + 1) % count;
                    } else {
                        shared.place_index = (shared.place_index + count - 1) % count;
                    }
                    let here = shared.place_list[shared.place_index].clone();
                    if here.editor_id == self.id {
                        self.cur_line = here.line;
                        self.row = ctx.term.height >> 1;
                    } else {
                        return Some(key);
                    }
                }
            }
            KEY_UNDO_PREV | KEY_UNDO_NEXT => {
                if !self.undo.is_empty() {
                    if key == KEY_UNDO_NEXT {
                        self.undo_index = (self.undo_index + 1) % self.undo.len();
                    } else {
                        self.undo_index = (self.undo_index + self.undo.len() - 1) % self.undo.len();
                    }
                    if let Some(item) = self.undo.get(self.undo_index) {
                        self.cur_line = item.line;
                        self.col = item.col;
                    }
                }
            }
            KEY_UNDO_YANK => {
                if let Some(item) = self.undo.get(self.undo_index) {
                    shared.yank_buffer = item.text.clone();
                }
            }
            _ => {}
        }

        Some(key)
    }

    pub fn edit_loop(&mut self, ctx: &mut Context, shared: &mut Shared) -> EditAction {
        if self.content.is_empty() {
            self.content.push(Vec::new());
        }
        self.hash = self.hash_buffer();
        self.redraw(ctx, shared, self.message.is_empty());

        loop {
            self.display_window(ctx, shared);
            let evt = ctx.input.get_input(self.key_max);
            self.message.clear();

            let key_opt = self.handle_edit_keys(ctx, shared, evt);
            let key = key_opt.unwrap_or(KEY_NONE);
            if key == KEY_QUIT {
                if self.hash != self.hash_buffer() {
                    if let Some(res) = self.line_edit(ctx, "File changed! Quit (y/N/f/s)? ", "N", None) {
                        if !res.is_empty() {
                            let c = res.as_bytes()[0].to_ascii_uppercase();
                            if c == b'F' {
                                return EditAction::ForceQuit;
                            }
                            if c == b'S' {
                                self.save_current(ctx, shared);
                                if self.hash == self.hash_buffer() {
                                    break;
                                }
                            }
                            if c == b'Y' {
                                break;
                            }
                        }
                    }
                } else {
                    break;
                }
            } else if key == KEY_FORCE_QUIT {
                return EditAction::ForceQuit;
            } else if key == KEY_GET {
                return EditAction::OpenFile;
            } else if key == KEY_NEXT {
                return EditAction::Next;
            } else if key == KEY_PREV {
                return EditAction::Prev;
            } else if key == KEY_NEXT_PLACE || key == KEY_PREV_PLACE {
                return EditAction::NextPlace;
            }
        }
        EditAction::Quit
    }

    pub fn prompt_open_file(&mut self, ctx: &mut Context, shared: &Shared) -> Option<String> {
        self.line_edit(ctx, "Open file: ", "", Some(shared.file_char()))
    }

    fn packtabs(&self, s: &[u8]) -> Vec<u8> {
        if s.contains(&b' ') {
            let mut out = Vec::new();
            let mut pos = 0usize;
            let mut i = 0;
            while i < s.len() {
                if s[i] == b' ' {
                    let start = i;
                    while i < s.len() && s[i] == b' ' {
                        i += 1;
                    }
                    let span = i - start;
                    let add = span / 8;
                    for _ in 0..add {
                        out.push(b'\t');
                        pos += 8;
                    }
                    let rest = span % 8;
                    if rest > 0 {
                        out.extend_from_slice(&vec![b' '; rest]);
                        pos += rest;
                    }
                } else {
                    out.push(s[i]);
                    pos += 1;
                    i += 1;
                }
            }
            out
        } else {
            s.to_vec()
        }
    }

    fn hash_buffer(&self) -> u32 {
        let mut res = 0u32;
        for line in &self.content {
            let mut h = 0u32;
            for &b in line {
                h = h.wrapping_mul(227).wrapping_add(b as u32 + 1);
            }
            res = res.wrapping_mul(227).wrapping_add(1) ^ h;
        }
        res & 0x3fffffff
    }

    pub fn get_file(&mut self, fname: &str) {
        if fname.is_empty() {
            return;
        }
        self.fname = fname.to_string();
        self.is_dir = false;
        if fname == "." || fname == ".." {
            if self.open_directory(fname) {
                return;
            }
        }

        match fs::File::open(fname, sys::O_RDONLY, 0) {
            Ok(file) => {
            self.write_tabs = false;
            let mut buf = Vec::new();
            let mut tmp = [0u8; 512];
            loop {
                if let Ok(n) = file.read(&mut tmp) {
                    if n == 0 {
                        break;
                    }
                    buf.extend_from_slice(&tmp[..n]);
                } else {
                    break;
                }
            }
            let mut new_content: Vec<Vec<u8>> = Vec::new();
            for line in buf.split(|&b| b == b'\n') {
                let line = if line.ends_with(&[b'\r']) {
                    &line[..line.len() - 1]
                } else {
                    line
                };
                let expanded = self.expandtabs(line);
                new_content.push(expanded);
            }
            if new_content.is_empty() {
                new_content.push(Vec::new());
            }
            self.content = new_content;
            self.is_dir = false;
            }
            Err(file_err) => {
                if self.open_directory(fname) {
                    return;
                }
                self.message.clear();
                self.message.push_str("Error: cannot open '");
                self.message.push_str(fname);
                self.message.push_str("' (errno=");
                push_usize_str(&mut self.message, file_err.errno as usize);
                self.message.push(')');
            }
        }
        self.hash = self.hash_buffer();
    }

    fn open_directory(&mut self, fname: &str) -> bool {
        match fs::chdir(fname) {
            Ok(_) => {
                self.work_dir = fs::getcwd()
                    .ok()
                    .and_then(|v| String::from_utf8(v).ok())
                    .unwrap_or_else(|| "/".to_string());
                let display = if self.work_dir == "/" {
                    "/".to_string()
                } else {
                    self.work_dir
                        .split('/')
                        .last()
                        .unwrap_or("")
                        .to_string()
                };
                self.content.clear();
                let mut line = Vec::new();
                line.extend_from_slice(b"Directory '");
                line.extend_from_slice(self.work_dir.as_bytes());
                line.extend_from_slice(b"'");
                self.content.push(line);
                self.content.push(Vec::new());
                let mut entries: Vec<String> = Vec::new();
                if let Ok(mut dir) = Dir::open(".") {
                    while let Ok(Some(ent)) = dir.next() {
                        if let Ok(name) = String::from_utf8(ent.name.clone()) {
                            entries.push(name);
                        }
                    }
                }
                entries.sort();
                for e in entries {
                    self.content.push(e.into_bytes());
                }
                self.is_dir = true;
                self.fname = display;
                self.hash = self.hash_buffer();
                true
            }
            Err(_) => false,
        }
    }

    fn put_file(&mut self, fname: &str) {
        let mut tmpfile = String::new();
        tmpfile.push_str(fname);
        tmpfile.push_str(".edittmp");
        let mut write_err: Option<i32> = None;
        {
            let file = match fs::File::open(&tmpfile, sys::O_WRONLY | sys::O_CREAT | sys::O_TRUNC, 0o666) {
                Ok(file) => file,
                Err(err) => {
                    self.message.clear();
                    self.message.push_str("Error: cannot write '");
                    self.message.push_str(fname);
                    self.message.push_str("' (errno=");
                    push_usize_str(&mut self.message, err.errno as usize);
                    self.message.push(')');
                    return;
                }
            };

            for l in &self.content {
                let out = if self.write_tabs { self.packtabs(l) } else { l.clone() };
                if let Err(err) = file.write_all(&out) {
                    write_err = Some(err.errno);
                    break;
                }
                if let Err(err) = file.write_all(b"\n") {
                    write_err = Some(err.errno);
                    break;
                }
            }
        }

        if let Some(errno) = write_err {
            let _ = fs::remove(&tmpfile);
            self.message.clear();
            self.message.push_str("Error: write failed '");
            self.message.push_str(fname);
            self.message.push_str("' (errno=");
            push_usize_str(&mut self.message, errno as usize);
            self.message.push(')');
            return;
        }

        if let Err(err) = fs::rename(&tmpfile, fname) {
            let _ = fs::remove(&tmpfile);
            self.message.clear();
            self.message.push_str("Error: cannot save '");
            self.message.push_str(fname);
            self.message.push_str("' (errno=");
            push_usize_str(&mut self.message, err.errno as usize);
            self.message.push(')');
            return;
        }
        self.message.clear();
        self.message.push_str("Saved '");
        self.message.push_str(fname);
        self.message.push('\'');
    }

    fn save_current(&mut self, ctx: &mut Context, shared: &mut Shared) {
        let name = if self.is_dir {
            String::new()
        } else {
            self.fname.clone()
        };
        let file_char = shared.file_char.clone();
        if let Some(fname) = self.line_edit(ctx, "Save File: ", &name, Some(&file_char)) {
            if fname != self.fname {
                if fs::exists(&fname) {
                    if let Some(res) = self.line_edit(ctx, "The file exists! Overwrite (y/N)? ", "N", None) {
                        if !res.is_empty() && res.as_bytes()[0].to_ascii_uppercase() != b'Y' {
                            return;
                        }
                    }
                }
            }
            self.put_file(&fname);
            if self.message.starts_with("Saved '") {
                self.fname = fname;
                self.hash = self.hash_buffer();
            }
        }
    }

    fn expandtabs(&mut self, s: &[u8]) -> Vec<u8> {
        if s.contains(&b'\t') {
            self.write_tabs = true;
            let mut out = Vec::new();
            let mut pos = 0usize;
            for &c in s {
                if c == b'\t' {
                    let spaces = 8 - (pos % 8);
                    out.extend_from_slice(&vec![b' '; spaces]);
                    pos += spaces;
                } else {
                    out.push(c);
                    pos += 1;
                }
            }
            out
        } else {
            s.to_vec()
        }
    }
}
