#!/usr/bin/env python3
"""
Send mDNS response packets (RAOP and AirPlay) as unicast UDP to a target IP
for quick testing whether multicast/IGMP is being blocked.

Usage:
  python send_unicast_mdns.py 10.0.0.31

This will send two responses (one for _raop._tcp.local and one for
_airplay._tcp.local) directly to target:5353.
"""
import socket
import struct
import sys


def write_name(buf, name):
    for part in name.split('.'):
        buf.append(struct.pack('B', len(part)))
        buf.append(part.encode())
    buf.append(b'\x00')


def write16(buf, v):
    buf.append(struct.pack('!H', v))


def write32(buf, v):
    buf.append(struct.pack('!I', v))


def append_txt(buf, txt):
    b = txt.encode()
    buf.append(struct.pack('B', len(b)))
    buf.append(b)


def build_packet(service, instance, hostname, local_ip):
    parts = []
    parts.append(struct.pack('!H', 0))
    parts.append(struct.pack('!H', 0x8400))
    parts.append(struct.pack('!H', 0))
    parts.append(struct.pack('!H', 4))
    parts.append(struct.pack('!H', 0))
    parts.append(struct.pack('!H', 0))

    # PTR
    write_name(parts, service)
    write16(parts, 12)
    write16(parts, 0x8001)
    write32(parts, 120)
    rdata = []
    write_name(rdata, instance)
    write_name(rdata, service)
    rdata_b = b''.join(rdata)
    write16(parts, len(rdata_b))
    parts.append(rdata_b)

    # SRV
    write_name(parts, instance)
    write_name(parts, service)
    write16(parts, 33)
    write16(parts, 0x8001)
    write32(parts, 120)
    rd = []
    rd.append(struct.pack('!H', 0))
    rd.append(struct.pack('!H', 0))
    rd.append(struct.pack('!H', 5000))
    write_name(rd, hostname)
    rd_b = b''.join(rd)
    write16(parts, len(rd_b))
    parts.append(rd_b)

    # TXT
    write_name(parts, instance)
    write_name(parts, service)
    write16(parts, 16)
    write16(parts, 0x8001)
    write32(parts, 4500)
    txtparts = []
    append_txt(txtparts, 'sf=0x4')
    append_txt(txtparts, 'fv=1.0')
    append_txt(txtparts, f'am={instance.split("@")[1]}')
    append_txt(txtparts, 'vs=105.1')
    append_txt(txtparts, 'tp=TCP,UDP')
    append_txt(txtparts, 'vn=65537')
    append_txt(txtparts, 'ss=16')
    append_txt(txtparts, 'sr=44100')
    append_txt(txtparts, 'da=true')
    append_txt(txtparts, 'sv=false')
    append_txt(txtparts, 'et=0,1')
    append_txt(txtparts, 'ek=1')
    append_txt(txtparts, 'cn=0,1')
    append_txt(txtparts, 'ch=2')
    append_txt(txtparts, 'txtvers=1')
    append_txt(txtparts, 'pw=false')
    txt_b = b''.join(txtparts)
    write16(parts, len(txt_b))
    parts.append(txt_b)

    # A record
    write_name(parts, hostname)
    write16(parts, 1)
    write16(parts, 0x8001)
    write32(parts, 120)
    write16(parts, 4)
    try:
        a = socket.inet_aton(local_ip)
    except Exception:
        a = socket.inet_aton('127.0.0.1')
    parts.append(a)

    return b''.join(parts)


def get_local_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(('8.8.8.8', 53))
        ip = s.getsockname()[0]
    except Exception:
        ip = '127.0.0.1'
    finally:
        s.close()
    return ip


def main():
    if len(sys.argv) < 2:
        print('Usage: python send_unicast_mdns.py <target-ip>')
        sys.exit(1)
    target = sys.argv[1]
    local_ip = get_local_ip()
    instance = f'{local_ip.replace('.', '-')}-PortableSpeaker'
    hostname = 'PortableSpeaker.local'
    pkt1 = build_packet('_raop._tcp.local', instance, hostname, local_ip)
    pkt2 = build_packet('_airplay._tcp.local', instance, hostname, local_ip)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    for i in range(3):
        try:
            n = sock.sendto(pkt1, (target, 5353))
            print('sent raop ->', target, n)
            n = sock.sendto(pkt2, (target, 5353))
            print('sent airplay ->', target, n)
        except Exception as e:
            print('send error', e)
        time.sleep(1)

if __name__ == '__main__':
    main()
