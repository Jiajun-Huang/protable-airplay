#!/usr/bin/env python3
"""
Custom mDNS advertiser with manual packet construction.
Includes the critical _services._dns-sd._udp.local PTR record
that tells iOS what service types are available.
"""
import socket
import struct
import time
import uuid


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
        if "." in hostname:
            hostname = hostname.split(".")[0]
        return hostname
    except Exception:
        return "Windows"


def encode_dns_name(name):
    """Encode a domain name in DNS format (labels separated by length bytes)."""
    parts = name.rstrip(".").split(".")
    result = bytearray()
    for part in parts:
        result.append(len(part))
        result.extend(part.encode())
    result.append(0)  # null terminator
    return bytes(result)


def build_mdns_packet(ip, hostname, instance_raop, instance_airplay):
    """
    Build a complete mDNS response packet with:
    - Meta PTR: _services._dns-sd._udp.local → _raop._tcp.local
    - Service PTR: _raop._tcp.local → instance
    - SRV: instance → hostname:port
    - TXT: instance → properties
    - A: hostname → IP
    """
    buf = bytearray()

    # DNS Header
    buf.extend(struct.pack(">H", 0))  # Transaction ID
    buf.extend(struct.pack(">H", 0x8400))  # Flags: response, no error
    buf.extend(struct.pack(">H", 0))  # Questions
    buf.extend(
        struct.pack(">H", 9)
    )  # Answer RRs (meta-PTR, 2x service PTRs, 2x SRVs, 2x TXTs, 1x A, 1x AAAA)
    buf.extend(struct.pack(">H", 0))  # Authority
    buf.extend(struct.pack(">H", 0))  # Additional

    # Meta PTR: _services._dns-sd._udp.local PTR _raop._tcp.local
    buf.extend(encode_dns_name("_services._dns-sd._udp.local"))
    buf.extend(struct.pack(">H", 12))  # PTR record type
    buf.extend(struct.pack(">H", 0x8001))  # Cache flush + IN
    buf.extend(struct.pack(">I", 120))  # TTL
    rdlen_pos = len(buf)
    buf.extend(struct.pack(">H", 0))  # RDLEN placeholder
    rdata_start = len(buf)
    buf.extend(encode_dns_name("_raop._tcp.local"))
    rdlen = len(buf) - rdata_start
    struct.pack_into(">H", buf, rdlen_pos, rdlen)

    # Service PTR: _raop._tcp.local PTR instance
    buf.extend(encode_dns_name("_raop._tcp.local"))
    buf.extend(struct.pack(">H", 12))  # PTR type
    buf.extend(struct.pack(">H", 0x8001))  # Cache flush + IN
    buf.extend(struct.pack(">I", 120))  # TTL
    rdlen_pos = len(buf)
    buf.extend(struct.pack(">H", 0))  # RDLEN placeholder
    rdata_start = len(buf)
    buf.extend(encode_dns_name(instance_raop + "._raop._tcp.local"))
    rdlen = len(buf) - rdata_start
    struct.pack_into(">H", buf, rdlen_pos, rdlen)

    # Service PTR: _airplay._tcp.local PTR instance
    buf.extend(encode_dns_name("_airplay._tcp.local"))
    buf.extend(struct.pack(">H", 12))  # PTR type
    buf.extend(struct.pack(">H", 0x8001))  # Cache flush + IN
    buf.extend(struct.pack(">I", 120))  # TTL
    rdlen_pos = len(buf)
    buf.extend(struct.pack(">H", 0))  # RDLEN placeholder
    rdata_start = len(buf)
    buf.extend(encode_dns_name(instance_airplay + "._airplay._tcp.local"))
    rdlen = len(buf) - rdata_start
    struct.pack_into(">H", buf, rdlen_pos, rdlen)

    # SRV: instance._raop._tcp.local SRV hostname:5000
    buf.extend(encode_dns_name(instance_raop + "._raop._tcp.local"))
    buf.extend(struct.pack(">H", 33))  # SRV type
    buf.extend(struct.pack(">H", 0x8001))  # Cache flush + IN
    buf.extend(struct.pack(">I", 120))  # TTL
    rdlen_pos = len(buf)
    buf.extend(struct.pack(">H", 0))  # RDLEN placeholder
    rdata_start = len(buf)
    buf.extend(struct.pack(">H", 0))  # Priority
    buf.extend(struct.pack(">H", 0))  # Weight
    buf.extend(struct.pack(">H", 5000))  # Port
    buf.extend(encode_dns_name(hostname + ".local"))
    rdlen = len(buf) - rdata_start
    struct.pack_into(">H", buf, rdlen_pos, rdlen)

    # SRV: instance._airplay._tcp.local SRV hostname:5000
    buf.extend(encode_dns_name(instance_airplay + "._airplay._tcp.local"))
    buf.extend(struct.pack(">H", 33))  # SRV type
    buf.extend(struct.pack(">H", 0x8001))  # Cache flush + IN
    buf.extend(struct.pack(">I", 120))  # TTL
    rdlen_pos = len(buf)
    buf.extend(struct.pack(">H", 0))  # RDLEN placeholder
    rdata_start = len(buf)
    buf.extend(struct.pack(">H", 0))  # Priority
    buf.extend(struct.pack(">H", 0))  # Weight
    buf.extend(struct.pack(">H", 5000))  # Port
    buf.extend(encode_dns_name(hostname + ".local"))
    rdlen = len(buf) - rdata_start
    struct.pack_into(">H", buf, rdlen_pos, rdlen)

    # TXT: instance._raop._tcp.local TXT (AirPlay spec properties)
    txt_parts = [
        b"txtvers=1",
        b"ch=2",
        b"cn=0,1,2,3",
        b"da=true",
        b"et=0,3,5",
        b"md=0,1,2",
        b"pw=false",
        b"sv=false",
        b"sr=44100",
        b"ss=16",
        b"tp=UDP",
        b"vn=65537",
        b"vs=130.14",
        b"am=PortableSpeaker",
        b"sf=0x4",
    ]
    buf.extend(encode_dns_name(instance_raop + "._raop._tcp.local"))
    buf.extend(struct.pack(">H", 16))  # TXT type
    buf.extend(struct.pack(">H", 0x8001))  # Cache flush + IN
    buf.extend(struct.pack(">I", 4500))  # TTL
    rdlen_pos = len(buf)
    buf.extend(struct.pack(">H", 0))  # RDLEN placeholder
    rdata_start = len(buf)
    for txt in txt_parts:
        buf.append(len(txt))
        buf.extend(txt)
    rdlen = len(buf) - rdata_start
    struct.pack_into(">H", buf, rdlen_pos, rdlen)

    # TXT: instance._airplay._tcp.local TXT (same properties)
    buf.extend(encode_dns_name(instance_airplay + "._airplay._tcp.local"))
    buf.extend(struct.pack(">H", 16))  # TXT type
    buf.extend(struct.pack(">H", 0x8001))  # Cache flush + IN
    buf.extend(struct.pack(">I", 4500))  # TTL
    rdlen_pos = len(buf)
    buf.extend(struct.pack(">H", 0))  # RDLEN placeholder
    rdata_start = len(buf)
    for txt in txt_parts:
        buf.append(len(txt))
        buf.extend(txt)
    rdlen = len(buf) - rdata_start
    struct.pack_into(">H", buf, rdlen_pos, rdlen)

    # A: hostname A IP
    buf.extend(encode_dns_name(hostname + ".local"))
    buf.extend(struct.pack(">H", 1))  # A type
    buf.extend(struct.pack(">H", 0x8001))  # Cache flush + IN
    buf.extend(struct.pack(">I", 120))  # TTL
    buf.extend(struct.pack(">H", 4))  # RDLEN
    buf.extend(socket.inet_aton(ip))  # IPv4 address

    return bytes(buf)


