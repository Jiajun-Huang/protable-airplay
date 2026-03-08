#!/usr/bin/env python3
"""
Register two Zeroconf services for a short, fixed duration (30s) and exit.
Useful for automated captures and quick verification.
"""
import socket
import struct
import time
import uuid

from zeroconf import ServiceInfo, Zeroconf


def get_local_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 53))
        ip = s.getsockname()[0]
    except Exception:
        ip = "127.0.0.1"
    finally:
        s.close()
    return ip


def get_windows_hostname():
    """Get the Windows computer hostname."""
    try:
        hostname = socket.gethostname()
        # Remove domain suffix if present (e.g., "JIAJUN_PC.local" → "JIAJUN_PC")
        if "." in hostname:
            hostname = hostname.split(".")[0]
        return hostname
    except Exception:
        return "Windows"


def get_mac_address():
    """Get MAC address as hex string for instance naming."""
    try:
        mac = uuid.getnode()
        return "{:012X}".format(mac)
    except Exception:
        return "000000000000"


def get_ipv6_addresses():
    """Try to get IPv6 addresses for the network interface."""
    try:
        # Get IPv6 addresses via socket
        ipv6_addrs = []
        interfaces = socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET6)
        for family, socktype, proto, canonname, sockaddr in interfaces:
            ipv6_addr = sockaddr[0]
            if ipv6_addr not in ipv6_addrs and not ipv6_addr.startswith("::"):
                ipv6_addrs.append(ipv6_addr)
        return ipv6_addrs
    except Exception:
        return []


def make_props():
    # specification-derived TXT values for RAOP advertisement (per Apple AirPlay spec)
    props = {
        "txtvers": "1",
        "ch": "2",  # stereo (2 channels)
        "cn": "0,1,2,3",  # codecs: PCM, ALAC, AAC, AAC-ELD
        "da": "true",  # dynamic audio rate conversion
        "et": "0,3,5",  # encryption: no encryption, FairPlay, FairPlayv2.5
        "md": "0,1,2",  # metadata: text, artwork, progress
        "pw": "false",  # password required
        "sv": "false",  # special modes
        "sr": "44100",  # sample rate
        "ss": "16",  # sample size in bits
        "tp": "UDP",  # transport protocol (RAOP uses UDP, not TCP)
        "vn": "65537",  # AirPlay protocol version
        "vs": "130.14",  # device software version
        "am": "PortableSpeaker",  # audio module/device name
        "sf": "0x4",  # status flags
    }
    return props


def main(duration=30):
    ip_v4 = get_local_ip()
    addr_v4 = socket.inet_aton(ip_v4)
    ipv6_addrs = get_ipv6_addresses()

    # Get Windows hostname
    windows_hostname = get_windows_hostname()
    hostname = f"{windows_hostname}.local."

    # Create instance name: MAC@AppName@Hostname pattern (matching UxPlay)
    mac_hex = get_mac_address()
    instance_base = f"{mac_hex}@PortableSpeaker@{windows_hostname}"

    zeroconf = Zeroconf()
    info_raop = None
    info_airplay = None

    try:
        props = make_props()

        # Prepare address list (IPv4 + any IPv6)
        addresses = [addr_v4]
        if ipv6_addrs:
            for ipv6_addr in ipv6_addrs:
                try:
                    addresses.append(socket.inet_pton(socket.AF_INET6, ipv6_addr))
                except Exception:
                    pass  # Skip invalid IPv6

        # RAOP service (audio streaming)
        # SRV record will point to actual Windows hostname (e.g., "jiajun-PC.local")
        info_raop = ServiceInfo(
            "_raop._tcp.local.",
            f"{instance_base}._raop._tcp.local.",
            addresses=addresses,
            port=5000,
            properties=props,
            server=hostname,
        )

        # AIRPLAY service (also needed for iOS discovery)
        info_airplay = ServiceInfo(
            "_airplay._tcp.local.",
            f"{instance_base}._airplay._tcp.local.",
            addresses=addresses,
            port=5000,
            properties=props,
            server=hostname,
        )

        zeroconf.register_service(info_raop)
        zeroconf.register_service(info_airplay)
        print(f"[zeroconf] registered _raop._tcp.local and _airplay._tcp.local")
        print(f"[zeroconf] instance: {instance_base}")
        print(f"[zeroconf] hostname: {hostname}")
        print(f"[zeroconf] IPv4: {ip_v4}")
        if ipv6_addrs:
            print(f"[zeroconf] IPv6: {', '.join(ipv6_addrs[:3])}")
        print(f"[zeroconf] advertising for {duration}s (both RAOP and AirPlay)")

        time.sleep(duration)

    except Exception as e:
        print("error", e)
        import traceback

        traceback.print_exc()
    finally:
        try:
            if info_raop:
                zeroconf.unregister_service(info_raop)
            if info_airplay:
                zeroconf.unregister_service(info_airplay)
        except Exception:
            pass
        zeroconf.close()
        print("[zeroconf] stopped")


if __name__ == "__main__":
    main(30)
