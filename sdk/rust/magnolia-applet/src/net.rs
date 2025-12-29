use alloc::ffi::CString;
use alloc::string::String;
use alloc::vec;
use alloc::vec::Vec;
use core::mem;
use core::str;

use crate::errno::{Error, Result};
use crate::sys;

#[derive(Clone, Debug)]
pub struct NetIpv4 {
    pub addr: u32,
    pub mask: u32,
    pub gw: u32,
}

#[derive(Clone, Debug)]
pub struct NetIfaceInfo {
    pub name: String,
    pub state: u8,
    pub link_state: u8,
    pub mtu: u16,
    pub mac: [u8; 6],
    pub dhcp_enabled: bool,
    pub has_ipv4: bool,
    pub ipv4: NetIpv4,
}

#[derive(Clone, Debug)]
pub struct NetdevStats {
    pub rx_packets: u64,
    pub rx_bytes: u64,
    pub rx_drops: u64,
    pub rx_errors: u64,
    pub tx_packets: u64,
    pub tx_bytes: u64,
    pub tx_drops: u64,
    pub tx_errors: u64,
}

#[derive(Clone, Debug)]
pub struct NetStats {
    pub sockets_open: usize,
    pub rx_bytes: u64,
    pub tx_bytes: u64,
    pub rx_errors: u64,
    pub tx_errors: u64,
    pub rx_drops: u64,
    pub tx_drops: u64,
}

#[derive(Clone, Debug)]
pub struct NetSocketSummary {
    pub job_id: usize,
    pub count: u32,
}

fn cstring_from_str(value: &str) -> Result<CString> {
    CString::new(value).map_err(|_| Error { errno: 22 })
}

fn cchar_array_to_string(buf: &[sys::c_char]) -> Result<String> {
    let len = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
    let bytes = &buf[..len];
    let s = str::from_utf8(bytes).map_err(|_| Error { errno: 22 })?;
    Ok(String::from(s))
}

fn rc_to_result(rc: i32) -> Result<()> {
    if rc < 0 {
        return Err(Error { errno: -rc });
    }
    Ok(())
}

pub fn list_ifaces() -> Result<Vec<String>> {
    let mut count: sys::size_t = 0;
    let rc = unsafe { sys::m_net_list_ifaces(core::ptr::null_mut(), 0, &mut count) };
    rc_to_result(rc)?;

    if count == 0 {
        return Ok(Vec::new());
    }

    let mut entries: Vec<sys::magnolia_net_iface_name_t> = Vec::with_capacity(count as usize);
    for _ in 0..count {
        entries.push(unsafe { mem::zeroed() });
    }
    let rc = unsafe {
        sys::m_net_list_ifaces(entries.as_mut_ptr(), entries.len(), &mut count)
    };
    rc_to_result(rc)?;

    let mut out = Vec::new();
    for entry in entries.iter() {
        let name = cchar_array_to_string(&entry.name)?;
        if !name.is_empty() {
            out.push(name);
        }
    }
    Ok(out)
}

pub fn get_iface_info(name: Option<&str>) -> Result<NetIfaceInfo> {
    let mut info: sys::magnolia_net_iface_info_t = unsafe { mem::zeroed() };
    info.size = mem::size_of::<sys::magnolia_net_iface_info_t>() as u32;

    let rc = if let Some(name) = name {
        let cname = cstring_from_str(name)?;
        unsafe { sys::m_net_iface_get_info(cname.as_ptr(), &mut info) }
    } else {
        unsafe { sys::m_net_iface_get_info(core::ptr::null(), &mut info) }
    };
    rc_to_result(rc)?;

    let name = cchar_array_to_string(&info.name)?;
    Ok(NetIfaceInfo {
        name,
        state: info.state,
        link_state: info.link_state,
        mtu: info.mtu,
        mac: info.mac,
        dhcp_enabled: info.dhcp_enabled != 0,
        has_ipv4: info.has_ipv4 != 0,
        ipv4: NetIpv4 {
            addr: info.ipv4.addr,
            mask: info.ipv4.mask,
            gw: info.ipv4.gw,
        },
    })
}

pub fn iface_up(name: &str) -> Result<()> {
    let cname = cstring_from_str(name)?;
    let rc = unsafe { sys::m_net_iface_up(cname.as_ptr()) };
    rc_to_result(rc)
}

