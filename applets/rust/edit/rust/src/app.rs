/*
 * Magnolia OS - Edit Applet (App Entrypoint)
 * Copyright (c) 2025 Magnolia Project
 * All rights reserved.
 */
use alloc::string::String;
use alloc::vec::Vec;

use magnolia_applet::fs;
use magnolia_applet::Args;

use crate::editor::{Context, EditAction, Editor, Shared};
use crate::tty::TtyGuard;

fn edit_main(args: &[String]) -> i32 {
    let _tty_guard = TtyGuard::new();
    let mut ctx = Context::new();
    let mut shared = Shared::new();
    let mut editors: Vec<Editor> = Vec::new();
    let mut next_id = 1u32;

    if !args.is_empty() {
        for name in args {
            let mut editor = Editor::new(next_id, 4, 500);
            next_id += 1;
            editor.get_file(name);
            editors.push(editor);
        }
    } else {
        let mut editor = Editor::new(next_id, 4, 500);
        next_id += 1;
        let cwd = fs::getcwd().ok()
            .and_then(|v| String::from_utf8(v).ok())
            .unwrap_or_else(|| String::from("/"));
        editor.get_file(&cwd);
        editors.push(editor);
    }

    let mut index = 0usize;

    loop {
        if editors.is_empty() {
            break;
        }
        if index >= editors.len() {
            index = 0;
        }
        let action = {
            let editor = &mut editors[index];
            editor.edit_loop(&mut ctx, &mut shared)
        };
        match action {
            EditAction::Quit => {
                if editors.len() == 1 {
                    break;
                }
                editors.remove(index);
                if index >= editors.len() {
                    index = editors.len().saturating_sub(1);
                }
            }
            EditAction::ForceQuit => break,
            EditAction::OpenFile => {
                let fname = {
                    let editor = &mut editors[index];
                    editor.prompt_open_file(&mut ctx, &shared)
                };
                if let Some(fname) = fname {
                    let mut editor = Editor::new(next_id, 4, 500);
                    next_id += 1;
                    editor.get_file(&fname);
                    editors.push(editor);
                    index = editors.len().saturating_sub(1);
                }
            }
            EditAction::Next => {
                index = (index + 1) % editors.len();
            }
            EditAction::Prev => {
                index = (index + editors.len() - 1) % editors.len();
            }
            EditAction::NextPlace => {
                if let Some(editor_id) = shared.current_place_editor() {
                    if let Some((pos, _)) = editors.iter().enumerate().find(|(_, e)| e.id() == editor_id) {
                        index = pos;
                    }
                }
            }
        }
    }

    ctx.reset_screen();
    0
}

fn args_to_vec(args: Args) -> Vec<String> {
    let mut out = Vec::new();
    for (i, arg) in args.iter().enumerate() {
        if i == 0 {
            continue;
        }
        if let Ok(s) = arg.to_str() {
            out.push(String::from(s));
        }
    }
    out
}

pub fn main(args: Args) -> i32 {
    let argv = args_to_vec(args);
    edit_main(&argv)
}
