#![no_std]

use magnolia_applet::cli;
use magnolia_applet::elf;
use magnolia_applet::{entry, Args};
use magnolia_applet::{eprintln, println};

const FAIL_IFC: i32 = 10;
const FAIL_NETSTAT_IF: i32 = 11;
const FAIL_NETSTAT_SOCK: i32 = 12;
const FAIL_SYSCTL: i32 = 13;
const FAIL_DMESG: i32 = 14;

struct Step {
    name: &'static str,
    path: &'static str,
    args: &'static [&'static str],
    fail_code: i32,
}

fn usage() {
    println!("usage: aptest [--quiet|--verbose|--json]");
}

fn run_step(step: &Step, verbose: bool, quiet: bool) -> Result<(), i32> {
    let rc = match elf::run_file(step.path, step.args) {
        Ok(rc) => rc,
        Err(err) => {
            if verbose {
                eprintln!("aptest: {} failed errno={}", step.name, err.errno);
            } else {
                eprintln!("aptest: {} failed", step.name);
            }
            return Err(step.fail_code);
        }
    };

    if rc != 0 {
        if verbose {
            eprintln!("aptest: {} exited rc={}", step.name, rc);
        } else {
            eprintln!("aptest: {} failed", step.name);
        }
        return Err(step.fail_code);
    }

    if !quiet {
        println!("ok: {}", step.name);
    }
    Ok(())
}

fn main(args: Args) -> i32 {
    let cli = match cli::parse(args) {
        Ok(cli) => cli,
        Err(_) => {
            eprintln!("aptest: invalid arguments");
            return 2;
        }
    };

    if cli.json {
        eprintln!("aptest: --json not supported");
        return 3;
    }

    let args = cli.args();
    if args.len() > 0 {
        if args[0] == "--help" || args[0] == "-h" {
            usage();
            return 0;
        }
        eprintln!("aptest: unexpected argument");
        return 2;
    }

    let steps = [
        Step {
            name: "ifc list",
            path: "/bin/ifc",
            args: &["--quiet", "list"],
            fail_code: FAIL_IFC,
        },
        Step {
            name: "netstat if",
            path: "/bin/netstat",
            args: &["--quiet", "if"],
            fail_code: FAIL_NETSTAT_IF,
        },
        Step {
            name: "netstat sock",
            path: "/bin/netstat",
            args: &["--quiet", "sock"],
            fail_code: FAIL_NETSTAT_SOCK,
        },
        Step {
            name: "sysctl list",
            path: "/bin/sysctl",
            args: &["--quiet", "list"],
            fail_code: FAIL_SYSCTL,
        },
        Step {
            name: "dmesg",
            path: "/bin/dmesg",
            args: &["--quiet", "--lines", "5"],
            fail_code: FAIL_DMESG,
        },
    ];

    for step in &steps {
        if let Err(code) = run_step(step, cli.verbose, cli.quiet) {
            return code;
        }
    }

    if !cli.quiet {
        println!("aptest: ok");
    }

    0
}

entry!(main);
