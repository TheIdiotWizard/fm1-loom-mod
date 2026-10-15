#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The wavetable engine's tables: 8 tables x 16 frames x 256 int16 samples (64 KB in flash).

    tools/gen_wavetables.py build/gen/wavetables.h

Every frame is built from a harmonic recipe or drawn in the time domain and then band-limited to
NHARM harmonics through a plain 256-point DFT (no numpy), so the device plays them with a linear
read and no gross aliasing below about C6. Frames are peak-normalised so the position knob does
not change the level. The header defines the table (synth.c includes it once)."""
import math
import sys

N = 256
FRAMES = 16
NHARM = 40
TWO_PI = 2.0 * math.pi

TABLES = ["sine", "pulse", "vocal", "harmon", "digital", "fold", "organ", "metal"]


def from_harmonics(amp, phase=None):
    """amp[k] = amplitude of harmonic k+1"""
    out = []
    for i in range(N):
        t = i / N
        s = 0.0
        for k, a in enumerate(amp[:NHARM]):
            if a:
                ph = phase[k] if phase else 0.0
                s += a * math.sin(TWO_PI * (k + 1) * t + ph)
        out.append(s)
    return out


def bandlimit(x):
    """keep harmonics 1..NHARM of a time-domain cycle (DFT, 256 points)"""
    re = []
    im = []
    for k in range(1, NHARM + 1):
        sr = si = 0.0
        for i in range(N):
            a = TWO_PI * k * i / N
            sr += x[i] * math.cos(a)
            si -= x[i] * math.sin(a)
        re.append(sr * 2.0 / N)
        im.append(si * 2.0 / N)
    out = []
    for i in range(N):
        s = 0.0
        for k in range(NHARM):
            a = TWO_PI * (k + 1) * i / N
            s += re[k] * math.cos(a) - im[k] * math.sin(a)
        out.append(s)
    return out


def normalise(x):
    p = max(abs(v) for v in x) or 1.0
    return [v / p for v in x]


def lerp(a, b, t):
    return a + (b - a) * t


# -------------------------------------------------------------------- the tables ---
def t_sine(f):
    """sine -> triangle -> saw: the harmonic rolloff eases from 1 partial to 1/n"""
    u = f / (FRAMES - 1)
    if u < 0.5:                                     # sine -> triangle (odd, 1/n^2, alternating)
        w = u * 2.0
        amp = [0.0] * NHARM
        for k in range(NHARM):
            n = k + 1
            if n == 1:
                amp[k] = 1.0
            elif n % 2 == 1:
                amp[k] = w * (1.0 / (n * n)) * (1.0 if (n // 2) % 2 == 0 else -1.0)
        return from_harmonics(amp)
    w = (u - 0.5) * 2.0                             # triangle -> saw
    tri = [0.0] * NHARM
    saw = [0.0] * NHARM
    for k in range(NHARM):
        n = k + 1
        if n % 2 == 1:
            tri[k] = (1.0 / (n * n)) * (1.0 if (n // 2) % 2 == 0 else -1.0)
        saw[k] = 1.0 / n
    return from_harmonics([lerp(tri[k], saw[k], w) for k in range(NHARM)])


def t_pulse(f):
    """pulse widths 50 % -> 3 %"""
    d = lerp(0.5, 0.03, f / (FRAMES - 1))
    amp = [2.0 * math.sin(math.pi * (k + 1) * d) / ((k + 1) * math.pi) for k in range(NHARM)]
    return from_harmonics(amp, [math.pi / 2 - math.pi * (k + 1) * d for k in range(NHARM)])


VOWELS = [(800, 1150, 2900), (400, 1600, 2700), (350, 1700, 2700), (450, 800, 2830), (325, 700, 2530)]   # a e i o u


def t_vocal(f):
    """formants of a-e-i-o-u on a 110 Hz voice, morphing between vowels"""
    u = f / (FRAMES - 1) * (len(VOWELS) - 1)
    i0 = min(int(u), len(VOWELS) - 2)
    w = u - i0
    f0 = 110.0
    amp = []
    for k in range(NHARM):
        hz = f0 * (k + 1)
        g = 0.0
        for j, (fa, fb) in enumerate(zip(VOWELS[i0], VOWELS[i0 + 1])):
            fc = lerp(fa, fb, w)
            bw = 80.0 + 40.0 * j
            g += (1.0 if j == 0 else 0.6 if j == 1 else 0.3) * math.exp(-((hz - fc) / bw) ** 2 * 0.5)
        amp.append(g + 0.02 / (k + 1))
    return from_harmonics(amp)


def t_harmon(f):
    """the harmonic series stacking up: frame k adds harmonic k+1, equal weights"""
    amp = [0.0] * NHARM
    for k in range(f + 1):
        amp[k] = 1.0 / math.sqrt(k + 1)
    return from_harmonics(amp)


def t_digital(f):
    """a sine quantised to fewer and fewer steps, then sample-and-held: aliasing by design, tamed by the DFT"""
    levels = max(2, int(round(lerp(64, 2, f / (FRAMES - 1)))))
    hold = 1 + f // 2
    x = []
    for i in range(N):
        j = (i // hold) * hold
        s = math.sin(TWO_PI * j / N)
        x.append(round(s * levels * 0.5) / (levels * 0.5))
    return bandlimit(x)


def t_fold(f):
    """a triangle folded harder and harder"""
    g = lerp(1.0, 6.0, f / (FRAMES - 1))
    x = []
    for i in range(N):
        t = i / N
        tri = 4.0 * abs(t - 0.5) - 1.0
        y = tri * g
        while y > 1.0 or y < -1.0:                  # fold back into [-1, 1]
            y = 2.0 - y if y > 1.0 else -2.0 - y
        x.append(y)
    return bandlimit(x)


ORGAN = [[8, 0, 0, 0, 0, 0, 0, 0, 0], [8, 8, 0, 0, 0, 0, 0, 0, 0], [8, 8, 8, 0, 0, 0, 0, 0, 0], [8, 0, 8, 0, 0, 0, 8, 0, 0],
         [8, 8, 8, 8, 0, 0, 0, 0, 0], [8, 6, 8, 6, 4, 0, 0, 0, 0], [8, 8, 8, 8, 8, 8, 8, 8, 8], [0, 0, 8, 8, 8, 8, 8, 8, 8]]
ORGAN_RATIO = [1, 3, 2, 4, 6, 8, 10, 12, 16]        # 8' 5 1/3' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1' relative to 8'


def t_organ(f):
    u = f / (FRAMES - 1) * (len(ORGAN) - 1)
    i0 = min(int(u), len(ORGAN) - 2)
    w = u - i0
    amp = [0.0] * NHARM
    for j, r in enumerate(ORGAN_RATIO):
        if r - 1 < NHARM:
            amp[r - 1] += lerp(ORGAN[i0][j], ORGAN[i0 + 1][j], w) / 8.0
    return from_harmonics(amp)


def t_metal(f):
    """sparse high partials, as an FM bell's spectrum: more and higher as the frame rises"""
    u = f / (FRAMES - 1)
    amp = [0.0] * NHARM
    amp[0] = 1.0 - 0.6 * u
    parts = [3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37]
    for j, n in enumerate(parts):
        if n <= NHARM:
            amp[n - 1] = max(0.0, u * 2.0 - j * 0.15) * (0.7 / (1 + j * 0.3))
    return from_harmonics(amp, [0.37 * k * k for k in range(NHARM)])


GEN = [t_sine, t_pulse, t_vocal, t_harmon, t_digital, t_fold, t_organ, t_metal]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "build/gen/wavetables.h"
    lines = ["/* SPDX-License-Identifier: GPL-3.0-only */",
             "/* generated by tools/gen_wavetables.py: %d tables x %d frames x %d samples; do not edit */" % (len(GEN), FRAMES, N),
             "#pragma once", "#include <stdint.h>",
             "#define WT_NTABLE %d" % len(GEN), "#define WT_NFRAME %d" % FRAMES, "#define WT_N %d" % N,
             "const int16_t WT[%d][%d][%d] = {" % (len(GEN), FRAMES, N)]
    for ti, gen in enumerate(GEN):
        lines.append("{ /* %s */" % TABLES[ti])
        for f in range(FRAMES):
            x = normalise(gen(f))
            vals = [int(max(-32767, min(32767, round(v * 32000)))) for v in x]
            rows = []
            for i in range(0, N, 16):
                rows.append(",".join("%d" % v for v in vals[i:i + 16]))
            lines.append("{" + ",\n".join(rows) + "},")
        lines.append("},")
    lines.append("};")
    with open(out, "w") as fp:
        fp.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