pub fn iface_down(name: &str) -> Result<()> {
    let cname = cstring_from_str(name)?;
    let rc = unsafe { sys::m_net_iface_down(cname.as_ptr()) };
    rc_to_result(rc)
}

pub fn iface_set_ipv4(name: &str, ipv4: &NetIpv4) -> Result<()> {
    let cname = cstring_from_str(name)?;
    let addr = sys::magnolia_net_ipv4_t {
        addr: ipv4.addr,
        mask: ipv4.mask,
        gw: ipv4.gw,
    };
    let rc = unsafe { sys::m_net_iface_set_ipv4(cname.as_ptr(), &addr) };
    rc_to_result(rc)
}

pub fn iface_dhcp_start(name: &str) -> Result<()> {
    let cname = cstring_from_str(name)?;
    let rc = unsafe { sys::m_net_iface_dhcp_start(cname.as_ptr()) };
    rc_to_result(rc)
}

pub fn iface_dhcp_stop(name: &str) -> Result<()> {
    let cname = cstring_from_str(name)?;
    let rc = unsafe { sys::m_net_iface_dhcp_stop(cname.as_ptr()) };
    rc_to_result(rc)
}

pub fn get_stats(name: Option<&str>) -> Result<NetdevStats> {
    let mut stats: sys::magnolia_netdev_stats_t = unsafe { mem::zeroed() };
    let rc = if let Some(name) = name {
        let cname = cstring_from_str(name)?;
        unsafe { sys::m_net_get_stats(cname.as_ptr(), &mut stats) }
    } else {
        unsafe { sys::m_net_get_stats(core::ptr::null(), &mut stats) }
    };
    rc_to_result(rc)?;

    Ok(NetdevStats {
        rx_packets: stats.rx_packets,
        rx_bytes: stats.rx_bytes,
        rx_drops: stats.rx_drops,
        rx_errors: stats.rx_errors,
        tx_packets: stats.tx_packets,
        tx_bytes: stats.tx_bytes,
        tx_drops: stats.tx_drops,
        tx_errors: stats.tx_errors,
    })
}

pub fn get_default_iface() -> Result<String> {
    let mut buf = [0 as sys::c_char; sys::MAGNOLIA_NET_IFACE_NAME_MAX];
    let rc = unsafe { sys::m_net_get_default_iface(buf.as_mut_ptr(), buf.len()) };
    rc_to_result(rc)?;
    cchar_array_to_string(&buf)
}

pub fn set_default_iface(name: &str) -> Result<()> {
    let cname = cstring_from_str(name)?;
    let rc = unsafe { sys::m_net_set_default_iface(cname.as_ptr()) };
    rc_to_result(rc)
}

pub fn stats_snapshot() -> Result<NetStats> {
    let mut stats: sys::magnolia_net_stats_t = unsafe { mem::zeroed() };
    let rc = unsafe { sys::m_net_stats_snapshot_api(&mut stats) };
    rc_to_result(rc)?;
    Ok(NetStats {
        sockets_open: stats.sockets_open,
        rx_bytes: stats.rx_bytes,
        tx_bytes: stats.tx_bytes,
        rx_errors: stats.rx_errors,
        tx_errors: stats.tx_errors,
        rx_drops: stats.rx_drops,
        tx_drops: stats.tx_drops,
    })
}

pub fn socket_summary() -> Result<Vec<NetSocketSummary>> {
    let mut count: sys::size_t = 0;
    let rc = unsafe { sys::m_net_socket_summary(core::ptr::null_mut(), 0, &mut count) };
    rc_to_result(rc)?;

    if count == 0 {
        return Ok(Vec::new());
    }

    let mut entries: Vec<sys::magnolia_net_socket_summary_t> = Vec::with_capacity(count as usize);
    for _ in 0..count {
        entries.push(unsafe { mem::zeroed() });
    }
    let rc = unsafe {
        sys::m_net_socket_summary(entries.as_mut_ptr(), entries.len(), &mut count)
    };
    rc_to_result(rc)?;

    let mut out = Vec::new();
    for entry in entries.iter() {
        out.push(NetSocketSummary {
            job_id: entry.job_id as usize,
            count: entry.count,
        });
    }
    Ok(out)
}

pub fn ipv4_from_parts(a: u8, b: u8, c: u8, d: u8) -> u32 {
    u32::from_be_bytes([a, b, c, d])
}

pub fn ipv4_to_octets(addr: u32) -> [u8; 4] {
    addr.to_be_bytes()
}
