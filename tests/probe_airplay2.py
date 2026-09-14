"""Independent transient-pairing/control probe. Requires Python cryptography."""
import argparse
import hashlib
import logging
import secrets
import socket
import struct
import plistlib

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.kdf.hkdf import HKDF
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305

# RFC 5054 3072-bit group.
N = int("""
FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1
29024E088A67CC74020BBEA63B139B22514A08798E3404DD
EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245
E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED
EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3D
C2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F
83655D23DCA3AD961C62F356208552BB9ED529077096966D
670C354E4ABC9804F1746C08CA18217C32905E462E36CE3B
E39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9
DE2BCBF6955817183995497CEA956AE515D2261898FA0510
15728E5A8AAAC42DAD33170D04507A33A85521ABDF1CBA64
ECFB850458DBEF0A8AEA71575D060C7DB3970F85A6E1E4C7
ABF5AE8CDB0933D71E8C94E04A25619DCEE3D2261AD2EE6B
F12FFA06D98A0864D87602733EC86A64521F2B18177B200C
BBE117577A615D6C770988C0BAD946E208E24FA074E5AB3143
DB5BFCE0FD108E4B82D120A93AD2CAFFFFFFFFFFFFFFFF
""".replace("\n", ""), 16)

def H(*parts):
    return hashlib.sha512(b"".join(parts)).digest()

