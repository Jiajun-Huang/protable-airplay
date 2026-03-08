#!/usr/bin/env python3
"""
Simple mDNS responder PoC for _raop._tcp.local and _airplay._tcp.local
Run this, then use Wireshark to capture udp.dstport == 5353 and watch the
packets. This is a minimal proof-of-concept, not a full Zeroconf implementation.
"""
import socket
import struct
import sys
import time

MCAST_GRP = "224.0.0.251"
MCAST_PORT = 5353

# helpers for DNS-style encoding


def write_name(buf, name):
    parts = name.split(".")
    for part in parts:
        l = len(part)
        buf.append(struct.pack("B", l))
        buf.append(part.encode("utf-8"))
    buf.append(b"\x00")


def write16(buf, v):
    buf.append(struct.pack("!H", v))


def write32(buf, v):
    buf.append(struct.pack("!I", v))


def append_txt(buf, txt):
    b = txt.encode("utf-8")
    buf.append(struct.pack("B", len(b)))
    buf.append(b)


def build_packet(service, instance, hostname, local_ip):
    parts = []
    # header
    parts.append(struct.pack("!H", 0x0000))  # id
    parts.append(struct.pack("!H", 0x8400))  # flags: response + authoritative
    parts.append(struct.pack("!H", 0))  # qdcount
    parts.append(struct.pack("!H", 4))  # ancount
    parts.append(struct.pack("!H", 0))  # nscount
    parts.append(struct.pack("!H", 0))  # arcount

    # PTR
    write_name(parts, service)
    write16(parts, 12)  # TYPE PTR
    write16(parts, 0x8001)  # CLASS IN + cache flush
    write32(parts, 120)  # TTL
    # rdata length placeholder
    rdata = []
    write_name(rdata, instance)
    write_name(rdata, service)
    rdata_bytes = b"".join(rdata)
    write16(parts, len(rdata_bytes))
    parts.append(rdata_bytes)

    # SRV
    write_name(parts, instance)
    write_name(parts, service)
    write16(parts, 33)  # TYPE SRV
    write16(parts, 0x8001)
    write32(parts, 120)
    # rdata
    rd = []
    rd.append(struct.pack("!H", 0))  # priority
    rd.append(struct.pack("!H", 0))  # weight
    rd.append(struct.pack("!H", 5000))
    write_name(rd, hostname)
    rd_bytes = b"".join(rd)
    write16(parts, len(rd_bytes))
    parts.append(rd_bytes)

    # TXT
    write_name(parts, instance)
    write_name(parts, service)
    write16(parts, 16)
    write16(parts, 0x8001)
    write32(parts, 4500)
    txt_buf = []
    # common keys
    append_txt(txt_buf, "sf=0x4")
    append_txt(txt_buf, "fv=1.0")
    append_txt(txt_buf, f'am={instance.split("@")[1]}')
    append_txt(txt_buf, "vs=105.1")
    append_txt(txt_buf, "tp=TCP,UDP")
    append_txt(txt_buf, "vn=65537")
    append_txt(txt_buf, "ss=16")
    append_txt(txt_buf, "sr=44100")
    append_txt(txt_buf, "da=true")
    append_txt(txt_buf, "sv=false")
    append_txt(txt_buf, "et=0,1")
    append_txt(txt_buf, "ek=1")
    append_txt(txt_buf, "cn=0,1")
    append_txt(txt_buf, "ch=2")
    append_txt(txt_buf, "txtvers=1")
    append_txt(txt_buf, "pw=false")
    txt_bytes = b"".join(txt_buf)
    write16(parts, len(txt_bytes))
    parts.append(txt_bytes)

    # A record
    write_name(parts, hostname)
    write16(parts, 1)
    write16(parts, 0x8001)
    write32(parts, 120)
    write16(parts, 4)
    try:
        a = socket.inet_aton(local_ip)
    except Exception:
        a = socket.inet_aton("127.0.0.1")
    parts.append(a)

    return b"".join(parts)


def get_local_ip():
    # attempt to discover local IP by connecting to a public IP
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 53))
        ip = s.getsockname()[0]
    except Exception:
        ip = "127.0.0.1"
    finally:
        s.close()
    return ip


def main():
    local_ip = get_local_ip()
    instance = f"PYPOC@PortableSpeaker"
    hostname = "PortableSpeaker.local"

    pkt1 = build_packet("_raop._tcp.local", instance, hostname, local_ip)
    pkt2 = build_packet("_airplay._tcp.local", instance, hostname, local_ip)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        # set TTL and enable loop
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1)
    except Exception:
        pass

    dest = (MCAST_GRP, MCAST_PORT)
    print(f"Will multicast {len(pkt1)} and {len(pkt2)} bytes to {dest} every 5s")
    try:
        while True:
            try:
                n = sock.sendto(pkt1, dest)
                print("[poc] sent raop", n)
                n = sock.sendto(pkt2, dest)
                print("[poc] sent airplay", n)
            except Exception as e:
                print("send error", e)
            time.sleep(5)
    except KeyboardInterrupt:
        print("stopped")


if __name__ == "__main__":
    main()
