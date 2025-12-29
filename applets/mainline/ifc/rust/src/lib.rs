#![no_std]

extern crate alloc;

use alloc::format;
use alloc::string::String;

use magnolia_applet::cli;
use magnolia_applet::errno::{Error, ENOTSUP};
use magnolia_applet::net::{self, NetIpv4};
use magnolia_applet::{entry, Args};
use magnolia_applet::{eprintln, println};

fn usage() {
    println!("usage: ifc [--quiet|--verbose|--json] <command> [args]\n");
    println!("commands:");
    println!("  list");
    println!("  show <iface>");
    println!("  up <iface>");
    println!("  down <iface>");
    println!("  dhcp <iface> on|off");
    println!("  ip <iface> <addr> <mask> <gw>");
    println!("  default <iface>");
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

fn parse_ipv4(value: &str) -> Option<u32> {
    let mut parts = value.split('.');
    let a = parts.next()?.parse::<u8>().ok()?;
    let b = parts.next()?.parse::<u8>().ok()?;
    let c = parts.next()?.parse::<u8>().ok()?;
    let d = parts.next()?.parse::<u8>().ok()?;
    if parts.next().is_some() {
        return None;
    }
    Some(net::ipv4_from_parts(a, b, c, d))
}

fn ipv4_to_string(addr: u32) -> String {
    let octets = net::ipv4_to_octets(addr);
    format!("{}.{}.{}.{}", octets[0], octets[1], octets[2], octets[3])
}

fn mac_to_string(mac: [u8; 6]) -> String {
    format!(
        "{:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
    )
}

fn cmd_list(quiet: bool, verbose: bool) -> i32 {
    let ifaces = match net::list_ifaces() {
        Ok(list) => list,
        Err(err) => return err_exit("ifc list failed", err, verbose),
    };

    let default = net::get_default_iface().ok();

    if !quiet {
        if ifaces.is_empty() {
            println!("no interfaces");
        } else {
            for iface in ifaces {
                if default.as_ref() == Some(&iface) {
                    println!("{} (default)", iface);
                } else {
                    println!("{}", iface);
                }
            }
        }
    }
    0
}

fn cmd_show(iface: &str, quiet: bool, verbose: bool) -> i32 {
    let info = match net::get_iface_info(Some(iface)) {
        Ok(info) => info,
        Err(err) => return err_exit("ifc show failed", err, verbose),
    };

    if quiet {
        return 0;
    }

    let state = if info.state == 1 { "UP" } else { "DOWN" };
    let link = if info.link_state == 1 { "UP" } else { "DOWN" };
    let mac = mac_to_string(info.mac);

    println!("iface: {}", info.name);
    println!("state: {}", state);
    println!("link: {}", link);
    println!("mtu: {}", info.mtu);
    println!("mac: {}", mac);
    println!("dhcp: {}", if info.dhcp_enabled { "on" } else { "off" });
    if info.has_ipv4 {
        let ip = ipv4_to_string(info.ipv4.addr);
        let mask = ipv4_to_string(info.ipv4.mask);
        let gw = ipv4_to_string(info.ipv4.gw);
        println!("ipv4: {} mask {} gw {}", ip, mask, gw);
    } else {
        println!("ipv4: none");
    }
    0
}

fn cmd_up(iface: &str, verbose: bool) -> i32 {
    match net::iface_up(iface) {
        Ok(_) => 0,
        Err(err) => err_exit("ifc up failed", err, verbose),
    }
}

fn cmd_down(iface: &str, verbose: bool) -> i32 {
    match net::iface_down(iface) {
        Ok(_) => 0,
        Err(err) => err_exit("ifc down failed", err, verbose),
    }
}

fn cmd_dhcp(iface: &str, enabled: bool, verbose: bool) -> i32 {
    let result = if enabled {
        net::iface_dhcp_start(iface)
    } else {
        net::iface_dhcp_stop(iface)
    };
    match result {
        Ok(_) => 0,
        Err(err) => err_exit("ifc dhcp failed", err, verbose),
    }
}

fn cmd_ip(iface: &str, addr: &str, mask: &str, gw: &str, verbose: bool) -> i32 {
    let addr = match parse_ipv4(addr) {
        Some(value) => value,
        None => {
            eprintln!("ifc ip: invalid address");
            return 2;
        }
    };
    let mask = match parse_ipv4(mask) {
        Some(value) => value,
        None => {
            eprintln!("ifc ip: invalid mask");
            return 2;
        }
    };
    let gw = match parse_ipv4(gw) {
        Some(value) => value,
        None => {
            eprintln!("ifc ip: invalid gateway");
            return 2;
        }
    };

    let ipv4 = NetIpv4 { addr, mask, gw };
    match net::iface_set_ipv4(iface, &ipv4) {
        Ok(_) => 0,
        Err(err) => err_exit("ifc ip failed", err, verbose),
    }
}

fn cmd_default(iface: &str, verbose: bool) -> i32 {
    match net::set_default_iface(iface) {
        Ok(_) => 0,
        Err(err) => err_exit("ifc default failed", err, verbose),
    }
}

fn main(args: Args) -> i32 {
    let cli = match cli::parse(args) {
        Ok(cli) => cli,
        Err(_) => {
            eprintln!("ifc: invalid arguments");
            return 2;
        }
    };

    if cli.json {
        eprintln!("ifc: --json not supported");
        return 3;
    }

    let args = cli.args();
    if args.is_empty() || args[0] == "--help" || args[0] == "-h" {
        usage();
        return 0;
    }

    match args[0].as_str() {
        "list" => cmd_list(cli.quiet, cli.verbose),
        "show" => {
            if args.len() < 2 {
                eprintln!("ifc show: missing iface");
                return 2;
            }
            cmd_show(&args[1], cli.quiet, cli.verbose)
        }
        "up" => {
            if args.len() < 2 {
                eprintln!("ifc up: missing iface");
                return 2;
            }
            cmd_up(&args[1], cli.verbose)
        }
        "down" => {
            if args.len() < 2 {
                eprintln!("ifc down: missing iface");
                return 2;
            }
            cmd_down(&args[1], cli.verbose)
        }
        "dhcp" => {
            if args.len() < 3 {
                eprintln!("ifc dhcp: missing args");
                return 2;
            }
            let enabled = match args[2].as_str() {
                "on" => true,
                "off" => false,
                _ => {
                    eprintln!("ifc dhcp: expected on|off");
                    return 2;
                }
            };
            cmd_dhcp(&args[1], enabled, cli.verbose)
        }
        "ip" => {
            if args.len() < 5 {
                eprintln!("ifc ip: missing args");
                return 2;
            }
            cmd_ip(&args[1], &args[2], &args[3], &args[4], cli.verbose)
        }
        "default" => {
            if args.len() < 2 {
                eprintln!("ifc default: missing iface");
                return 2;
            }
            cmd_default(&args[1], cli.verbose)
        }
        _ => {
            eprintln!("ifc: unknown command");
            2
        }
    }
}

entry!(main);