def main(duration=30):
    ip = get_local_ip()
    hostname = get_windows_hostname()
    mac_hex = "{:012X}".format(uuid.getnode())
    instance_raop = f"{mac_hex}@PortableSpeaker@{hostname}"
    instance_airplay = f"{mac_hex}@PortableSpeaker@{hostname}"

    print(f"[mdns] IP: {ip}")
    print(f"[mdns] Hostname: {hostname}")
    print(f"[mdns] Instance: {instance_raop}")

    # Build packet
    packet = build_mdns_packet(ip, hostname, instance_raop, instance_airplay)
    print(f"[mdns] Packet size: {len(packet)} bytes")

    # Create multicast socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    # Join multicast group
    mreq = socket.inet_aton("224.0.0.251") + socket.inet_aton("0.0.0.0")
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)

    try:
        # Send packet every 5 seconds for 'duration' seconds
        start = time.time()
        count = 0
        while time.time() - start < duration:
            sock.sendto(packet, ("224.0.0.251", 5353))
            count += 1
            print(f"[mdns] sent packet #{count}")
            time.sleep(5)
    except KeyboardInterrupt:
        print("[mdns] interrupted")
    finally:
        sock.close()
        print(f"[mdns] sent {count} packets total")


if __name__ == "__main__":
    main(30)