def integer(value, size=None):
    return value.to_bytes(size or max(1, (value.bit_length() + 7) // 8), "big")

def tlv(**values):
    output = bytearray()
    for kind, data in values.items():
        kind = int(kind[1:])
        for offset in range(0, len(data), 255):
            part = data[offset:offset + 255]
            output.extend(bytes([kind, len(part)]) + part)
    return bytes(output)

def parse_tlv(data):
    result = {}
    while data:
        kind, length = data[:2]
        assert len(data) >= length + 2
        result[kind] = result.get(kind, b"") + data[2:length + 2]
        data = data[length + 2:]
    return result

class Client:
    def __init__(self, host, port):
        self.socket = socket.create_connection((host, port), timeout=5)
        self.pending = b""
        self.encrypt = self.decrypt = None
        self.sent = self.received = self.cseq = 0

    def exact(self, count):
        data = b""
        while len(data) < count:
            part = self.socket.recv(count - len(data))
            assert part, "Receiver closed the connection"
            data += part
        return data

    def request(self, method, path, body=b"", content_type="application/octet-stream", expected_status=200):
        self.cseq += 1
        request = (f"{method} {path} RTSP/1.0\r\nCSeq: {self.cseq}\r\n"
                   f"Content-Type: {content_type}\r\nContent-Length: {len(body)}\r\n\r\n").encode() + body
        if self.encrypt:
            records = b""
            for offset in range(0, len(request), 1024):
                part = request[offset:offset + 1024]
                length = struct.pack("<H", len(part))
                nonce = b"\0" * 4 + struct.pack("<Q", self.sent)
                records += length + self.encrypt.encrypt(nonce, part, length)
                self.sent += 1
            # Deliberately split the encrypted length prefix across TCP sends.
            self.socket.sendall(records[:1])
            self.socket.sendall(records[1:])
        else:
            self.socket.sendall(request)
        while True:
            if b"\r\n\r\n" in self.pending:
                header, tail = self.pending.split(b"\r\n\r\n", 1)
                lines = header.decode().split("\r\n")
                headers = dict(line.lower().split(":", 1) for line in lines[1:] if ":" in line)
                length = int(headers.get("content-length", "0"))
                if len(tail) >= length:
                    self.pending = tail[length:]
                    assert int(lines[0].split()[1]) == expected_status, lines[0]
                    return tail[:length]
            if self.decrypt:
                length = self.exact(2)
                ciphertext = self.exact(struct.unpack("<H", length)[0] + 16)
                nonce = b"\0" * 4 + struct.pack("<Q", self.received)
                self.pending += self.decrypt.decrypt(nonce, ciphertext, length)
                self.received += 1
            else:
                part = self.socket.recv(4096)
                assert part, "Receiver closed the connection"
                self.pending += part

    def pair(self):
        reply = parse_tlv(self.request("POST", "/pair-setup",
                         tlv(t6=b"\1", t0=b"\0", t19=b"\x10")))
        assert reply[6] == b"\2" and len(reply[2]) == 16
        salt, B = reply[2], int.from_bytes(reply[3], "big")
        assert B % N
        a = secrets.randbits(256)
        A = pow(5, a, N)
        k = int.from_bytes(H(integer(N, 384), integer(5, 384)), "big")
        u = int.from_bytes(H(integer(A, 384), integer(B, 384)), "big")
        x = int.from_bytes(H(salt, H(b"Pair-Setup:3939")), "big")
        S = pow((B - k * pow(5, x, N)) % N, a + u * x, N)
        key = H(integer(S))
        xor = bytes(a ^ b for a, b in zip(H(integer(N)), H(b"\5")))
        proof = H(xor, H(b"Pair-Setup"), salt.lstrip(b"\0") or b"\0", integer(A), integer(B), key)
        response = parse_tlv(self.request("POST", "/pair-setup", tlv(t6=b"\3", t3=integer(A, 384), t4=proof)))
        assert response.get(4) == H(integer(A), proof, key), "Server SRP proof mismatch"
        def derive(label):
            return HKDF(algorithm=hashes.SHA512(), length=32, salt=b"Control-Salt", info=label).derive(key)
        self.encrypt = ChaCha20Poly1305(derive(b"Control-Write-Encryption-Key"))
        self.decrypt = ChaCha20Poly1305(derive(b"Control-Read-Encryption-Key"))
        logging.info("SRP client and server proofs verified")

    def plist(self, method, path, data):
        return self.request(method, path, plistlib.dumps(data, fmt=plistlib.FMT_BINARY),
                            "application/x-apple-binary-plist")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("host", nargs="?", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5000)
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="[%(levelname)s][probe] %(message)s")
    c = Client(args.host, args.port)
    try:
        info = plistlib.loads(c.request("GET", "/info"))
        assert info["vv"] == 2 and info["features"] & (1 << 48)
        c.pair()
        c.request("OPTIONS", "*")
        secure_info = plistlib.loads(c.request("GET", "/info"))
        assert secure_info == info
        c.request("RECORD", "/stream", expected_status=455)
        # SHA-256 of the four fixed version 3 wire responses, cross-checked
        # against the reference receiver independently of the C handler.
        replies = [
            "8e1a11ea61e4c397f30480910786893aaebb5bff59bf2074db9d7e84fda49dbc",
            "e3730ce34481b93312c97ecbebe5b2128ee16a8e4d0140e89904ac9ff7509df3",
            "c6ab6f9488d29f4c7ec395981cdd12f14a5fa5c51ce61ae6ce43c570f7eae989",
            "8c53e03e2a08e23558e48287851d1cf39297039a7a60bf906a582e22c92f826b",
        ]
        for mode, digest in enumerate(replies):
            first = b"FPLY\x03\x01\x01\x00" + struct.pack(">I", 4) + bytes([2, 0, mode, 0])
            reply = c.request("POST", "/fp-setup", first)
            assert len(reply) == 142 and hashlib.sha256(reply).hexdigest() == digest
            second = b"FPLY\x03\x01\x03\x00" + struct.pack(">I", 152) + bytes(range(152))
            reply = c.request("POST", "/fp-setup", second)
            assert reply == b"FPLY\x03\x01\x04\x00\x00\x00\x00\x14" + second[-20:]
        logging.info("Both FairPlay setup exchanges verified for all four modes over encrypted RTSP")
        setup = plistlib.loads(c.plist("SETUP", "/stream", {"timingProtocol": "PTP"}))
        assert 0 < setup["eventPort"] < 65536 and setup["timingPort"] == 0
        c.request("GET_PARAMETER", "/stream", b"volume\r\n", "text/parameters")
        c.request("RECORD", "/stream")
        logging.info("Control session RECORD before stream SETUP accepted")
        c.request("TEARDOWN", "/stream")
        logging.info("Encrypted RTSP, binary plist and initial SETUP passed for %s", info["name"])
    finally:
        c.socket.close()

if __name__ == "__main__":
    main()
