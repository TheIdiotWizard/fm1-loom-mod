#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""LOOM-8's factory samples: placeholders made here, procedurally (no recordings), until the real ones
are chosen: a kick, a snare, a hat, a clap, a sung "ah" (a steady vowel, looped), a piano-ish tone and a
noise sweep, mono at 22050 Hz, IMA ADPCM with a checkpoint every 64 samples (tools/sampleio.py), into a
header the firmware compiles in (dsp/sample.c). Deterministic: the same bytes every run.

  gen_samples.py OUT.h [WAVDIR]      WAVDIR: also each sample as a 16-bit WAV (to listen to)
"""
import math
import struct
import sys
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sampleio as sio  # noqa: E402

SR = sio.RATE
TWO_PI = 2.0 * math.pi


class Rng:
    """a small LCG: the same noise everywhere"""
    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFF

    def __call__(self):
        self.s = (self.s * 1664525 + 1013904223) & 0xFFFFFFFF
        return (self.s / 2147483648.0) - 1.0


def secs(t):
    return int(round(t * SR))


def onepole(x, hz, hp=False):
    a = 1.0 - math.exp(-TWO_PI * hz / SR)
    y, out = 0.0, []
    for v in x:
        y += a * (v - y)
        out.append(v - y if hp else y)
    return out


def bandpass(x, hz_of, q):
    """a state-variable band-pass whose centre may move (hz_of: index -> Hz)"""
    lo = bp = 0.0
    out = []
    for i, v in enumerate(x):
        f = 2.0 * math.sin(math.pi * min(hz_of(i), SR * 0.24) / SR)
        hp = v - lo - bp / q
        bp += f * hp
        lo += f * bp
        out.append(bp)
    return out


def kick():
    n, ph, out, r = secs(0.30), 0.0, [], Rng(1)
    for i in range(n):
        t = i / SR
        hz = 48.0 + 110.0 * math.exp(-t / 0.035)
        ph += hz / SR
        body = math.sin(TWO_PI * ph) * math.exp(-t / 0.11)
        click = r() * math.exp(-t / 0.002) * 0.5
        out.append(math.tanh(1.6 * body) + click)
    return out


def snare():
    n, r, out = secs(0.25), Rng(2), []
    noise = bandpass([r() for _ in range(n)], lambda i: 3000.0, 0.8)
    p1 = p2 = 0.0
    for i in range(n):
        t = i / SR
        p1 += 185.0 * (1.0 + 0.3 * math.exp(-t / 0.01)) / SR
        p2 += 330.0 * (1.0 + 0.3 * math.exp(-t / 0.01)) / SR
        body = (0.6 * math.sin(TWO_PI * p1) + 0.4 * math.sin(TWO_PI * p2)) * math.exp(-t / 0.045)
        out.append(body + 1.6 * noise[i] * math.exp(-t / 0.07))
    return out


def hat():
    n, r, out = secs(0.10), Rng(3), []
    hz = [205.3, 304.4, 369.6, 522.7, 540.0, 800.0]
    ph = [0.0] * 6
    for i in range(n):
        m = 0.0
        for k in range(6):
            ph[k] = (ph[k] + hz[k] * 1.9 / SR) % 1.0
            m += 1.0 if ph[k] < 0.5 else -1.0
        out.append((m / 6.0) * 0.6 + r() * 0.5)
    out = onepole(onepole(out, 6000.0, hp=True), 6000.0, hp=True)
    return [v * math.exp(-(i / SR) / 0.025) for i, v in enumerate(out)]


def clap():
    n, r, out = secs(0.25), Rng(4), []
    noise = bandpass([r() for _ in range(n)], lambda i: 1100.0, 1.6)
    starts = [0.0, 0.011, 0.022, 0.033]
    for i in range(n):
        t = i / SR
        e = sum(math.exp(-(t - s) / 0.006) for s in starts if t >= s)
        tail = math.exp(-(t - 0.033) / 0.07) if t >= 0.033 else 0.0
        out.append(noise[i] * (e + 0.8 * tail))
    return out


def formant_gain(f, formants):
    g = 0.0
    for fc, bw, a in formants:
        q = fc / bw
        x = f / fc
        g += a / math.sqrt((1.0 - x * x) ** 2 + (x / q) ** 2)
    return g


def ah():
    """a sung "ah": a period of exactly 100 samples (220.5 Hz, A3 + 4 cents) built from its harmonics
    under three formants, so whole periods loop seamlessly; a 40 ms onset, then the steady vowel"""
    per = 100
    f0 = SR / per
    formants = [(800.0, 80.0, 1.0), (1150.0, 90.0, 0.5), (2900.0, 120.0, 0.25)]
    r = Rng(5)
    harm = []
    for h in range(1, int(9000 / f0)):
        f = h * f0
        harm.append((h, formant_gain(f, formants) / math.sqrt(h), (r() + 1.0) * math.pi))
    cyc = [sum(a * math.sin(TWO_PI * h * i / per + p) for h, a, p in harm) for i in range(per)]
    n = per * 110                                    # 0.5 s
    out = []
    for i in range(n):
        t = i / SR
        on = min(1.0, t / 0.04)
        out.append(cyc[i % per] * on * on)
    loop = (per * 10, n)                             # from the 10th period (past the onset) to the end
    return out, loop, 57


def piano():
    """a struck string: twelve partials, slightly stretched, the higher ones dying sooner, and a felt thump"""
    n, r, out = secs(0.90), Rng(6), []
    f0 = sio.midi_hz(60)
    parts = []
    for k in range(1, 13):
        f = f0 * k * math.sqrt(1.0 + 0.0004 * k * k)
        if f > 10000:
            break
        parts.append((f, 1.0 / (k ** 1.1) * (1.0 if k != 7 else 0.3), 0.9 / (1.0 + 0.45 * (k - 1))))
    ph = [r() for _ in parts]
    thump = onepole([r() for _ in range(n)], 400.0)
    for i in range(n):
        t = i / SR
        v = 0.0
        for j, (f, a, d) in enumerate(parts):
            v += a * math.sin(TWO_PI * (f * t + ph[j])) * math.exp(-t / d)
        v *= min(1.0, t / 0.002)
        out.append(v + 3.0 * thump[i] * math.exp(-t / 0.012))
    return out, None, 60


def sweep():
    """noise through a resonant band-pass rising from 200 Hz to 8 kHz, swelling in and falling away"""
    n, r = secs(0.60), Rng(7)
    x = [r() for _ in range(n)]
    y = bandpass(x, lambda i: 200.0 * math.pow(40.0, i / n), 6.0)
    return [v * math.sin(math.pi * i / n) ** 0.7 for i, v in enumerate(y)]


SAMPLES = [("kick", kick), ("snare", snare), ("hat", hat), ("clap", clap), ("ah", ah), ("piano", piano),
           ("sweep", sweep)]


def build():
    out = []
    for name, fn in SAMPLES:
        v = fn()
        loop, root = None, 60
        if isinstance(v, tuple):
            v, loop, root = v
        out.append((name, sio.to_int16(v), root, loop))
    return out


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    smp = build()
    adp_all, ck_all, rows = bytearray(), [], []
    for name, s, root, loop in smp:
        adp, ck = sio.ima_encode(s)
        ls, le = loop if loop else (0, 0)
        rows.append((name, len(adp_all), len(ck_all), len(s), ls, le, root))
        adp_all += adp
        ck_all += ck
    total = len(adp_all) + 4 * len(ck_all)
    if total > 40 * 1024:
        sys.exit(f"gen_samples: {total} B, over the 40 KB the factory bank may take")
    lines = ["/* generated by tools/gen_samples.py: LOOM-8's factory samples (placeholders, made procedurally),",
             f" * IMA ADPCM at {sio.RATE} Hz, a checkpoint every {sio.BLK} samples; {len(adp_all)} B of ADPCM + "
             f"{4 * len(ck_all)} B of checkpoints */",
             "#pragma once",
             f"#define SMP_NFACT {len(rows)}",
             f"static const uint8_t SMP_FACT_ADP[{len(adp_all)}] = {{"]
    for i in range(0, len(adp_all), 24):
        lines.append("    " + ", ".join(str(b) for b in adp_all[i:i + 24]) + ",")
    lines.append("};")
    lines.append(f"static const uint32_t SMP_FACT_CK[{len(ck_all)}] = {{")
    for i in range(0, len(ck_all), 8):
        lines.append("    " + ", ".join(f"0x{c:08X}u" for c in ck_all[i:i + 8]) + ",")
    lines.append("};")
    lines.append("/* name, ADPCM offset, checkpoint index, samples, loop start, loop end (0: none), root */")
    lines.append("static const struct { const char *name; uint32_t adp, ck, n, ls, le; uint8_t root; } "
                 "SMP_FACT[SMP_NFACT] = {")
    for name, a, c, n, ls, le, root in rows:
        lines.append(f'    {{"{name}", {a}u, {c}u, {n}u, {ls}u, {le}u, {root}}},')
    lines.append("};")
    Path(sys.argv[1]).parent.mkdir(parents=True, exist_ok=True)
    Path(sys.argv[1]).write_text("\n".join(lines) + "\n")
    if len(sys.argv) > 2:
        d = Path(sys.argv[2])
        d.mkdir(parents=True, exist_ok=True)
        for name, s, _, _ in smp:
            with wave.open(str(d / f"{name}.wav"), "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(sio.RATE)
                w.writeframes(struct.pack("<%dh" % len(s), *s))
    print(f"samples: {len(rows)}, {total} B")


if __name__ == "__main__":
    main()
