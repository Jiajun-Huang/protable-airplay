"""Regenerate synthetic ALAC fixtures with PyAV/FFmpeg; not needed to run C tests."""
import math
from pathlib import Path
import struct
import av

frames = 352
out = Path(__file__).parent / "fixtures"
out.mkdir(exist_ok=True)
lines = ["/* Synthetic PCM encoded by PyAV/FFmpeg. Regenerate with generate_alac_fixtures.py. */"]
for name, channels in [("tone", 2), ("silence", 2), ("mono", 1)]:
    pcm = [[0 if name == "silence" else int(6000 * math.sin(2 * math.pi * (440 + ch * 233) * i / 44100))
            for i in range(frames)] for ch in range(channels)]
    encoder = av.CodecContext.create("alac", "w")
    encoder.sample_rate = 44100
    encoder.layout = "mono" if channels == 1 else "stereo"
    encoder.format = "s16p"
    encoder.open()
    frame = av.AudioFrame(format="s16p", layout=encoder.layout, samples=frames)
    frame.sample_rate = 44100
    for ch in range(channels):
        frame.planes[ch].update(struct.pack("<" + "h" * frames, *pcm[ch]))
    packets = encoder.encode(frame) + encoder.encode(None)
    assert len(packets) == 1
    packet = bytes(packets[0])
    expected = [pcm[ch][i] for i in range(frames) for ch in range(channels)]
    decoder = av.CodecContext.create("alac", "r")
    decoder.extradata = encoder.extradata
    decoded = decoder.decode(av.Packet(packet))
    assert len(decoded) == 1 and decoded[0].samples == frames
    for ch in range(channels):
        actual = struct.unpack("<" + "h" * frames, bytes(decoded[0].planes[ch])[:frames * 2])
        assert list(actual) == pcm[ch]
    for suffix, kind, values in [("packet", "uint8_t", packet), ("pcm", "int16_t", expected)]:
        lines.append(f"static const {kind} {name}_{suffix}[] = {{")
        lines += ["    " + ",".join(str(x) for x in values[i:i+16]) + "," for i in range(0, len(values), 16)]
        lines.append("};")
    print(name, "packet bytes", len(packet), "extradata", encoder.extradata.hex())
(out / "alac.h").write_text("\n".join(lines) + "\n", encoding="utf-8")
