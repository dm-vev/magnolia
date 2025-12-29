use alloc::string::String;
use alloc::vec::Vec;

use crate::errno::{Error, Result};
use crate::Args;

#[derive(Debug, Clone)]
pub struct CliArgs {
    pub argv: Vec<String>,
    pub quiet: bool,
    pub verbose: bool,
    pub json: bool,
}

impl CliArgs {
    pub fn prog(&self) -> &str {
        self.argv.first().map(|s| s.as_str()).unwrap_or("")
    }

    pub fn args(&self) -> &[String] {
        if self.argv.len() > 1 {
            &self.argv[1..]
        } else {
            &[]
        }
    }
}

pub fn parse(args: Args) -> Result<CliArgs> {
    let mut raw: Vec<String> = Vec::new();
    for arg in args.iter() {
        let s = arg.to_str().map_err(|_| Error { errno: 22 })?;
        raw.push(String::from(s));
    }

    let mut argv: Vec<String> = Vec::new();
    let mut quiet = false;
    let mut verbose = false;
    let mut json = false;
    let mut stop_flags = false;

    for (idx, arg) in raw.iter().enumerate() {
        if idx == 0 {
            argv.push(arg.clone());
            continue;
        }

        if !stop_flags {
            match arg.as_str() {
                "--" => {
                    stop_flags = true;
                    continue;
                }
                "--quiet" => {
                    quiet = true;
                    continue;
                }
                "--verbose" => {
                    verbose = true;
                    continue;
                }
                "--json" => {
                    json = true;
                    continue;
                }
                _ => {}
            }
        }

        argv.push(arg.clone());
    }

    Ok(CliArgs {
        argv,
        quiet,
        verbose,
        json,
    })
}
