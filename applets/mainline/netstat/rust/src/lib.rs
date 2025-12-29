#![no_std]

extern crate alloc;

use alloc::format;
use magnolia_applet::cli;
use magnolia_applet::errno::{Error, ENOTSUP};
use magnolia_applet::net;
use magnolia_applet::{entry, Args};
use magnolia_applet::{eprintln, println};

fn usage() {
    println!("usage: netstat [--quiet|--verbose|--json] <if|sock|route|dns>");
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

fn cmd_if(quiet: bool, verbose: bool) -> i32 {
    let ifaces = match net::list_ifaces() {
        Ok(list) => list,
        Err(err) => return err_exit("netstat if failed", err, verbose),
    };

    if ifaces.is_empty() {
        if !quiet {
            println!("no interfaces");
        }
        return 0;
    }

    if !quiet {
        println!("iface rx_bytes rx_packets tx_bytes tx_packets rx_err tx_err rx_drop tx_drop");
    }

    for iface in ifaces {
        let stats = match net::get_stats(Some(&iface)) {
            Ok(stats) => stats,
            Err(err) => return err_exit("netstat if failed", err, verbose),
        };
        if !quiet {
            println!(
                "{} {} {} {} {} {} {} {} {}",
                iface,
                stats.rx_bytes,
                stats.rx_packets,
                stats.tx_bytes,
                stats.tx_packets,
                stats.rx_errors,
                stats.tx_errors,
                stats.rx_drops,
                stats.tx_drops
            );
        }
    }
    0
}

fn cmd_sock(quiet: bool, verbose: bool) -> i32 {
    let summary = match net::socket_summary() {
        Ok(summary) => summary,
        Err(err) => return err_exit("netstat sock failed", err, verbose),
    };

    let totals = net::stats_snapshot().ok();

    if !quiet {
        if let Some(stats) = totals {
            println!("total_sockets={}", stats.sockets_open);
        }
        if summary.is_empty() {
            println!("no sockets");
        } else {
            for entry in summary {
                let job = format!("0x{:x}", entry.job_id);
                println!("job={} count={}", job, entry.count);
            }
        }
    }
    0
}

fn cmd_not_supported(feature: &str) -> i32 {
    eprintln!("netstat {}: not supported", feature);
    3
}

fn main(args: Args) -> i32 {
    let cli = match cli::parse(args) {
        Ok(cli) => cli,
        Err(_) => {
            eprintln!("netstat: invalid arguments");
            return 2;
        }
    };

    if cli.json {
        eprintln!("netstat: --json not supported");
        return 3;
    }

    let args = cli.args();
    if args.is_empty() || args[0] == "--help" || args[0] == "-h" {
        usage();
        return 0;
    }

    match args[0].as_str() {
        "if" => cmd_if(cli.quiet, cli.verbose),
        "sock" => cmd_sock(cli.quiet, cli.verbose),
        "route" => cmd_not_supported("route"),
        "dns" => cmd_not_supported("dns"),
        _ => {
            eprintln!("netstat: unknown command");
            2
        }
    }
}

entry!(main);
