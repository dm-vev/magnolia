#![no_std]

extern crate alloc;

use magnolia_applet::cli;
use magnolia_applet::errno::{Error, ENOENT, ENOTSUP};
use magnolia_applet::sysctl;
use magnolia_applet::{entry, Args};
use magnolia_applet::{eprintln, println};

fn usage() {
    println!("usage: sysctl [--quiet|--verbose|--json] <get|set|list> [args]");
}

fn err_exit(op: &str, err: Error, verbose: bool) -> i32 {
    if verbose {
        eprintln!("{}: errno={}", op, err.errno);
    } else {
        eprintln!("{}", op);
    }
    if err.errno == ENOTSUP {
        return 3;
    }
    if err.errno > 0 && err.errno < 256 {
        err.errno
    } else {
        1
    }
}

fn main(args: Args) -> i32 {
    let cli = match cli::parse(args) {
        Ok(cli) => cli,
        Err(_) => {
            eprintln!("sysctl: invalid arguments");
            return 2;
        }
    };

    if cli.json {
        eprintln!("sysctl: --json not supported");
        return 3;
    }

    let args = cli.args();
    if args.is_empty() || args[0] == "--help" || args[0] == "-h" {
        usage();
        return 0;
    }

    match args[0].as_str() {
        "get" => {
            if args.len() < 2 {
                eprintln!("sysctl get: missing key");
                return 2;
            }
            match sysctl::get(&args[1]) {
                Ok(value) => {
                    if !cli.quiet {
                        println!("{}", value);
                    }
                    0
                }
                Err(err) => err_exit("sysctl get failed", err, cli.verbose),
            }
        }
        "set" => {
            if args.len() < 3 {
                eprintln!("sysctl set: missing key/value");
                return 2;
            }
            match sysctl::set(&args[1], &args[2]) {
                Ok(_) => 0,
                Err(err) => err_exit("sysctl set failed", err, cli.verbose),
            }
        }
        "list" => {
            let prefix = if args.len() > 1 { Some(args[1].as_str()) } else { None };
            match sysctl::list(prefix) {
                Ok(items) => {
                    if !cli.quiet {
                        for (key, value) in items {
                            println!("{}={}", key, value);
                        }
                    }
                    0
                }
                Err(err) => {
                    if err.errno == ENOENT {
                        return 0;
                    }
                    err_exit("sysctl list failed", err, cli.verbose)
                }
            }
        }
        _ => {
            eprintln!("sysctl: unknown command");
            2
        }
    }
}

entry!(main);
