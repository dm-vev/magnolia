#![no_std]
/*
 * Magnolia OS - Edit Applet
 * Copyright (c) 2025 Magnolia Project
 * All rights reserved.
 *
 * Production-grade editor module for Magnolia OS.
 */

extern crate alloc;

mod app;
mod editor;
mod input;
mod key;
mod terminal;
mod tty;
mod util;

use magnolia_applet::Args;

fn main(args: Args) -> i32 {
    app::main(args)
}

magnolia_applet::entry!(main);
