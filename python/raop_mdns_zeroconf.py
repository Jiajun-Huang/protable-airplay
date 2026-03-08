#!/usr/bin/env python3
"""
Zeroconf-based mDNS PoC for advertising _raop._tcp.local and _airplay._tcp.local
Run:
  python -m pip install -r requirements.txt
  python raop_mdns_zeroconf.py

This registers two services using the Zeroconf library so we can verify
iOS discovery without building the C binary. Ctrl-C to stop and unregister.
"""
import socket
import time

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


def make_props():
    # keys and values must be bytes or strs (zeroconf will convert)
    props = {
        "sf": "0x4",
        "fv": "1.0",
        "am": "PortableSpeaker",
        "vs": "105.1",
        "tp": "TCP,UDP",
        "vn": "65537",
        "ss": "16",
        "sr": "44100",
        "da": "true",
        "sv": "false",
        "et": "0,1",
        "ek": "1",
        "cn": "0,1",
        "ch": "2",
        "txtvers": "1",
        "pw": "false",
    }
    return props


def main():
    ip = get_local_ip()
    addr = socket.inet_aton(ip)
    instance = f"{ip.replace('.', '-')}-PortableSpeaker"  # simple unique instance
    hostname = "PortableSpeaker.local."

    zeroconf = Zeroconf()
    try:
        props = make_props()

        info_raop = ServiceInfo(
            "_raop._tcp.local.",
            f"{instance}._raop._tcp.local.",
            addresses=[addr],
            port=5000,
            properties=props,
            server=hostname,
        )

        info_airplay = ServiceInfo(
            "_airplay._tcp.local.",
            f"{instance}._airplay._tcp.local.",
            addresses=[addr],
            port=5000,
            properties=props,
            server=hostname,
        )

        zeroconf.register_service(info_raop)
        print("[zeroconf] registered _raop._tcp.local")
        zeroconf.register_service(info_airplay)
        print("[zeroconf] registered _airplay._tcp.local")
        print("[zeroconf] advertising. Press Ctrl-C to stop.")

        while True:
            time.sleep(5)

    except KeyboardInterrupt:
        print("\n[zeroconf] unregistering and exiting")
    finally:
        try:
            zeroconf.unregister_service(info_raop)
            zeroconf.unregister_service(info_airplay)
        except Exception:
            pass
        zeroconf.close()


if __name__ == "__main__":
    main()
